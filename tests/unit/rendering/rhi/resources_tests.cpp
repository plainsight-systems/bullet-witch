// Unit test: public contracts of bw/rendering/rhi/resources.hpp, one
// scenario per CTest entry. Each scenario names the header declaration it asserts.
#include "bw/rendering/rhi/resources.hpp"

#include "bw/rendering/rhi/build.hpp"
#include "bw/rendering/rhi/descriptor_allocator.hpp"
#include "bw/rendering/rhi/lifetime.hpp"
#include "bw/testing/harness.hpp"
#include "test_objects.hpp"
#include "unwrap.hpp"

#include <array>
#include <cstddef>
#include <span>
#include <string>
#include <string_view>

namespace {
using bw::rhi::Access;
using bw::rhi::BufferDescription;
using bw::rhi::BufferUsage;
using bw::rhi::BufferViewDescription;
using bw::rhi::Format;
using bw::rhi::FormatCapability;
using bw::rhi::ResourceError;
using bw::rhi::TextureDescription;
using bw::rhi::TextureDimension;
using bw::rhi::TextureLimits;
using bw::rhi::TexturePlane;
using bw::rhi::TextureUsage;
using bw::rhi::TextureViewDescription;
using bw::testing::require;
using Scenario = bw::testing::Scenario<void>;

constexpr TextureLimits kLimits{4096, 2048, 4096, 256, 8};
constexpr FormatCapability kColor = FormatCapability::Texture | FormatCapability::RenderTarget |
                                    FormatCapability::UnorderedAccess |
                                    FormatCapability::Blendable | FormatCapability::Multisample;

BufferDescription valid_buffer() {
  return BufferDescription{.size = 64,
                           .stride = 4,
                           .usage = BufferUsage::Vertex | BufferUsage::CopyDestination,
                           .initial_access = Access::CopyDestination,
                           .debug_name = "Vertices"};
}

TextureDescription valid_texture() {
  TextureDescription d{};
  d.extent = bw::rhi::Extent3{64, 32, 1};
  d.format = Format::R8G8B8A8Unorm;
  d.usage = TextureUsage::ShaderResource | TextureUsage::RenderTarget;
  d.initial_access = Access::RenderTarget;
  d.debug_name = "Target";
  return d;
}

ResourceError check(const TextureDescription& d, FormatCapability capability = kColor) {
  return bw::rhi::validate_texture(d, kLimits, capability);
}

// (resources.hpp, validate_texture, ResourceError::BackBufferUsageReserved,
// TextureUsage::BackBuffer): a caller's description with the back buffer
// mark is rejected by that code, checked before every other rule; the same
// description without the mark passes.
void back_buffer_usage_reserved() {
  TextureDescription d = valid_texture();
  d.usage = d.usage | TextureUsage::BackBuffer;
  require(check(d) == ResourceError::BackBufferUsageReserved, "the mark on a valid description");
  d.extent.width = 0;
  d.debug_name = {};
  d.format = Format::Unknown;
  require(check(d, FormatCapability::None) == ResourceError::BackBufferUsageReserved,
          "BackBufferUsageReserved before ZeroSize, MissingDebugName and UnsupportedFormat");
  require(check(valid_texture()) == ResourceError::None, "without the mark");
}

// (resources.hpp, TextureUsage::Memoryless, ResourceError::MemorylessUnsupported):
// legal with RenderTarget alone where the limits admit it, IncompatibleUsage
// with any usage beside RenderTarget or DepthStencil, and
// MemorylessUnsupported where TextureLimits::memoryless is false.
void memoryless_usage_rules() {
  TextureLimits memoryless_limits = kLimits;
  memoryless_limits.memoryless = true;
  TextureDescription d = valid_texture();
  d.usage = TextureUsage::Memoryless | TextureUsage::RenderTarget;
  require(bw::rhi::validate_texture(d, memoryless_limits, kColor) == ResourceError::None,
          "Memoryless with RenderTarget alone");
  require(check(d) == ResourceError::MemorylessUnsupported,
          "MemorylessUnsupported where limits.memoryless is false");
  d.usage = TextureUsage::Memoryless | TextureUsage::ShaderResource;
  require(check(d) == ResourceError::IncompatibleUsage, "Memoryless with ShaderResource");
}

// (resources.hpp, validate_buffer) and (resources.hpp, ResourceError): usage
// None, initial access Unknown and an empty debug name are errors; each rule
// has its code; the first in ResourceError's order that applies is returned.
void buffer_validation_named_in_order() {
  require(bw::rhi::validate_buffer(valid_buffer()) == ResourceError::None, "valid buffer");
  BufferDescription d = valid_buffer();
  d.size = 0;
  d.usage = BufferUsage::None;
  require(bw::rhi::validate_buffer(d) == ResourceError::ZeroSize, "ZeroSize before NoUsage");
  d = valid_buffer();
  d.usage = BufferUsage::None;
  d.debug_name = {};
  require(bw::rhi::validate_buffer(d) == ResourceError::NoUsage, "NoUsage before MissingDebugName");
  d = valid_buffer();
  d.usage = BufferUsage::Upload | BufferUsage::UnorderedAccess;
  d.initial_access = Access::Unknown;
  require(bw::rhi::validate_buffer(d) == ResourceError::IncompatibleUsage,
          "Upload with UnorderedAccess, before UnknownInitialAccess");
  d = valid_buffer();
  d.initial_access = Access::Unknown;
  require(bw::rhi::validate_buffer(d) == ResourceError::UnknownInitialAccess,
          "UnknownInitialAccess");
  d = valid_buffer();
  d.initial_access = Access::RenderTarget;
  require(bw::rhi::validate_buffer(d) == ResourceError::InitialAccessNotAllowed,
          "a render target state on a buffer");
  d = valid_buffer();
  d.initial_access = Access::UnorderedAccessCompute;
  require(bw::rhi::validate_buffer(d) == ResourceError::InitialAccessNotAllowed,
          "unordered access without the usage");
  d = valid_buffer();
  d.debug_name = {};
  d.stride = 0;
  require(bw::rhi::validate_buffer(d) == ResourceError::MissingDebugName,
          "MissingDebugName before InvalidStride");
  d = valid_buffer();
  d.stride = 0;
  require(bw::rhi::validate_buffer(d) == ResourceError::InvalidStride, "a vertex stride of 0");
  d = valid_buffer();
  d.usage = BufferUsage::Index | BufferUsage::CopyDestination;
  d.stride = 3;
  require(bw::rhi::validate_buffer(d) == ResourceError::InvalidStride, "an index stride of 3");
  d.stride = 2;
  require(bw::rhi::validate_buffer(d) == ResourceError::None, "an index stride of 2");
}

// (resources.hpp, validate_texture) and (resources.hpp, ResourceError): the
// texture rules against the limits and the format's capability, the first in
// ResourceError's order returned.
void texture_validation_named_in_order() {
  require(check(valid_texture()) == ResourceError::None, "valid texture");
  TextureDescription d = valid_texture();
  d.extent.width = 0;
  d.usage = TextureUsage::None;
  require(check(d) == ResourceError::ZeroSize, "ZeroSize before NoUsage");
  d = valid_texture();
  d.usage = TextureUsage::None;
  require(check(d) == ResourceError::NoUsage, "NoUsage");
  d = valid_texture();
  d.usage = TextureUsage::RenderTarget | TextureUsage::DepthStencil;
  d.initial_access = Access::Unknown;
  require(check(d) == ResourceError::IncompatibleUsage,
          "DepthStencil with RenderTarget, before UnknownInitialAccess");
  require(check(valid_texture(), FormatCapability::Texture) == ResourceError::IncompatibleUsage,
          "a usage the format's capabilities do not include");
  d = valid_texture();
  d.initial_access = Access::Unknown;
  require(check(d) == ResourceError::UnknownInitialAccess, "UnknownInitialAccess");
  d = valid_texture();
  d.initial_access = Access::CopyDestination;
  require(check(d) == ResourceError::InitialAccessNotAllowed, "a state the usage does not allow");
  d = valid_texture();
  d.debug_name = {};
  d.format = Format::Unknown;
  require(check(d, FormatCapability::None) == ResourceError::MissingDebugName,
          "MissingDebugName before UnsupportedFormat");
  d = valid_texture();
  d.format = Format::Unknown;
  require(check(d, FormatCapability::None) == ResourceError::UnsupportedFormat, "Unknown format");
  require(check(valid_texture(), FormatCapability::None) == ResourceError::UnsupportedFormat,
          "a format the device does not support");
  d = valid_texture();
  d.extent.width = 8192;
  d.mip_levels = 0;
  require(check(d) == ResourceError::ExtentOutOfRange, "ExtentOutOfRange before mips");
  d = valid_texture();
  d.dimension = TextureDimension::TextureCube;
  require(check(d) == ResourceError::ExtentOutOfRange, "a cube is square");
  d = valid_texture();
  d.extent.depth = 2;
  require(check(d) == ResourceError::ExtentOutOfRange, "a 2D texture has depth 1");
  d = valid_texture();
  d.mip_levels = 7;
  require(check(d) == ResourceError::None, "64x32 has a seven-mip chain");
  d.mip_levels = 8;
  require(check(d) == ResourceError::MipCountOutOfRange, "an eighth mip is out of range");
  d.mip_levels = 0;
  require(check(d) == ResourceError::MipCountOutOfRange, "zero mips");
  d = valid_texture();
  d.sample_count = 4;
  d.mip_levels = 2;
  require(check(d) == ResourceError::MipCountOutOfRange, "a multisampled texture has one mip");
  d = valid_texture();
  d.array_size = 2;
  require(check(d) == ResourceError::ArraySizeOutOfRange, "a 2D texture has one slice");
  d.dimension = TextureDimension::Texture2DArray;
  require(check(d) == ResourceError::None, "a 2D array takes slices");
  d.array_size = 257;
  require(check(d) == ResourceError::ArraySizeOutOfRange, "beyond the array limit");
  d = valid_texture();
  d.sample_count = 3;
  require(check(d) == ResourceError::SampleCountUnsupported, "3 samples");
  d.sample_count = 16;
  require(check(d) == ResourceError::SampleCountUnsupported, "beyond the sample limit");
  d.sample_count = 4;
  require(check(d) == ResourceError::None, "4 samples on a 2D render target");
  require(check(d, FormatCapability::Texture | FormatCapability::RenderTarget) ==
              ResourceError::SampleCountUnsupported,
          "multisampling needs the format capability");
}

// (resources.hpp, validate_buffer_view) and
// (resources.hpp, validate_texture_view): a view range outside the resource, a format or
// plane the resource cannot take, a kind its usage does not include; each
// named, checked in ResourceError's order.
void view_validation_named() {
  BufferDescription buffer = valid_buffer();
  buffer.usage = BufferUsage::ShaderResource;
  buffer.initial_access = Access::ShaderResourcePixel;
  require(bw::rhi::validate_buffer_view(buffer, BufferViewDescription{0, 0}, false) ==
              ResourceError::None,
          "the whole buffer");
  require(bw::rhi::validate_buffer_view(buffer, BufferViewDescription{64, 0}, false) ==
              ResourceError::ViewOutOfRange,
          "an offset at the end");
  require(bw::rhi::validate_buffer_view(buffer, BufferViewDescription{32, 48}, false) ==
              ResourceError::ViewOutOfRange,
          "a range past the end");
  require(bw::rhi::validate_buffer_view(buffer, BufferViewDescription{0, 0}, true) ==
              ResourceError::ViewUsageMissing,
          "a storage view without UnorderedAccess");
  require(bw::rhi::validate_buffer_view(buffer, BufferViewDescription{64, 0}, true) ==
              ResourceError::ViewOutOfRange,
          "range before usage");

  TextureDescription texture = valid_texture();
  texture.mip_levels = 3;
  texture.usage = TextureUsage::ShaderResource | TextureUsage::UnorderedAccess;
  texture.initial_access = Access::ShaderResourcePixel;
  TextureViewDescription view{};
  require(bw::rhi::validate_texture_view(texture, view, false) == ResourceError::None,
          "the whole texture");
  view.dimension = TextureDimension::Texture3D;
  require(bw::rhi::validate_texture_view(texture, view, false) == ResourceError::ViewOutOfRange,
          "a 3D view of a 2D texture");
  view = TextureViewDescription{};
  view.first_mip = 1;
  view.mip_count = 3;
  require(bw::rhi::validate_texture_view(texture, view, false) == ResourceError::ViewOutOfRange,
          "mips past the last");
  view = TextureViewDescription{};
  view.first_slice = 1;
  require(bw::rhi::validate_texture_view(texture, view, false) == ResourceError::ViewOutOfRange,
          "a slice past the last");
  view = TextureViewDescription{};
  require(bw::rhi::validate_texture_view(texture, view, true) == ResourceError::ViewOutOfRange,
          "a storage view covers one mip");
  view.mip_count = 1;
  require(bw::rhi::validate_texture_view(texture, view, true) == ResourceError::None,
          "a storage view of one mip");
  view = TextureViewDescription{};
  view.plane = TexturePlane::Depth;
  require(bw::rhi::validate_texture_view(texture, view, false) ==
              ResourceError::ViewFormatIncompatible,
          "a depth plane of a color texture");
  view = TextureViewDescription{};
  view.format = Format::R8G8B8A8UnormSrgb;
  require(bw::rhi::validate_texture_view(texture, view, false) == ResourceError::None,
          "the sRGB encoding of the texture's format");
  view.format = Format::R16Float;
  require(bw::rhi::validate_texture_view(texture, view, false) ==
              ResourceError::ViewFormatIncompatible,
          "an unrelated format");
  TextureDescription sampled_only = texture;
  sampled_only.usage = TextureUsage::ShaderResource;
  view = TextureViewDescription{};
  view.mip_count = 1;
  require(bw::rhi::validate_texture_view(sampled_only, view, true) ==
              ResourceError::ViewUsageMissing,
          "a storage view without UnorderedAccess");
  view.format = Format::R16Float;
  require(bw::rhi::validate_texture_view(sampled_only, view, true) ==
              ResourceError::ViewFormatIncompatible,
          "format before usage");
}

// (resources.hpp, subresource_count) and (resources.hpp, subresource_index):
// array_size * mip_levels subresources, six slices per cube, index = slice *
// mip_levels + mip.
void subresource_arithmetic() {
  TextureDescription d = valid_texture();
  d.dimension = TextureDimension::Texture2DArray;
  d.array_size = 4;
  d.mip_levels = 3;
  require(bw::rhi::subresource_count(d) == 12U, "four slices of three mips");
  require(bw::rhi::subresource_index(d, 0, 0) == 0U, "the first subresource");
  require(bw::rhi::subresource_index(d, 0, 2) == 2U, "mips of a slice are consecutive");
  require(bw::rhi::subresource_index(d, 2, 1) == 7U, "slice-major across slices");
  d.dimension = TextureDimension::TextureCube;
  d.array_size = 1;
  require(bw::rhi::subresource_count(d) == 18U, "a cube is six slices");
}

// (resources.hpp, DebugName) and (resources.hpp, Buffer::description): the
// debug name is copied into the resource, so the caller's storage may change
// after creation; at most kDebugNameCapacity bytes are kept; with
// BW_RHI_DEBUG 0 nothing is kept.
void debug_name_copied_and_bounded() {
  std::string caller = "Original";
  const bw::rhi::DebugName name{caller};
  caller.assign("Changed!");
  const std::string long_name(100, 'x');
  const bw::rhi::DebugName bounded{long_name};

  bw::rhi::DeferredDeleteQueue queue;
  std::string buffer_name = "Scratch";
  BufferDescription description = valid_buffer();
  description.debug_name = buffer_name;
  const bw::rhi::Ref<bw_rhi_test::TestBuffer> buffer =
      bw_rhi_test::make<bw_rhi_test::TestBuffer>(queue, description);
  buffer_name.assign("Mutated");
#if BW_RHI_DEBUG
  require(name.view() == "Original", "the name is a copy");
  require(bounded.view().size() == bw::rhi::kDebugNameCapacity, "a long name is truncated");
  require(buffer->debug_name() == "Scratch", "the resource holds its own copy");
  require(buffer->description().debug_name == "Scratch" &&
              buffer->description().debug_name.data() == buffer->debug_name().data(),
          "description().debug_name views the resource's copy");
#else
  require(name.view().empty() && bounded.view().empty(), "no name in shipping");
  require(buffer->description().debug_name.empty(), "no name in shipping");
#endif
}

// (resources.hpp, View): a view owns one descriptor slot and its
// destructor returns it to the pools it was created against;
// (descriptor_allocator.hpp, DescriptorIndexAllocator): a freed index is reused by a later
// allocate, lowest first. The slot is not returned while the view lives,
// and is free again once the queue destroys the view.
void view_returns_slot_on_destruction() {
  // The pools outlive the queue, whose destruction may still destroy a view.
  bw::rhi::DescriptorAllocators pools(
      bw::rhi::DescriptorCapacities{4, 1, bw::shaders::kStorageDescriptors});
  bw::rhi::DeferredDeleteQueue queue;
  bw_rhi_test::DestructionLog log;
  const bw::rhi::Ref<bw::rhi::Viewable> texture = bw_rhi_test::make<bw_rhi_test::TestTexture>(
      queue, bw_rhi_test::render_target_description("Viewed"));
  const auto pool = bw::rhi::DescriptorPool::Read;
  const bw::rhi::DescriptorSlot before =
      bw_rhi_test::unwrap(pools.allocate(bw::shaders::DescriptorArray::Texture2D), "slot 0");
  const bw::rhi::DescriptorSlot slot =
      bw_rhi_test::unwrap(pools.allocate(bw::shaders::DescriptorArray::Texture2D), "slot 1");
  require(before.index == 0U && slot.index == 1U, "two slots in use");
  bw::rhi::Ref<bw_rhi_test::TestShaderResourceView> view =
      bw_rhi_test::make<bw_rhi_test::TestShaderResourceView>(queue, texture, slot, pools, log, 1);
  require(pools.pool(pool).allocated() == 2U, "the live view holds its slot");

  view.reset();
  require(pools.pool(pool).allocated() == 2U, "an enqueued view still holds its slot");
  require(queue.destroy_completed(queue.current_frame()) == 1U && log.destroyed(1),
          "the queue destroys the view");
  require(pools.pool(pool).allocated() == 1U, "the destroyed view returned its slot");
  require(
      bw_rhi_test::unwrap(pools.allocate(bw::shaders::DescriptorArray::Texture2D), "a reused slot")
              .index == slot.index,
      "a later allocate returns the view's index");
}

// (resources.hpp, Viewable): the tracked access starts as initial_access on
// Graphics and is written by set_tracked_access; is_texture tells a texture
// from a buffer.
void viewable_initial_tracked_access_and_kind() {
  bw::rhi::DeferredDeleteQueue queue;
  BufferDescription description = valid_buffer();
  const bw::rhi::Ref<bw_rhi_test::TestBuffer> buffer =
      bw_rhi_test::make<bw_rhi_test::TestBuffer>(queue, description);
  const bw::rhi::Ref<bw_rhi_test::TestTexture> texture =
      bw_rhi_test::make<bw_rhi_test::TestTexture>(queue, valid_texture());
  require(buffer->tracked_access() ==
              (bw::rhi::TrackedAccess{Access::CopyDestination, bw::rhi::Pipeline::Graphics}),
          "a buffer starts in its initial access on Graphics");
  require(texture->tracked_access() ==
              (bw::rhi::TrackedAccess{Access::RenderTarget, bw::rhi::Pipeline::Graphics}),
          "a texture starts in its initial access on Graphics");
  require(!buffer->is_texture() && texture->is_texture(), "the kind");
  buffer->set_tracked_access(
      bw::rhi::TrackedAccess{Access::VertexOrIndexBuffer, bw::rhi::Pipeline::Graphics});
  require(buffer->tracked_access().access == Access::VertexOrIndexBuffer,
          "set_tracked_access writes the tracked access");
}

// (resources.hpp, array_for): a shader resource view lands in the read array
// of its dimension; every storage view in StorageTexture2D.
void array_for_by_dimension_and_kind() {
  using bw::shaders::DescriptorArray;
  require(bw::rhi::array_for(TextureDimension::Texture2D, false) == DescriptorArray::Texture2D,
          "2D");
  require(bw::rhi::array_for(TextureDimension::Texture2DArray, false) ==
              DescriptorArray::Texture2DArray,
          "2D array");
  require(bw::rhi::array_for(TextureDimension::Texture3D, false) == DescriptorArray::Texture3D,
          "3D");
  require(bw::rhi::array_for(TextureDimension::TextureCube, false) == DescriptorArray::TextureCube,
          "cube");
  for (const TextureDimension dimension :
       {TextureDimension::Texture2D, TextureDimension::Texture2DArray, TextureDimension::Texture3D,
        TextureDimension::TextureCube}) {
    require(bw::rhi::array_for(dimension, true) == DescriptorArray::StorageTexture2D,
            "storage views land in StorageTexture2D");
  }
}

// (resources.hpp, SubresourceData), (resources.hpp, validate_buffer_initial_data)
// and (resources.hpp, validate_initial_data): a buffer's data is empty or exactly
// its size; a texture's is empty or one entry per subresource with row_pitch at least the row
// bytes, slice_pitch read only for a 3D mip with depth above 1, and the bytes covering what the
// pitches address.
void initial_data_validation_named() {
  const BufferDescription buffer = valid_buffer();
  const std::array<std::byte, 64> exact{};
  const std::array<std::byte, 63> short_bytes{};
  const std::array<std::byte, 65> long_bytes{};
  require(bw::rhi::validate_buffer_initial_data(buffer, {}) == ResourceError::None, "no data");
  require(bw::rhi::validate_buffer_initial_data(buffer, exact) == ResourceError::None,
          "exactly the size");
  require(bw::rhi::validate_buffer_initial_data(buffer, short_bytes) ==
              ResourceError::InitialDataMismatch,
          "one byte short");
  require(bw::rhi::validate_buffer_initial_data(buffer, long_bytes) ==
              ResourceError::InitialDataMismatch,
          "one byte long");

  TextureDescription texture = valid_texture();
  texture.extent = bw::rhi::Extent3{4, 4, 1};
  texture.mip_levels = 2;
  const std::array<std::byte, 112> bytes{};
  const std::span<const std::byte> all{bytes};
  require(bw::rhi::validate_initial_data(texture, {}) == ResourceError::None, "no data");
  const std::array<bw::rhi::SubresourceData, 2> tight{
      bw::rhi::SubresourceData{all.first(64), 16, 0},
      bw::rhi::SubresourceData{all.first(16), 8, 0}};
  require(bw::rhi::validate_initial_data(texture, tight) == ResourceError::None,
          "tight rows, slice_pitch 0 on a 2D mip");
  require(bw::rhi::validate_initial_data(texture, std::span{tight}.first(1)) ==
              ResourceError::InitialDataMismatch,
          "one entry per subresource");
  std::array<bw::rhi::SubresourceData, 2> data = tight;
  data[0].row_pitch = 15;
  require(bw::rhi::validate_initial_data(texture, data) == ResourceError::InitialDataMismatch,
          "a row pitch below the row bytes");
  data = tight;
  data[0].bytes = all.first(63);
  require(bw::rhi::validate_initial_data(texture, data) == ResourceError::InitialDataMismatch,
          "bytes short of the rows");
  data = tight;
  data[0] = bw::rhi::SubresourceData{all.first(112), 32, 0};
  require(bw::rhi::validate_initial_data(texture, data) == ResourceError::None,
          "loose rows; the last row needs only its row bytes");

  TextureDescription volume = valid_texture();
  volume.dimension = TextureDimension::Texture3D;
  volume.extent = bw::rhi::Extent3{4, 4, 2};
  volume.usage = TextureUsage::ShaderResource;
  volume.initial_access = Access::ShaderResourcePixel;
  const std::array<std::byte, 128> volume_bytes{};
  const std::span<const std::byte> volume_all{volume_bytes};
  require(bw::rhi::validate_initial_data(volume, std::array{bw::rhi::SubresourceData{
                                                     volume_all, 16, 64}}) == ResourceError::None,
          "a volume mip with its slice pitch");
  require(bw::rhi::validate_initial_data(volume,
                                         std::array{bw::rhi::SubresourceData{volume_all, 16, 0}}) ==
              ResourceError::InitialDataMismatch,
          "a volume mip with depth above 1 reads slice_pitch");
  require(bw::rhi::validate_initial_data(
              volume, std::array{bw::rhi::SubresourceData{volume_all.first(127), 16, 64}}) ==
              ResourceError::InitialDataMismatch,
          "bytes short of the last slice");
  TextureDescription memoryless = valid_texture();
  memoryless.usage = TextureUsage::Memoryless | TextureUsage::RenderTarget;
  const std::array<std::byte, std::size_t{64} * 32 * 4> memoryless_bytes{};
  require(bw::rhi::validate_initial_data(
              memoryless, std::array{bw::rhi::SubresourceData{
                              std::span<const std::byte>{memoryless_bytes}, 64 * 4, 0}}) ==
              ResourceError::InitialDataMismatch,
          "data on a Memoryless texture");
}

// (resources.hpp, validate_lock): NotUploadBuffer first, then
// RangeOutOfBounds for an empty range or one past the end.
void lock_validation_named() {
  BufferDescription upload = valid_buffer();
  upload.usage = BufferUsage::Upload | BufferUsage::Vertex;
  require(bw::rhi::validate_lock(upload, bw::rhi::BufferRange{0, 64}) == bw::rhi::LockError::None,
          "the whole buffer");
  require(bw::rhi::validate_lock(upload, bw::rhi::BufferRange{16, 48}) == bw::rhi::LockError::None,
          "a range ending at the end");
  require(bw::rhi::validate_lock(upload, bw::rhi::BufferRange{0, 0}) ==
              bw::rhi::LockError::RangeOutOfBounds,
          "an empty range");
  require(bw::rhi::validate_lock(upload, bw::rhi::BufferRange{16, 49}) ==
              bw::rhi::LockError::RangeOutOfBounds,
          "a range past the end");
  require(bw::rhi::validate_lock(upload, bw::rhi::BufferRange{65, 1}) ==
              bw::rhi::LockError::RangeOutOfBounds,
          "an offset past the end");
  require(bw::rhi::validate_lock(valid_buffer(), bw::rhi::BufferRange{0, 0}) ==
              bw::rhi::LockError::NotUploadBuffer,
          "not an upload buffer, before the range");
}

constexpr std::array scenarios{
    Scenario{"buffer_validation_named_in_order", buffer_validation_named_in_order},
    Scenario{"texture_validation_named_in_order", texture_validation_named_in_order},
    Scenario{"view_validation_named", view_validation_named},
    Scenario{"subresource_arithmetic", subresource_arithmetic},
    Scenario{"debug_name_copied_and_bounded", debug_name_copied_and_bounded},
    Scenario{"view_returns_slot_on_destruction", view_returns_slot_on_destruction},
    Scenario{"viewable_initial_tracked_access_and_kind", viewable_initial_tracked_access_and_kind},
    Scenario{"array_for_by_dimension_and_kind", array_for_by_dimension_and_kind},
    Scenario{"initial_data_validation_named", initial_data_validation_named},
    Scenario{"lock_validation_named", lock_validation_named},
    Scenario{"back_buffer_usage_reserved", back_buffer_usage_reserved},
    Scenario{"memoryless_usage_rules", memoryless_usage_rules},
};
} // namespace

int main(int argc, char** argv) { return bw::testing::run_scenario(argc, argv, scenarios); }
