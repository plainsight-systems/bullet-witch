#include "bw/rendering/rhi/resources.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace bw::rhi {

#if BW_RHI_DEBUG
DebugName::DebugName(std::string_view name) noexcept {
  const std::size_t length = std::min(name.size(), kDebugNameCapacity);
  std::copy_n(name.begin(), length, text_.begin());
  text_[length] = '\0';
  length_ = static_cast<std::uint8_t>(length);
}
std::string_view DebugName::view() const noexcept { return {text_.data(), length_}; }
#else
DebugName::DebugName(std::string_view name) noexcept { static_cast<void>(name); }
std::string_view DebugName::view() const noexcept { return {}; }
#endif

namespace {

// The access states each usage permits as an initial state. One table for
// both checks of InitialAccessNotAllowed (resources.hpp, ResourceError): an
// initial access must be a legal access (access.hpp, is_valid_access) whose
// every bit is permitted by some usage the descriptor names.
[[nodiscard]] Access allowed_access(BufferUsage usage) noexcept {
  Access allowed = Access::Unknown;
  if (has_usage(usage, BufferUsage::Vertex) || has_usage(usage, BufferUsage::Index)) {
    allowed = allowed | Access::VertexOrIndexBuffer;
  }
  if (has_usage(usage, BufferUsage::Uniform) || has_usage(usage, BufferUsage::ShaderResource)) {
    allowed = allowed | kShaderResourceMask;
  }
  if (has_usage(usage, BufferUsage::UnorderedAccess)) {
    allowed = allowed | kUnorderedAccessMask;
  }
  if (has_usage(usage, BufferUsage::IndirectArguments)) {
    allowed = allowed | Access::IndirectArguments;
  }
  if (has_usage(usage, BufferUsage::CopySource)) {
    allowed = allowed | Access::CopySource;
  }
  if (has_usage(usage, BufferUsage::CopyDestination)) {
    allowed = allowed | Access::CopyDestination;
  }
  return allowed;
}

[[nodiscard]] Access allowed_access(TextureUsage usage) noexcept {
  Access allowed = Access::Unknown;
  if (has_usage(usage, TextureUsage::ShaderResource)) {
    allowed = allowed | kShaderResourceMask;
  }
  if (has_usage(usage, TextureUsage::RenderTarget)) {
    allowed = allowed | Access::RenderTarget;
  }
  if (has_usage(usage, TextureUsage::DepthStencil)) {
    allowed = allowed | Access::DepthStencilRead | Access::DepthStencilWrite;
  }
  if (has_usage(usage, TextureUsage::UnorderedAccess)) {
    allowed = allowed | kUnorderedAccessMask;
  }
  if (has_usage(usage, TextureUsage::CopySource)) {
    allowed = allowed | Access::CopySource;
  }
  if (has_usage(usage, TextureUsage::CopyDestination)) {
    allowed = allowed | Access::CopyDestination;
  }
  return allowed;
}

[[nodiscard]] bool initial_access_allowed(Access initial, Access allowed) noexcept {
  return is_valid_access(initial) && (initial & ~allowed) == Access::Unknown;
}

// A format the device can create a texture of: a real enumerator the
// capability report lists with any capability.
[[nodiscard]] bool format_supported(Format format, FormatCapability capability) noexcept {
  return format != Format::Unknown && format < Format::Count &&
         capability != FormatCapability::None;
}

// The texture usages that need a format capability.
[[nodiscard]] bool usage_within_capability(TextureUsage usage,
                                           FormatCapability capability) noexcept {
  const auto needs = [&](TextureUsage bit, FormatCapability required) {
    return !has_usage(usage, bit) || has_capability(capability, required);
  };
  return needs(TextureUsage::ShaderResource, FormatCapability::Texture) &&
         needs(TextureUsage::RenderTarget, FormatCapability::RenderTarget) &&
         needs(TextureUsage::DepthStencil, FormatCapability::DepthStencil) &&
         needs(TextureUsage::UnorderedAccess, FormatCapability::UnorderedAccess);
}

[[nodiscard]] bool extent_within_limits(const TextureDescription& d,
                                        const TextureLimits& limits) noexcept {
  const Extent3& e = d.extent;
  switch (d.dimension) {
  case TextureDimension::Texture2D:
  case TextureDimension::Texture2DArray:
    return e.width <= limits.max_dimension_2d && e.height <= limits.max_dimension_2d &&
           e.depth == 1U;
  case TextureDimension::TextureCube:
    // A cube is square: width == height.
    return e.width == e.height && e.width <= limits.max_dimension_cube && e.depth == 1U;
  case TextureDimension::Texture3D:
    return e.width <= limits.max_dimension_3d && e.height <= limits.max_dimension_3d &&
           e.depth <= limits.max_dimension_3d;
  }
  return false;
}

// Mips of a full chain down to 1x1(x1): 1 + floor(log2(largest dimension)).
[[nodiscard]] std::uint32_t full_mip_chain(const TextureDescription& d) noexcept {
  std::uint32_t largest = std::max(d.extent.width, d.extent.height);
  if (d.dimension == TextureDimension::Texture3D) {
    largest = std::max(largest, d.extent.depth);
  }
  std::uint32_t mips = 1;
  while (largest > 1U) {
    largest >>= 1U;
    ++mips;
  }
  return mips;
}

[[nodiscard]] bool is_power_of_two(std::uint32_t value) noexcept {
  return value != 0U && (value & (value - 1U)) == 0U;
}

// Slices a view range is checked against: 6 per cube, 1 for a volume.
[[nodiscard]] std::uint32_t view_slices(const TextureDescription& d) noexcept {
  switch (d.dimension) {
  case TextureDimension::TextureCube:
    return 6U * d.array_size;
  case TextureDimension::Texture3D:
    return 1U;
  case TextureDimension::Texture2D:
  case TextureDimension::Texture2DArray:
    return d.array_size;
  }
  return d.array_size;
}

// A 3D view only of a 3D texture, a 3D texture only through a 3D view, a
// cube view only of a cube texture.
[[nodiscard]] bool dimension_compatible(const TextureDescription& texture,
                                        const TextureViewDescription& view_description) noexcept {
  const TextureDimension view = view_description.dimension;
  switch (texture.dimension) {
  case TextureDimension::Texture3D:
    return view == TextureDimension::Texture3D;
  case TextureDimension::TextureCube:
    return view != TextureDimension::Texture3D;
  case TextureDimension::Texture2D:
  case TextureDimension::Texture2DArray:
    return view == TextureDimension::Texture2D || view == TextureDimension::Texture2DArray;
  }
  return false;
}

// The linear and sRGB encodings of one format: the only reinterpretation a
// view may name besides the texture's own format.
[[nodiscard]] Format srgb_twin(Format format) noexcept {
  switch (format) {
  case Format::R8G8B8A8Unorm:
    return Format::R8G8B8A8UnormSrgb;
  case Format::R8G8B8A8UnormSrgb:
    return Format::R8G8B8A8Unorm;
  case Format::B8G8R8A8Unorm:
    return Format::B8G8R8A8UnormSrgb;
  case Format::B8G8R8A8UnormSrgb:
    return Format::B8G8R8A8Unorm;
  case Format::BC1Unorm:
    return Format::BC1UnormSrgb;
  case Format::BC1UnormSrgb:
    return Format::BC1Unorm;
  case Format::BC2Unorm:
    return Format::BC2UnormSrgb;
  case Format::BC2UnormSrgb:
    return Format::BC2Unorm;
  case Format::BC3Unorm:
    return Format::BC3UnormSrgb;
  case Format::BC3UnormSrgb:
    return Format::BC3Unorm;
  case Format::BC7Unorm:
    return Format::BC7UnormSrgb;
  case Format::BC7UnormSrgb:
    return Format::BC7Unorm;
  default:
    return Format::Unknown;
  }
}

} // namespace

// Checked in ResourceError's order; the first that applies is returned.
ResourceError validate_buffer(const BufferDescription& description) noexcept {
  if (description.size == 0U) {
    return ResourceError::ZeroSize;
  }
  if (description.usage == BufferUsage::None) {
    return ResourceError::NoUsage;
  }
  if (has_usage(description.usage, BufferUsage::Upload) &&
      has_usage(description.usage, BufferUsage::UnorderedAccess)) {
    return ResourceError::IncompatibleUsage;
  }
  if (description.initial_access == Access::Unknown) {
    return ResourceError::UnknownInitialAccess;
  }
  if (!initial_access_allowed(description.initial_access, allowed_access(description.usage))) {
    return ResourceError::InitialAccessNotAllowed;
  }
  if (description.debug_name.empty()) {
    return ResourceError::MissingDebugName;
  }
  const bool vertex = has_usage(description.usage, BufferUsage::Vertex);
  const bool index = has_usage(description.usage, BufferUsage::Index);
  if ((vertex || index) && description.stride == 0U) {
    return ResourceError::InvalidStride;
  }
  if (index && description.stride != 2U && description.stride != 4U) {
    return ResourceError::InvalidStride;
  }
  return ResourceError::None;
}

ResourceError validate_buffer_initial_data(const BufferDescription& description,
                                           std::span<const std::byte> data) noexcept {
  if (!data.empty() && data.size() != description.size) {
    return ResourceError::InitialDataMismatch;
  }
  return ResourceError::None;
}

// The pitch rule above SubresourceData: one entry per subresource in
// subresource_index order; row_pitch at least row_bytes of the mip width;
// slice_pitch read only for a 3D mip with depth above 1, and then at least
// slice_bytes of the mip extent; bytes cover every addressed byte, the last
// row counting row_bytes only. Precondition: validate_texture returned None
// (the format has a block size).
ResourceError validate_initial_data(const TextureDescription& description,
                                    std::span<const SubresourceData> data) noexcept {
  const TextureDescription& d = description;
  if (data.empty()) {
    return ResourceError::None;
  }
  if (data.size() != subresource_count(d)) {
    return ResourceError::InitialDataMismatch;
  }
  const FormatInfo info = format_info(d.format);
  if (info.block_height == 0U) {
    return ResourceError::InitialDataMismatch;
  }
  const auto at_mip = [](std::uint32_t size, std::uint32_t mip) {
    const std::uint32_t value = size >> mip;
    return value == 0U ? 1U : value;
  };
  for (std::size_t i = 0; i < data.size(); ++i) {
    const auto mip = static_cast<std::uint32_t>(i % d.mip_levels);
    const std::uint32_t width = at_mip(d.extent.width, mip);
    const std::uint32_t height = at_mip(d.extent.height, mip);
    const std::uint32_t depth =
        d.dimension == TextureDimension::Texture3D ? at_mip(d.extent.depth, mip) : 1U;
    const std::uint64_t tight_row = row_bytes(d.format, width);
    const std::uint64_t rows = (height + info.block_height - 1U) / info.block_height;
    const SubresourceData& entry = data[i];
    const std::uint64_t row_pitch = entry.row_pitch;
    if (row_pitch < tight_row) {
      return ResourceError::InitialDataMismatch;
    }
    std::uint64_t needed = row_pitch * (rows - 1U) + tight_row;
    if (depth > 1U) {
      const std::uint64_t slice_pitch = entry.slice_pitch;
      if (slice_pitch < slice_bytes(d.format, Extent2{width, height})) {
        return ResourceError::InitialDataMismatch;
      }
      needed += slice_pitch * (depth - 1U);
    }
    if (entry.bytes.size() < needed) {
      return ResourceError::InitialDataMismatch;
    }
  }
  return ResourceError::None;
}

// Checked in ResourceError's order; the first that applies is returned.
// IncompatibleUsage includes "a usage the format's capabilities do not
// include"; that clause is read for a supported format only, so a format the
// device does not support at all, or Unknown, is UnsupportedFormat.
ResourceError validate_texture(const TextureDescription& description, const TextureLimits& limits,
                               FormatCapability format_capability) noexcept {
  const TextureDescription& d = description;
  if (d.extent.width == 0U || d.extent.height == 0U || d.extent.depth == 0U) {
    return ResourceError::ZeroSize;
  }
  if (d.usage == TextureUsage::None) {
    return ResourceError::NoUsage;
  }
  const bool supported = format_supported(d.format, format_capability);
  if ((has_usage(d.usage, TextureUsage::DepthStencil) &&
       has_usage(d.usage, TextureUsage::RenderTarget)) ||
      (supported && !usage_within_capability(d.usage, format_capability))) {
    return ResourceError::IncompatibleUsage;
  }
  if (d.initial_access == Access::Unknown) {
    return ResourceError::UnknownInitialAccess;
  }
  if (!initial_access_allowed(d.initial_access, allowed_access(d.usage))) {
    return ResourceError::InitialAccessNotAllowed;
  }
  if (d.debug_name.empty()) {
    return ResourceError::MissingDebugName;
  }
  // InvalidStride is a buffer rule.
  // The checks run in ResourceError's order, which the header makes the
  // contract.
  if (!supported) {
    return ResourceError::UnsupportedFormat;
  }
  if (!extent_within_limits(d, limits)) {
    return ResourceError::ExtentOutOfRange;
  }
  // A multisampled texture has one mip. The mip count is bounded by the
  // texture's full mip chain.
  if (d.mip_levels == 0U || d.mip_levels > full_mip_chain(d) ||
      (d.sample_count > 1U && d.mip_levels != 1U)) {
    return ResourceError::MipCountOutOfRange;
  }
  // Only an array dimension takes more than one slice (there is no cube
  // array, resources.hpp TextureDimension).
  if (d.array_size == 0U ||
      (d.dimension == TextureDimension::Texture2DArray ? d.array_size > limits.max_array_size
                                                       : d.array_size != 1U)) {
    return ResourceError::ArraySizeOutOfRange;
  }
  // Samples only on 2D and 2D array textures; the sample count is a power of
  // two within the limit.
  if (!is_power_of_two(d.sample_count) || d.sample_count > limits.max_sample_count) {
    return ResourceError::SampleCountUnsupported;
  }
  if (d.sample_count > 1U && ((d.dimension != TextureDimension::Texture2D &&
                               d.dimension != TextureDimension::Texture2DArray) ||
                              !has_capability(format_capability, FormatCapability::Multisample))) {
    return ResourceError::SampleCountUnsupported;
  }
  return ResourceError::None;
}

// The offset lies inside the buffer and the range inside it. ResourceError
// has no code for an offset alignment rule; the backend rejects an offset
// its API cannot take with BackendRejected. Size 0 means "to the end of the
// buffer" at any offset (resources.hpp).
ResourceError validate_buffer_view(const BufferDescription& buffer,
                                   const BufferViewDescription& view,
                                   bool unordered_access) noexcept {
  if (view.offset >= buffer.size || (view.size != 0U && view.size > buffer.size - view.offset)) {
    return ResourceError::ViewOutOfRange;
  }
  // Every buffer view is raw: no format to reconcile (ViewFormatIncompatible).
  const BufferUsage needed =
      unordered_access ? BufferUsage::UnorderedAccess : BufferUsage::ShaderResource;
  if (!has_usage(buffer.usage, needed)) {
    return ResourceError::ViewUsageMissing;
  }
  return ResourceError::None;
}

// Dimension compatibility, mip and array ranges, plane and format, and that
// a UAV covers one mip of a texture that is not multisampled, checked in
// ResourceError's order. ViewOutOfRange covers the dimension, the mip and
// slice ranges and the storage view's shape: one mip, one slice, dimension
// Texture2D (the storage array is RWTexture2D, root_layout.hpp) and a
// texture that is not multisampled.
// ViewFormatIncompatible covers the plane and the format. A count of 0
// means "every mip (slice) from first" at any first mip or slice
// (resources.hpp).
ResourceError validate_texture_view(const TextureDescription& texture,
                                    const TextureViewDescription& view,
                                    bool unordered_access) noexcept {
  if (!dimension_compatible(texture, view)) {
    return ResourceError::ViewOutOfRange;
  }
  const std::uint32_t mips = texture.mip_levels;
  if (view.first_mip >= mips || (view.mip_count != 0U && view.mip_count > mips - view.first_mip)) {
    return ResourceError::ViewOutOfRange;
  }
  // A cube view's slice range is counted in faces (6 per cube).
  const std::uint32_t slices = view_slices(texture);
  if (view.first_slice >= slices ||
      (view.slice_count != 0U && view.slice_count > slices - view.first_slice)) {
    return ResourceError::ViewOutOfRange;
  }
  // A UAV covers exactly one mip; a count of 0 is accepted when one mip
  // remains from first_mip.
  if (unordered_access) {
    const std::uint32_t mip_count =
        view.mip_count == 0U ? mips - view.first_mip : std::uint32_t{view.mip_count};
    const std::uint32_t slice_count =
        view.slice_count == 0U ? slices - view.first_slice : std::uint32_t{view.slice_count};
    if (mip_count != 1U || slice_count != 1U || view.dimension != TextureDimension::Texture2D ||
        texture.sample_count > 1U) {
      return ResourceError::ViewOutOfRange;
    }
  }
  // Both the Depth and the Stencil plane are gated on the format (is_depth,
  // has_stencil).
  const FormatInfo info = format_info(texture.format);
  if ((view.plane == TexturePlane::Depth && !info.is_depth) ||
      (view.plane == TexturePlane::Stencil && !info.has_stencil)) {
    return ResourceError::ViewFormatIncompatible;
  }
  if (view.format != Format::Unknown && view.format != texture.format &&
      (view.plane != TexturePlane::Primary || view.format != srgb_twin(texture.format))) {
    return ResourceError::ViewFormatIncompatible;
  }
  const TextureUsage needed =
      unordered_access ? TextureUsage::UnorderedAccess : TextureUsage::ShaderResource;
  if (!has_usage(texture.usage, needed)) {
    return ResourceError::ViewUsageMissing;
  }
  return ResourceError::None;
}

Buffer::Buffer(DeferredDeleteQueue& queue, const BufferDescription& description) noexcept
    : Viewable(queue, description.initial_access, description.debug_name, false),
      description_(description) {
  description_.debug_name = debug_name();
}

Texture::Texture(DeferredDeleteQueue& queue, const TextureDescription& description) noexcept
    : Viewable(queue, description.initial_access, description.debug_name, true),
      description_(description) {
  description_.debug_name = debug_name();
}

} // namespace bw::rhi
