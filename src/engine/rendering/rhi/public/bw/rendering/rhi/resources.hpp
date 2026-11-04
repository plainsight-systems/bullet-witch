#ifndef BW_RENDERING_RHI_RESOURCES_HPP
#define BW_RENDERING_RHI_RESOURCES_HPP
#include "bw/rendering/rhi/access.hpp"
#include "bw/rendering/rhi/build.hpp"
#include "bw/rendering/rhi/descriptor_allocator.hpp"
#include "bw/rendering/rhi/format.hpp"
#include "bw/rendering/rhi/lifetime.hpp"
#include "bw/rendering/rhi/result.hpp"
#include "bw/rendering/shaders/parameter_block.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <utility>

// Responsibility: buffers, textures and the views that place them in the
// typed descriptor arrays (docs/architecture/rhi.md, invariant 2: every
// resource is created from an explicit descriptor that names its usage, its
// initial state and its debug name; a descriptor the backend cannot honor is
// a named error at creation). The device creates them (device.hpp); the
// backend implements them (backend.hpp); the renderer holds Refs.
//
// Contract.
//   Descriptors: every field is meaningful; validate_* names the first
//   violation. Usage None, initial access Unknown or an empty debug name are
//   errors, not defaults. The debug name is copied into the resource at
//   creation (DebugName), so the caller's view may die after the call; with
//   BW_RHI_DEBUG 0 the copy holds nothing and description().debug_name is
//   empty.
//   Viewable: carries the tracked access state of invariant 3. It is set at
//   creation to initial_access on Graphics, and afterwards only by the
//   execution of a declared transition (backend or validation layer); the
//   renderer reads it, never writes it.
//   Views: a ShaderResourceView or UnorderedAccessView holds a Ref to its
//   resource (the resource outlives the view) and owns one descriptor slot
//   in the array its dimension names; the slot is written at creation and
//   returned at destruction: the view's destructor frees it in the pools the
//   device named (backend.hpp, BackendOptions::slot_pools), and the device
//   destroys views only while holding its mutex, which serializes the pools
//   (descriptor_allocator.hpp). The index the shader uses is slot().index;
//   the typed accessors narrow it to the shader system's index types.
//   Initial data: a buffer or texture may be created with its contents; the
//   data is copied during creation and the resource is in initial_access when
//   creation returns. Subresource order is mip-major within a slice: index =
//   slice * mip_levels + mip.
//   Locks: only a buffer with BufferUsage::Upload may be locked; the lock is
//   a persistent host mapping, write-only, unlocked once per lock, and reads
//   from it are not defined.
//
// Core guidelines read: C.35 and C.67 (through Resource), I.1 and E.27
// (descriptors are explicit values; failure is a named code), C.32 (a view's
// Ref is the owning pointer; Viewable* in a transition is an observer),
// R.14 and SL.str.2 (spans and views for borrowed bytes and names).
// Performance guidelines read: CDSA.32 was not applicable; CACHE.2 (block
// bytes stated so sizes are counted before allocation), MEM.5 (usage flags
// are what the backend's pool selection reads), GDSA.12 (the view's slot is
// a direct index the GPU reads).
namespace bw::rhi {

// A copied, bounded debug name. Empty in shipping builds.
inline constexpr std::size_t kDebugNameCapacity = 63;
class DebugName {
public:
  DebugName() = default;
  // Copies at most kDebugNameCapacity bytes; a longer name is truncated.
  explicit DebugName(std::string_view name) noexcept;
  [[nodiscard]] std::string_view view() const noexcept;

private:
#if BW_RHI_DEBUG
  std::array<char, kDebugNameCapacity + 1> text_{};
  std::uint8_t length_{};
#endif
};

// A buffer the CPU writes has Upload; a buffer only the GPU writes does not.
// CopyDestination is explicit so the initial state and the pool choice are
// visible. Per-frame transient buffers come from the frame ring
// (command_list.hpp), and every shader-visible buffer is reached as a byte
// address buffer through the buffer array, so neither has a usage flag.
enum class BufferUsage : std::uint16_t {
  None = 0,
  Vertex = 1U << 0U,
  Index = 1U << 1U,
  Uniform = 1U << 2U,
  ShaderResource = 1U << 3U,
  UnorderedAccess = 1U << 4U,
  IndirectArguments = 1U << 5U,
  CopySource = 1U << 6U,
  CopyDestination = 1U << 7U,
  // CPU-writable, persistently mapped through lock_buffer. Excludes
  // UnorderedAccess.
  Upload = 1U << 8U,
};
[[nodiscard]] constexpr BufferUsage operator|(BufferUsage a, BufferUsage b) noexcept {
  // A flag combination is not an enumerator.
  // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange)
  return static_cast<BufferUsage>(static_cast<std::uint16_t>(a) | static_cast<std::uint16_t>(b));
}
[[nodiscard]] constexpr BufferUsage operator&(BufferUsage a, BufferUsage b) noexcept {
  return static_cast<BufferUsage>(static_cast<std::uint16_t>(a) & static_cast<std::uint16_t>(b));
}
[[nodiscard]] constexpr bool has_usage(BufferUsage value, BufferUsage bit) noexcept {
  return (value & bit) == bit;
}

struct BufferDescription {
  std::uint64_t size{};
  // Element stride for Vertex and Index buffers (2 or 4 for Index); 0 for
  // byte-addressed and uniform buffers.
  std::uint32_t stride{};
  BufferUsage usage{BufferUsage::None};
  Access initial_access{Access::Unknown};
  std::string_view debug_name{};
};

// There is no cube array dimension: no descriptor array of the root layout
// can hold one.
enum class TextureDimension : std::uint8_t {
  Texture2D,
  Texture2DArray,
  Texture3D,
  TextureCube,
};

// sRGB is a property of the format, not a usage. No usage marks CPU access:
// readback goes through a staging buffer (synchronization.hpp), and textures
// upload through initial data or a copy.
enum class TextureUsage : std::uint8_t {
  None = 0,
  ShaderResource = 1U << 0U,
  RenderTarget = 1U << 1U,
  DepthStencil = 1U << 2U,
  UnorderedAccess = 1U << 3U,
  CopySource = 1U << 4U,
  CopyDestination = 1U << 5U,
  // The mark of a viewport's back buffer (viewport.hpp, invariant 2): set by
  // the viewport's own texture, never by a caller; validate_texture rejects
  // it with BackBufferUsageReserved.
  BackBuffer = 1U << 6U,
};
[[nodiscard]] constexpr TextureUsage operator|(TextureUsage a, TextureUsage b) noexcept {
  // A flag combination is not an enumerator.
  // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange)
  return static_cast<TextureUsage>(static_cast<std::uint16_t>(a) | static_cast<std::uint16_t>(b));
}
[[nodiscard]] constexpr TextureUsage operator&(TextureUsage a, TextureUsage b) noexcept {
  return static_cast<TextureUsage>(static_cast<std::uint16_t>(a) & static_cast<std::uint16_t>(b));
}
[[nodiscard]] constexpr bool has_usage(TextureUsage value, TextureUsage bit) noexcept {
  return (value & bit) == bit;
}

struct Extent3 {
  std::uint32_t width{};
  std::uint32_t height{};
  std::uint32_t depth{1};
  [[nodiscard]] friend constexpr bool operator==(Extent3, Extent3) noexcept = default;
};

// The clear value a render target or depth target is created with; a render
// pass whose clear differs is a programming error on Direct3D 12 and is
// reported by the validation layer.
struct ClearValue {
  std::array<float, 4> color{};
  float depth{1.0F};
  std::uint8_t stencil{};
};

struct TextureDescription {
  Extent3 extent{};
  std::uint16_t array_size{1};
  std::uint8_t mip_levels{1};
  std::uint8_t sample_count{1};
  TextureDimension dimension{TextureDimension::Texture2D};
  Format format{Format::Unknown};
  TextureUsage usage{TextureUsage::None};
  Access initial_access{Access::Unknown};
  ClearValue clear_value{};
  std::string_view debug_name{};
};

// One subresource's bytes for initial data: rows of row_pitch bytes,
// slices of slice_pitch bytes, tightly or loosely packed. row_pitch is at
// least row_bytes(format, mip width); slice_pitch is read only for a 3D
// mip with depth above 1 and is then at least slice_bytes(format, mip
// extent); otherwise it is ignored and may be 0. bytes holds at least the
// bytes the pitches address (validate_initial_data).
struct SubresourceData {
  std::span<const std::byte> bytes{};
  std::uint32_t row_pitch{};
  std::uint32_t slice_pitch{};
};

// Named creation failures. The first in this order that applies is
// returned.
enum class ResourceError : std::uint8_t {
  None,
  ZeroSize,
  NoUsage,
  // Upload with UnorderedAccess; DepthStencil with RenderTarget; a usage the
  // format's capabilities do not include.
  IncompatibleUsage,
  UnknownInitialAccess,
  // The initial access is not one the usage allows (a RenderTarget state on
  // a buffer, UnorderedAccess without the usage).
  InitialAccessNotAllowed,
  MissingDebugName,
  // Stride 0 for Vertex or Index, Index stride not 2 or 4.
  InvalidStride,
  UnsupportedFormat,
  ExtentOutOfRange,
  MipCountOutOfRange,
  ArraySizeOutOfRange,
  SampleCountUnsupported,
  // Initial data that does not match the subresource count or sizes.
  InitialDataMismatch,
  // The backend's pool or the API could not provide the memory.
  OutOfMemory,
  // A null Ref passed to a view creation.
  NullResource,
  // A view range outside the resource, a view format the resource cannot
  // take, or a view kind the resource's usage does not include.
  ViewOutOfRange,
  ViewFormatIncompatible,
  ViewUsageMissing,
  // No free slot in the descriptor pool the view needs; checked after the
  // view description, so a bad description never consumes a slot.
  DescriptorPoolExhausted,
  // The API rejected the request for a reason outside the checks above.
  BackendRejected,
  // TextureUsage::BackBuffer in a caller's description (viewport.hpp).
  BackBufferUsageReserved,
};

// The limits the texture validation checks against; from the capability
// report (device.hpp).
struct TextureLimits {
  std::uint32_t max_dimension_2d{};
  std::uint32_t max_dimension_3d{};
  std::uint32_t max_dimension_cube{};
  std::uint16_t max_array_size{};
  std::uint8_t max_sample_count{};
};

// Pure validation of a descriptor against its own rules and the limits;
// the device calls these before the backend and returns the same codes.
// validate_texture checks that the format is known and supported, and checks
// the extents against the maximum dimensions, the depth, the array size, the
// samples and the mips; a usage with BackBuffer is BackBufferUsageReserved,
// checked first. A buffer of size 0 is ZeroSize.
[[nodiscard]] ResourceError validate_buffer(const BufferDescription& description) noexcept;
// Initial data checks (InitialDataMismatch or None): a buffer's data is
// empty or exactly description.size bytes; a texture's data is empty or
// one SubresourceData per subresource in subresource_index order, each
// meeting the pitch rule above SubresourceData.
[[nodiscard]] ResourceError validate_buffer_initial_data(const BufferDescription& description,
                                                         std::span<const std::byte> data) noexcept;
[[nodiscard]] ResourceError validate_initial_data(const TextureDescription& description,
                                                  std::span<const SubresourceData> data) noexcept;
[[nodiscard]] ResourceError validate_texture(const TextureDescription& description,
                                             const TextureLimits& limits,
                                             FormatCapability format_capability) noexcept;
// Subresources of a texture: array_size * mip_levels (6 * array_size for a
// cube), and the index of one.
[[nodiscard]] constexpr std::uint32_t subresource_count(const TextureDescription& d) noexcept {
  const std::uint32_t slices =
      d.dimension == TextureDimension::TextureCube ? 6U * d.array_size : d.array_size;
  return slices * d.mip_levels;
}
[[nodiscard]] constexpr std::uint32_t
subresource_index(const TextureDescription& d, std::uint16_t slice, std::uint8_t mip) noexcept {
  return static_cast<std::uint32_t>(slice) * d.mip_levels + mip;
}

// The common base of buffer and texture; holds the tracked access and the
// debug name.
class Viewable : public Resource {
public:
  [[nodiscard]] TrackedAccess tracked_access() const noexcept { return tracked_; }
  // Written by transition execution only (backend.hpp, Context; the
  // validation layer).
  void set_tracked_access(TrackedAccess access) noexcept { tracked_ = access; }
  [[nodiscard]] std::string_view debug_name() const noexcept { return name_.view(); }
  [[nodiscard]] bool is_texture() const noexcept { return is_texture_; }

protected:
  Viewable(DeferredDeleteQueue& queue, Access initial_access, std::string_view name,
           bool is_texture) noexcept
      : Resource(queue), tracked_{initial_access, Pipeline::Graphics}, name_(name),
        is_texture_(is_texture) {}

private:
  TrackedAccess tracked_;
  DebugName name_;
  bool is_texture_;
};

// The constructor is protected: only backends create buffers, and they
// derive from Buffer.
class Buffer : public Viewable {
public:
  // description().debug_name views this object's copy of the name.
  [[nodiscard]] const BufferDescription& description() const noexcept { return description_; }

protected:
  Buffer(DeferredDeleteQueue& queue, const BufferDescription& description) noexcept;

private:
  BufferDescription description_;
};

// As for Buffer, only backends create textures. A texture has no bindless
// index of its own: every shader-visible texture has an explicit view and
// index.
class Texture : public Viewable {
public:
  [[nodiscard]] const TextureDescription& description() const noexcept { return description_; }

protected:
  Texture(DeferredDeleteQueue& queue, const TextureDescription& description) noexcept;

private:
  TextureDescription description_;
};

enum class TexturePlane : std::uint8_t {
  Primary,
  Depth,
  Stencil,
};

// Buffer and texture view descriptions, used for both shader resource and
// unordered access views. Every buffer view is raw (placement() Buffer is
// ByteAddressBuffer); a texture view names its format, which decides sRGB.
struct BufferViewDescription {
  std::uint64_t offset{};
  // 0 means to the end of the buffer.
  std::uint64_t size{};
};
struct TextureViewDescription {
  // Unknown means the texture's format.
  Format format{Format::Unknown};
  TextureDimension dimension{TextureDimension::Texture2D};
  TexturePlane plane{TexturePlane::Primary};
  std::uint8_t first_mip{};
  // 0 means every mip from first_mip. A UAV covers exactly one mip.
  std::uint8_t mip_count{};
  std::uint16_t first_slice{};
  // 0 means every slice from first_slice.
  std::uint16_t slice_count{};
};

// Pure checks the device runs before the backend writes a descriptor: a
// buffer view's offset, bounds and usage flags, and a texture view's
// dimension compatibility, mip and array ranges, plane and format, and that
// a UAV names one mip of a non-multisampled texture.
[[nodiscard]] ResourceError validate_buffer_view(const BufferDescription& buffer,
                                                 const BufferViewDescription& view,
                                                 bool unordered_access) noexcept;
[[nodiscard]] ResourceError validate_texture_view(const TextureDescription& texture,
                                                  const TextureViewDescription& view,
                                                  bool unordered_access) noexcept;
// The descriptor array a view of this dimension and kind lands in.
[[nodiscard]] constexpr shaders::DescriptorArray array_for(TextureDimension dimension,
                                                           bool unordered_access) noexcept {
  if (unordered_access) {
    return shaders::DescriptorArray::StorageTexture2D;
  }
  switch (dimension) {
  case TextureDimension::Texture2D:
    return shaders::DescriptorArray::Texture2D;
  case TextureDimension::Texture2DArray:
    return shaders::DescriptorArray::Texture2DArray;
  case TextureDimension::Texture3D:
    return shaders::DescriptorArray::Texture3D;
  case TextureDimension::TextureCube:
    return shaders::DescriptorArray::TextureCube;
  }
  return shaders::DescriptorArray::Texture2D;
}

// A view holds a Ref to its resource. Its bindless handle is the slot,
// assigned by the device's allocator at creation.
class View : public Resource {
public:
  [[nodiscard]] const Ref<Viewable>& resource() const noexcept { return resource_; }
  [[nodiscard]] DescriptorSlot slot() const noexcept { return slot_; }

protected:
  // pools: the device's allocators the slot came from; it outlives the view.
  View(DeferredDeleteQueue& queue, Ref<Viewable> resource, DescriptorSlot slot,
       DescriptorAllocators& pools) noexcept
      : Resource(queue), resource_(std::move(resource)), slot_(slot), pools_(&pools) {}
  // Returns the slot. Runs inside the delete queue's destroy, under the
  // device mutex (the contract above).
  ~View() override {
    if (slot_.valid()) {
      pools_->free(slot_);
    }
  }

private:
  Ref<Viewable> resource_;
  DescriptorSlot slot_;
  DescriptorAllocators* pools_;
};

class ShaderResourceView : public View {
public:
  // Precondition: the resource is a texture.
  [[nodiscard]] shaders::TextureIndex texture_index() const noexcept { return {slot().index}; }
  // Precondition: the resource is a buffer.
  [[nodiscard]] shaders::BufferIndex buffer_index() const noexcept { return {slot().index}; }

protected:
  using View::View;
};

class UnorderedAccessView : public View {
public:
  [[nodiscard]] shaders::StorageTextureIndex storage_texture_index() const noexcept {
    return {slot().index};
  }
  [[nodiscard]] shaders::StorageBufferIndex storage_buffer_index() const noexcept {
    return {slot().index};
  }

protected:
  using View::View;
};

using BufferResult = Result<Ref<Buffer>, ResourceError>;
using TextureResult = Result<Ref<Texture>, ResourceError>;
using ShaderResourceViewResult = Result<Ref<ShaderResourceView>, ResourceError>;
using UnorderedAccessViewResult = Result<Ref<UnorderedAccessView>, ResourceError>;

// A byte range of a buffer, and a buffer-to-buffer copy.
struct BufferRange {
  std::uint64_t offset{};
  std::uint64_t size{};
};
struct BufferCopy {
  std::uint64_t source_offset{};
  std::uint64_t destination_offset{};
  std::uint64_t size{};
};

// A lock is the write-only mapping of an Upload buffer. Reads go through a
// staging buffer and a fence (synchronization.hpp). An Upload buffer is
// persistently host-visible, so a lock needs no shadow copy.
enum class LockError : std::uint8_t {
  None,
  // The buffer was not created with BufferUsage::Upload.
  NotUploadBuffer,
  RangeOutOfBounds,
  AlreadyLocked,
  NotLocked,
};
using LockResult = Result<std::span<std::byte>, LockError>;

// The pure part of Device::lock_buffer, in this order: NotUploadBuffer when
// the usage lacks Upload; RangeOutOfBounds when range.size is 0 or
// range.offset + range.size exceeds the buffer's size. AlreadyLocked is the
// device's.
[[nodiscard]] constexpr LockError validate_lock(const BufferDescription& buffer,
                                                BufferRange range) noexcept {
  if (!has_usage(buffer.usage, BufferUsage::Upload)) {
    return LockError::NotUploadBuffer;
  }
  if (range.size == 0U || range.offset > buffer.size || range.size > buffer.size - range.offset) {
    return LockError::RangeOutOfBounds;
  }
  return LockError::None;
}

} // namespace bw::rhi
#endif
