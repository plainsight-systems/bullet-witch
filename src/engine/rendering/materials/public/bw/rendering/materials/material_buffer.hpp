#ifndef BW_RENDERING_MATERIALS_MATERIAL_BUFFER_HPP
#define BW_RENDERING_MATERIALS_MATERIAL_BUFFER_HPP

#include "bw/rendering/materials/material.hpp"
#include "bw/rendering/rhi/device.hpp"
#include "bw/rendering/rhi/resources.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// Responsibility: the one GPU buffer that holds material blocks
// (docs/architecture/materials.md, "Parameters at runtime": every material
// block lives in one buffer reached through the descriptor arrays; a block
// is filled only when an instance's values change, and the range it fills
// is shared by every draw of that instance). An instance is added once and
// gets a range; a draw names the range through a MaterialReference in its
// per-draw block; the shader reads the block from the buffer array at that
// offset with the loader the shader system generates
// (parameter_block.hpp, append_hlsl_buffer_loader).
//
// Contract.
//   1. create_material_buffer makes a buffer of `capacity_bytes` (a
//      multiple of shaders::kBlockAlignment and at least that many bytes;
//      anything else is CapacityInvalid), usage ShaderResource and Upload, initial
//      access ShaderResourcePixel, named `debug_name`, with one raw view
//      over the whole buffer (BufferFailed, ViewFailed with the device's
//      error and detail). On a failure `out` is left empty.
//   2. add places the instance's block at the next free offset, a multiple
//      of shaders::kBlockAlignment, writes its bytes to the GPU buffer and
//      returns the range in `slot`. Full when the block does not fit in
//      what is left; nothing is written and nothing is placed. Ranges are
//      never reused or moved: an offset stays valid for the buffer's life.
//   3. update compares the instance's block with the bytes the range holds
//      and writes it to the GPU buffer only when they differ; `wrote` says
//      which happened. SlotInvalid when `slot` is not a range add returned
//      from this buffer or the instance's block has another size; nothing
//      is written.
//   4. reference(slot) is the MaterialReference of the range: the view's
//      buffer index and the range's offset. It is the same value for every
//      draw of that instance, and add and update are the only calls that
//      write the buffer.
//   5. A write lands in the bytes a frame still in flight may read. Every
//      block of this slice is written before the first frame that draws
//      it. Changing an instance that frames in flight are drawing is not
//      supported here: such a frame reads either value.
//   6. A MaterialBuffer is movable and not copyable. The device must
//      outlive it.
//
// A material's parameter block is filled when its values change, then
// reused by every draw, not filled per draw. Every block is a range of one
// buffer, not a buffer of its own, which is what lets a draw name it by
// index and offset. Nothing updates blocks in the background: update is
// called by whoever changed the instance. No material of the game uses a
// virtual texture or a parameter shared across materials.
//
// Read guidance: R.1 and C.21 (the Refs own the buffer and view; copy
// deleted, move defaulted), E.27, I.1; GDSA.9 (blocks of differing size in
// one pool reached by offset; overflow fails by name), GPU.9 (one
// allocation, ranges inside it), GDSA.12 (a direct index and offset).
namespace bw::materials {

struct MaterialSlot {
  std::uint32_t offset{};
  std::uint32_t size{};
};

enum class MaterialBufferError : std::uint8_t {
  None,
  CapacityInvalid,
  BufferFailed,
  ViewFailed,
  Full,
  SlotInvalid,
  // The device refused the lock of a range add or update was writing.
  WriteFailed,
};

struct MaterialBufferFailure {
  MaterialBufferError error{MaterialBufferError::None};
  // Set for BufferFailed and ViewFailed, with the device's detail text.
  rhi::ResourceError resource{rhi::ResourceError::None};
  std::string detail{};
  [[nodiscard]] bool ok() const noexcept { return error == MaterialBufferError::None; }
};

class MaterialBuffer {
public:
  MaterialBuffer() = default;
  MaterialBuffer(const MaterialBuffer&) = delete;
  MaterialBuffer& operator=(const MaterialBuffer&) = delete;
  MaterialBuffer(MaterialBuffer&&) noexcept = default;
  MaterialBuffer& operator=(MaterialBuffer&&) noexcept = default;
  ~MaterialBuffer() = default;

  // Preconditions of the three: the buffer was created; the instance was
  // made from a material.
  [[nodiscard]] MaterialBufferFailure add(rhi::Device& device, const MaterialInstance& instance,
                                          MaterialSlot& slot);
  [[nodiscard]] MaterialBufferFailure update(rhi::Device& device, MaterialSlot slot,
                                             const MaterialInstance& instance, bool& wrote);
  [[nodiscard]] MaterialReference reference(MaterialSlot slot) const noexcept;

  [[nodiscard]] std::uint32_t capacity_bytes() const noexcept {
    return static_cast<std::uint32_t>(bytes_.size());
  }
  [[nodiscard]] std::uint32_t used_bytes() const noexcept { return used_; }

private:
  friend MaterialBufferFailure create_material_buffer(rhi::Device&, std::uint32_t, std::string_view,
                                                      MaterialBuffer&);
  rhi::Ref<rhi::Buffer> buffer_{};
  rhi::Ref<rhi::ShaderResourceView> view_{};
  // What the GPU buffer holds, so update can compare without reading it
  // back.
  std::vector<std::byte> bytes_{};
  std::uint32_t used_{};
  // Every range add returned, in offset order, so update can tell a range
  // it issued from any other.
  std::vector<MaterialSlot> slots_{};
};

[[nodiscard]] MaterialBufferFailure create_material_buffer(rhi::Device& device,
                                                           std::uint32_t capacity_bytes,
                                                           std::string_view debug_name,
                                                           MaterialBuffer& out);

} // namespace bw::materials

#endif
