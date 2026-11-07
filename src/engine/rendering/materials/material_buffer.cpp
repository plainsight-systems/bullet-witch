#include "bw/rendering/materials/material_buffer.hpp"

#include "bw/memory/alignment.hpp"
#include "bw/rendering/materials/material.hpp"
#include "bw/rendering/rhi/access.hpp"
#include "bw/rendering/rhi/device.hpp"
#include "bw/rendering/rhi/resources.hpp"
#include "bw/rendering/shaders/parameter_block.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string_view>
#include <utility>

// Responsibility: the out-of-line part of bw/rendering/materials/material_buffer.hpp.
//
// Contract, with the readings the header leaves to the implementation:
//   - Capacity: at least shaders::kBlockAlignment and a multiple of it.
//   - add places a block at align_up(used, kBlockAlignment); used becomes
//     the block's end.
//   - A write locks exactly the range, copies the block and unlocks; the
//     CPU copy is updated only after the unlock succeeds, so a failed write
//     leaves the copy and the placement as they were.
//   - add appends each range it returns to slots_, so slots_ is in offset
//     order; update finds the slot there by binary search on the offset and
//     accepts it only when offset and size both match. Any other slot, a
//     forged one inside the placed bytes included, is SlotInvalid, as is a
//     slot whose size differs from the instance's block.
//
// A block is refilled when its values change, then reused by every draw.
// Every block is a range of one buffer, not a buffer of its own, which is
// what lets a draw name it by index and offset.
namespace bw::materials {
namespace {

[[nodiscard]] MaterialBufferFailure failed(MaterialBufferError error) {
  return MaterialBufferFailure{error, rhi::ResourceError::None, {}};
}

// Writes `bytes` at `offset` of the GPU buffer through one lock.
[[nodiscard]] bool write_range(rhi::Device& device, rhi::Buffer& buffer, std::uint32_t offset,
                               std::span<const std::byte> bytes) {
  rhi::LockResult locked = device.lock_buffer(buffer, rhi::BufferRange{offset, bytes.size()});
  if (!locked.ok()) {
    return false;
  }
  std::memcpy(locked.value().data(), bytes.data(), bytes.size());
  return device.unlock_buffer(buffer) == rhi::LockError::None;
}

} // namespace

MaterialBufferFailure create_material_buffer(rhi::Device& device, std::uint32_t capacity_bytes,
                                             std::string_view debug_name, MaterialBuffer& out) {
  out = MaterialBuffer{};
  if (capacity_bytes < shaders::kBlockAlignment ||
      capacity_bytes % shaders::kBlockAlignment != 0U) {
    return failed(MaterialBufferError::CapacityInvalid);
  }
  rhi::BufferDescription description{};
  description.size = capacity_bytes;
  description.usage = rhi::BufferUsage::ShaderResource | rhi::BufferUsage::Upload;
  description.initial_access = rhi::Access::ShaderResourcePixel;
  description.debug_name = debug_name;
  rhi::BufferResult created = device.create_buffer(description);
  if (!created.ok()) {
    return MaterialBufferFailure{MaterialBufferError::BufferFailed, created.error(),
                                 std::string(created.detail())};
  }
  rhi::ShaderResourceViewResult viewed =
      device.create_shader_resource_view(created.value(), rhi::BufferViewDescription{});
  if (!viewed.ok()) {
    return MaterialBufferFailure{MaterialBufferError::ViewFailed, viewed.error(),
                                 std::string(viewed.detail())};
  }
  MaterialBuffer built;
  built.buffer_ = std::move(created.value());
  built.view_ = std::move(viewed.value());
  built.bytes_.assign(capacity_bytes, std::byte{0});
  out = std::move(built);
  return {};
}

MaterialBufferFailure MaterialBuffer::add(rhi::Device& device, const MaterialInstance& instance,
                                          MaterialSlot& slot) {
  const std::span<const std::byte> block = instance.block();
  const std::uint64_t offset =
      bw::memory::align_up(std::uint64_t{used_}, std::uint64_t{shaders::kBlockAlignment});
  if (offset > bytes_.size() || block.size() > bytes_.size() - offset) {
    return failed(MaterialBufferError::Full);
  }
  const auto at = static_cast<std::uint32_t>(offset);
  if (!write_range(device, *buffer_, at, block)) {
    return failed(MaterialBufferError::WriteFailed);
  }
  std::copy(block.begin(), block.end(), bytes_.begin() + at);
  used_ = at + static_cast<std::uint32_t>(block.size());
  slot = MaterialSlot{at, static_cast<std::uint32_t>(block.size())};
  slots_.push_back(slot);
  return {};
}

MaterialBufferFailure MaterialBuffer::update(rhi::Device& device, MaterialSlot slot,
                                             const MaterialInstance& instance, bool& wrote) {
  wrote = false;
  const std::span<const std::byte> block = instance.block();
  const auto issued = std::lower_bound(
      slots_.begin(), slots_.end(), slot.offset,
      [](const MaterialSlot& held, std::uint32_t offset) { return held.offset < offset; });
  if (issued == slots_.end() || issued->offset != slot.offset || issued->size != slot.size ||
      slot.size != block.size()) {
    return failed(MaterialBufferError::SlotInvalid);
  }
  const auto held = bytes_.begin() + slot.offset;
  if (std::equal(block.begin(), block.end(), held)) {
    return {};
  }
  if (!write_range(device, *buffer_, slot.offset, block)) {
    return failed(MaterialBufferError::WriteFailed);
  }
  std::copy(block.begin(), block.end(), held);
  wrote = true;
  return {};
}

MaterialReference MaterialBuffer::reference(MaterialSlot slot) const noexcept {
  return MaterialReference{view_->buffer_index(), slot.offset, 0, 0};
}

} // namespace bw::materials
