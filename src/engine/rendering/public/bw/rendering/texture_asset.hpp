#ifndef BW_RENDERING_TEXTURE_ASSET_HPP
#define BW_RENDERING_TEXTURE_ASSET_HPP
#include "bw/rendering/rhi/format.hpp"
#include "bw/rendering/rhi/resources.hpp"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>

// Responsibility: the texture payload of a cooked asset and its reader
// (docs/architecture/content.md, "Asset kinds": block-compressed or raw
// texel data with mip chain, format, color space). The converter
// generates the full mip chain and block-compresses; the renderer creates
// an RHI texture from this view with one
// SubresourceData per subresource and no copy.
//
// Layout, little-endian, read by copy: TexturePayloadHeader at 0;
// subresource records at subresources_offset in RHI subresource order
// (slice-major, mip-minor: index = slice * mip_levels + mip,
// rhi/resources.hpp subresource_index); each subresource's bytes at its
// own offset, a multiple of 16 from the payload start, tightly packed rows
// of row_bytes(format, mip width).
//
// Contract (decode refuses a payload that breaks a rule below and names a
// broken rule; with several broken, which one is named is not promised):
//   1. The header fits; the record table and every subresource are within
//      the payload at 16-aligned offsets.
//   2. format is an enumerator of TextureFormat; dimension is Texture2D
//      with faces 1, or Cube with faces 6 and width equal to height.
//   3. width and height are at least 1 and at most kMaxTextureExtent;
//      mip_levels equals full_mip_count({width, height}): every chain runs
//      to 1x1 (the converter generates it; no partial chains).
//   4. Each subresource's size is slice_bytes(format, mip_extent(...)) and
//      its row_pitch is row_bytes(format, mip width): rows are tight.
//   5. flags holds only known bits. kTwoChannelNormal marks a BC5 normal
//      map whose Z the shader reconstructs.
//   TextureFormat values are fixed numbers in the file, independent of the
//   RHI enumeration's order; to_rhi_format maps each. sRGB is a property
//   of the format, as in the RHI.
//   Mip arithmetic: full_mip_count({w, h}) = floor(log2(max(w, h))) + 1;
//   mip_extent({w, h}, level) = (max(1, w >> level), max(1, h >> level)),
//   rounding down.
//   Non-power-of-two images keep their size; a block-compressed mip whose
//   extent is not a multiple of 4 holds whole blocks, as row_bytes counts.
//   Cube faces are stored +X, -X, +Y, -Y, +Z, -Z in slices 0 to 5; the
//   converter writes this order.
//
// Core guidelines read: SL.con.3, C.183, Con.5 (as mesh_asset.hpp),
// Enum.3 (file values are a fixed enumeration). Performance guidelines
// read: CDSA.32 (texels are stored in the format the GPU reads), CACHE.2
// (sizes are stated by the format table before any allocation), GDSA.18
// (block compression is the stored code; the GPU decodes).
namespace bw::rendering {

static_assert(std::endian::native == std::endian::little);

inline constexpr std::uint32_t kTextureSectionAlignment = 16;
inline constexpr std::uint32_t kMaxTextureExtent = 16384;
inline constexpr std::uint32_t kTwoChannelNormal = 1U << 0U;
inline constexpr std::uint32_t kKnownTextureFlags = kTwoChannelNormal;

// Fixed file values (a u32 in the file); never renumbered.
enum class TextureFormat : std::uint8_t {
  BC1Unorm = 1,
  BC1UnormSrgb = 2,
  BC3Unorm = 3,
  BC3UnormSrgb = 4,
  BC4Unorm = 5,
  BC5Unorm = 6,
  BC7Unorm = 7,
  BC7UnormSrgb = 8,
  BC6HUfloat = 9,
  R8G8B8A8Unorm = 10,
  R8G8B8A8UnormSrgb = 11,
  R16G16B16A16Float = 12,
};
inline constexpr std::uint32_t kTextureFormatCount = 12;

[[nodiscard]] constexpr bool is_known_texture_format(std::uint32_t value) noexcept {
  return value >= 1U && value <= kTextureFormatCount;
}

[[nodiscard]] constexpr rhi::Format to_rhi_format(TextureFormat format) noexcept {
  switch (format) {
  case TextureFormat::BC1Unorm:
    return rhi::Format::BC1Unorm;
  case TextureFormat::BC1UnormSrgb:
    return rhi::Format::BC1UnormSrgb;
  case TextureFormat::BC3Unorm:
    return rhi::Format::BC3Unorm;
  case TextureFormat::BC3UnormSrgb:
    return rhi::Format::BC3UnormSrgb;
  case TextureFormat::BC4Unorm:
    return rhi::Format::BC4Unorm;
  case TextureFormat::BC5Unorm:
    return rhi::Format::BC5Unorm;
  case TextureFormat::BC7Unorm:
    return rhi::Format::BC7Unorm;
  case TextureFormat::BC7UnormSrgb:
    return rhi::Format::BC7UnormSrgb;
  case TextureFormat::BC6HUfloat:
    return rhi::Format::BC6HUfloat;
  case TextureFormat::R8G8B8A8Unorm:
    return rhi::Format::R8G8B8A8Unorm;
  case TextureFormat::R8G8B8A8UnormSrgb:
    return rhi::Format::R8G8B8A8UnormSrgb;
  case TextureFormat::R16G16B16A16Float:
    return rhi::Format::R16G16B16A16Float;
  }
  return rhi::Format::Unknown;
}

// A u32 in the file.
enum class TextureAssetDimension : std::uint8_t {
  Texture2D = 1,
  Cube = 2,
};

[[nodiscard]] constexpr std::uint32_t full_mip_count(rhi::Extent2 base) noexcept {
  std::uint32_t largest = base.width > base.height ? base.width : base.height;
  std::uint32_t count = 1;
  while (largest > 1U) {
    largest >>= 1U;
    ++count;
  }
  return count;
}
[[nodiscard]] constexpr rhi::Extent2 mip_extent(rhi::Extent2 base, std::uint32_t level) noexcept {
  const std::uint32_t w = base.width >> level;
  const std::uint32_t h = base.height >> level;
  return rhi::Extent2{w == 0U ? 1U : w, h == 0U ? 1U : h};
}

struct TexturePayloadHeader {
  std::uint32_t width{};
  std::uint32_t height{};
  std::uint32_t faces{};
  std::uint32_t mip_levels{};
  std::uint32_t format{};
  std::uint32_t dimension{};
  std::uint32_t flags{};
  std::uint32_t subresource_count{};
  std::uint32_t subresources_offset{};
  std::uint32_t reserved0{};
  std::uint32_t reserved1{};
  std::uint32_t reserved2{};
};
static_assert(sizeof(TexturePayloadHeader) == 48);
static_assert(std::is_trivially_copyable_v<TexturePayloadHeader>);

struct SubresourceRecord {
  std::uint32_t offset{};
  std::uint32_t size{};
  std::uint32_t row_pitch{};
  std::uint32_t reserved{};
};
static_assert(sizeof(SubresourceRecord) == 16);

enum class TextureAssetError : std::uint8_t {
  None,
  HeaderTruncated,
  SectionMisaligned,
  SectionOutOfBounds,
  FormatUnknown,
  DimensionInvalid,
  // faces not 1 for 2D or 6 for a cube; a cube not square.
  FaceCountInvalid,
  ExtentInvalid,
  MipCountInvalid,
  SubresourceCountMismatch,
  SubresourceSizeMismatch,
  RowPitchMismatch,
  FlagsUnknown,
};

struct TextureAssetFailure {
  TextureAssetError error{TextureAssetError::None};
  // The subresource concerned; 0 for a header, table or count failure.
  std::uint32_t index{};
};

class TextureAssetView {
public:
  TextureAssetView() = default;
  [[nodiscard]] const TexturePayloadHeader& header() const noexcept { return header_; }
  [[nodiscard]] TextureFormat format() const noexcept {
    return static_cast<TextureFormat>(header_.format);
  }
  [[nodiscard]] TextureAssetDimension dimension() const noexcept {
    return static_cast<TextureAssetDimension>(header_.dimension);
  }
  [[nodiscard]] std::uint32_t subresource_count() const noexcept {
    return header_.subresource_count;
  }
  // Precondition: index below subresource_count. The RHI's initial-data
  // entry for that subresource, viewing the payload.
  [[nodiscard]] rhi::SubresourceData subresource(std::uint32_t index) const noexcept;
  // The RHI description that create_texture takes for this asset, with
  // usage ShaderResource; initial_access is Unknown and debug_name empty,
  // both the caller's to set before the call.
  [[nodiscard]] rhi::TextureDescription texture_description() const noexcept;

private:
  friend TextureAssetFailure decode_texture_asset(std::span<const std::byte>,
                                                  TextureAssetView&) noexcept;
  std::span<const std::byte> payload_{};
  TexturePayloadHeader header_{};
};

[[nodiscard]] TextureAssetFailure decode_texture_asset(std::span<const std::byte> payload,
                                                       TextureAssetView& view) noexcept;

} // namespace bw::rendering

#endif
