#ifndef BW_RENDERING_RHI_FORMAT_HPP
#define BW_RENDERING_RHI_FORMAT_HPP

#include <cstdint>

// Responsibility: the pixel and vertex formats the RHI names and the facts
// about each that are true on every backend (block size, components, depth
// and stencil planes, sRGB). What a backend can do with a format on this
// machine is not here: FormatCapabilities is reported by the device at
// startup (device.hpp, invariant 10).
//
// Contract: format_info is a pure constexpr table; every enumerator has a
// row; Unknown has block_bytes 0. The set is sized by what the game's content
// and passes need: 8-bit and 16-bit color, float render targets, the block
// compressed families textures are converted to, depth and
// depth-stencil, and the vertex attribute formats.
//
// Core guidelines read: Enum.3, Con.5 (a constexpr table, no runtime
// registry), I.1 (capability is asked of the device, never assumed from the
// format). Performance guidelines read: EMB.6 (read-only table); CACHE.2 was
// read for block sizes (the table states bytes per block so a caller can
// count bytes before allocating).
namespace bw::rhi {

// One enumerator per format the game reaches: the formats its imported
// textures, render targets and vertex streams use.
enum class Format : std::uint8_t {
  Unknown = 0,
  // 8-bit normalized color.
  R8Unorm,
  R8G8Unorm,
  R8G8B8A8Unorm,
  R8G8B8A8UnormSrgb,
  B8G8R8A8Unorm,
  B8G8R8A8UnormSrgb,
  // Packed and small float render targets.
  R10G10B10A2Unorm,
  R11G11B10Float,
  R16Float,
  R16G16Float,
  R16G16B16A16Float,
  R16G16B16A16Unorm,
  // 32-bit.
  R32Float,
  R32Uint,
  R32G32Float,
  R32G32B32Float,
  R32G32B32A32Float,
  // Vertex-only formats.
  R16G16Snorm,
  R16G16B16A16Snorm,
  R8G8B8A8Snorm,
  R8G8B8A8Uint,
  // Block compressed.
  BC1Unorm,
  BC1UnormSrgb,
  BC2Unorm,
  BC2UnormSrgb,
  BC3Unorm,
  BC3UnormSrgb,
  BC4Unorm,
  BC5Unorm,
  BC6HUfloat,
  BC7Unorm,
  BC7UnormSrgb,
  // Depth and stencil.
  D16Unorm,
  D32Float,
  D24UnormS8Uint,
  D32FloatS8Uint,
  Count,
};

// The backend-neutral facts of a format. What a device supports for it is
// the device's FormatCapabilities report, never a mutable global table.
struct FormatInfo {
  std::uint8_t block_width{};
  std::uint8_t block_height{};
  std::uint8_t block_bytes{};
  std::uint8_t components{};
  bool is_depth{};
  bool has_stencil{};
  bool is_compressed{};
  bool is_srgb{};
};

[[nodiscard]] constexpr FormatInfo format_info(Format format) noexcept;

// Texel dimensions of one mip.
struct Extent2 {
  std::uint32_t width{};
  std::uint32_t height{};
  [[nodiscard]] friend constexpr bool operator==(Extent2, Extent2) noexcept = default;
};

// Bytes of one row of blocks and of one whole mip, from the block table.
// Dimensions are texels; a partial block counts whole.
[[nodiscard]] constexpr std::uint32_t row_bytes(Format format, std::uint32_t width) noexcept;
[[nodiscard]] constexpr std::uint64_t slice_bytes(Format format, Extent2 extent) noexcept;

// What a backend reports for one format at startup. The bits are the ones
// the game's passes ask about.
enum class FormatCapability : std::uint8_t {
  None = 0,
  Texture = 1U << 0U,
  RenderTarget = 1U << 1U,
  DepthStencil = 1U << 2U,
  UnorderedAccess = 1U << 3U,
  Blendable = 1U << 4U,
  VertexAttribute = 1U << 5U,
  Multisample = 1U << 6U,
};
[[nodiscard]] constexpr FormatCapability operator|(FormatCapability a,
                                                   FormatCapability b) noexcept {
  // A flag combination is not an enumerator.
  // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange)
  return static_cast<FormatCapability>(static_cast<std::uint8_t>(a) | static_cast<std::uint8_t>(b));
}
[[nodiscard]] constexpr FormatCapability operator&(FormatCapability a,
                                                   FormatCapability b) noexcept {
  return static_cast<FormatCapability>(static_cast<std::uint8_t>(a) & static_cast<std::uint8_t>(b));
}
[[nodiscard]] constexpr bool has_capability(FormatCapability value, FormatCapability bit) noexcept {
  return (value & bit) == bit;
}

// ---- constexpr definitions ------------------------------------------------

constexpr FormatInfo format_info(Format format) noexcept {
  switch (format) {
  case Format::Unknown:
  case Format::Count:
    return {};
  case Format::R8Unorm:
    return {1, 1, 1, 1, false, false, false, false};
  case Format::R8G8Unorm:
    return {1, 1, 2, 2, false, false, false, false};
  case Format::R8G8B8A8Unorm:
  case Format::B8G8R8A8Unorm:
  case Format::R8G8B8A8Snorm:
  case Format::R8G8B8A8Uint:
    return {1, 1, 4, 4, false, false, false, false};
  case Format::R8G8B8A8UnormSrgb:
  case Format::B8G8R8A8UnormSrgb:
    return {1, 1, 4, 4, false, false, false, true};
  case Format::R10G10B10A2Unorm:
    return {1, 1, 4, 4, false, false, false, false};
  case Format::R11G11B10Float:
    return {1, 1, 4, 3, false, false, false, false};
  case Format::R16Float:
    return {1, 1, 2, 1, false, false, false, false};
  case Format::R16G16Float:
  case Format::R16G16Snorm:
    return {1, 1, 4, 2, false, false, false, false};
  case Format::R16G16B16A16Float:
  case Format::R16G16B16A16Unorm:
  case Format::R16G16B16A16Snorm:
    return {1, 1, 8, 4, false, false, false, false};
  case Format::R32Float:
  case Format::R32Uint:
    return {1, 1, 4, 1, false, false, false, false};
  case Format::R32G32Float:
    return {1, 1, 8, 2, false, false, false, false};
  case Format::R32G32B32Float:
    return {1, 1, 12, 3, false, false, false, false};
  case Format::R32G32B32A32Float:
    return {1, 1, 16, 4, false, false, false, false};
  case Format::BC1Unorm:
    return {4, 4, 8, 4, false, false, true, false};
  case Format::BC1UnormSrgb:
    return {4, 4, 8, 4, false, false, true, true};
  case Format::BC2Unorm:
  case Format::BC3Unorm:
  case Format::BC7Unorm:
    return {4, 4, 16, 4, false, false, true, false};
  case Format::BC2UnormSrgb:
  case Format::BC3UnormSrgb:
  case Format::BC7UnormSrgb:
    return {4, 4, 16, 4, false, false, true, true};
  case Format::BC4Unorm:
    return {4, 4, 8, 1, false, false, true, false};
  case Format::BC5Unorm:
    return {4, 4, 16, 2, false, false, true, false};
  case Format::BC6HUfloat:
    return {4, 4, 16, 3, false, false, true, false};
  case Format::D16Unorm:
    return {1, 1, 2, 1, true, false, false, false};
  case Format::D32Float:
    return {1, 1, 4, 1, true, false, false, false};
  case Format::D24UnormS8Uint:
    return {1, 1, 4, 2, true, true, false, false};
  case Format::D32FloatS8Uint:
    return {1, 1, 8, 2, true, true, false, false};
  }
  return {};
}

constexpr std::uint32_t row_bytes(Format format, std::uint32_t width) noexcept {
  const FormatInfo info = format_info(format);
  if (info.block_width == 0) {
    return 0;
  }
  const std::uint32_t blocks = (width + info.block_width - 1U) / info.block_width;
  return blocks * info.block_bytes;
}

constexpr std::uint64_t slice_bytes(Format format, Extent2 extent) noexcept {
  const FormatInfo info = format_info(format);
  if (info.block_height == 0) {
    return 0;
  }
  const std::uint32_t rows = (extent.height + info.block_height - 1U) / info.block_height;
  return static_cast<std::uint64_t>(row_bytes(format, extent.width)) * rows;
}

} // namespace bw::rhi
#endif
