#ifndef BW_CONTENT_CONVERTER_TEXTURE_READER_HPP
#define BW_CONTENT_CONVERTER_TEXTURE_READER_HPP
#include "bw/content_converter/asset_data.hpp"
#include "bw/rendering/texture_asset.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

// Responsibility: the texture pipeline of the converter: the original
// record's 52-byte descriptor, its PNG payload, the color space, the mip
// chain and the block encode, ending in TextureAssetData
// (docs/architecture/content.md, "Asset kinds": block-compressed or raw
// texel data with mip chain, format, color space). Every step is a pure
// function over vectors; the vendored decoder and encoders are reached
// only through the functions here and never from the runtime.
//
// The descriptor: 13 u32 words in the shape of an Xbox 360 texture
// header, the last six a GPU fetch constant. The fields read, by word and
// bit: word 7 sign x,y,z,w at bits 2..9 (two bits each), pitch at 22..30,
// tiled at 31; word 8 format at 0..5, endianness at 6..7, base address at
// 12..31; word 9 width-1 at 0..12, height-1 at 13..25, depth-1 at 26..31;
// word 10 num_format at 0; word 11 mip_max_level at 6..9; word 12
// dimension at 9..10, packed_mips at 11, mip_address at 12..31. Sign 3 is
// gamma, 0 unsigned, 1 signed.
// Format codes seen: DXT1 18, DXT2_3 19, DXT4_5 20, 8_8_8_8 6, 4_4_4_4 15,
// DXN 49, CTX1 60, 16_16_16_16_EXPAND 29.
//
// Contract.
//   decode_descriptor: the fields above from 52 bytes; a payload of
//   another size is DescriptorSizeInvalid.
//   decode_png: a PNG from memory to tightly packed 8-bit RGBA, width and
//   height from IHDR; RGB gains alpha 255; a 16-bit, palette, grey or
//   interlaced file is converted the same way; gAMA, cHRM, iCCP and sRGB
//   chunks change no pixel (the decoded bytes are the stored samples);
//   CRC and Adler checks are on for the chunks the decoder reads (IHDR,
//   PLTE, tRNS, IDAT, IEND); ancillary chunks are not parsed, so a bad CRC
//   in one is passed over; any failure is a named PngError with the
//   decoder's code and text.
//   plan_texture decides, from the descriptor and the PNG's size, the
//   color space, the output format and the flags, naming a failure
//   as a PlanError:
//     sign 63 (gamma) -> sRGB, 0 (unsigned) -> linear, 85 (signed) -> HDR,
//       which only 16_16_16_16_EXPAND carries; 85 on any other format is
//       SignUnsupported (no shipped descriptor has it), as is any other
//       sign value. A descriptor dimension other than 2D or cube is
//       FormatUnsupported (none is shipped).
//     DXT1, DXT2_3, DXT4_5, 8_8_8_8, 4_4_4_4 -> BC7 (sRGB or Unorm by the
//       color space).
//     DXN, CTX1 -> BC5 with kTwoChannelNormal; the green convention is the
//       pipeline option (as stored, or flipped), recorded in the plan.
//     16_16_16_16_EXPAND -> R16G16B16A16Float, no block compression: no
//       BC6H encoder is vendored in this slice (the one the plan named does
//       not exist; the candidate read does not meet the determinism and
//       quality bar). 39 records.
//     The PNG's size is the texture's size (35 descriptors disagree with
//     their PNG; the PNG wins). A 16_16_16_16 PNG is twice the descriptor's
//     width and is read as pairs of pixels per texel (expand_half_texels).
//     A DDSC record's six PNGs are six faces in the content's order, each
//     the extent of the first (ExtentInvalid otherwise),
//     written +X, -X, +Y, -Y, +Z, -Z; the mapping from content index to
//     face is kCubeFaceOrder, established by the slice (identity until the
//     render check says otherwise), and recorded in the plan.
//   expand_half_texels: from a (2w, h) RGBA8 image, texel (x, y) is the
//   byte pair of pixels 2x and 2x+1: R = R0 | G0 << 8, G = B0 | A0 << 8,
//   B = B1 | G1 << 8, A = R1 | A1 << 8, each an IEEE binary16 bit pattern
//   (little-endian; the odd pixel's R and B bytes are exchanged). Under
//   this reading every one of the 164 images has A exactly 1.0, no NaN or
//   infinity and a maximum of 4032; the pipeline writes the four halfs as
//   they are.
//   generate_mips: the full chain down to 1x1 (full_mip_count), each level
//   from the previous by a 2x2 box filter; an sRGB image is converted to
//   linear, averaged and converted back, per channel, with alpha linear;
//   an odd dimension rounds down and the last source row or column is
//   dropped; a half image is averaged in float and rounded to nearest
//   even. Deterministic: the same bytes on every host, by fixed-order
//   float arithmetic with no fused operations relied on.
//   encode_blocks: BC7 (modes 1, 5, 6, 7; uber level kBc7UberLevel,
//   64 partitions, perceptual weights off: errors measured in linear
//   RGBA), BC5 (two 8-bit channels), BC4; blocks row-major over the mip,
//   a partial edge block padded by clamping to the edge; the vendored
//   encoders' tables are built once before the first block, under a lock,
//   so a concurrent first call is safe. A block decoded back by the
//   vendored decoders is within 2 per 8-bit channel of its input for a
//   block the format can hold exactly (BC7 at kBc7UberLevel measures at
//   most 1; BC4 and BC5 at most 2). BC4 is encodable and never planned:
//   no shipped descriptor is single-channel (plan_texture maps DXN and
//   CTX1 to BC5).
//   convert_texture composes the steps over one record (or six faces) into
//   TextureAssetData. The normal-map green flip, when on, replaces G with
//   255 - G before mip generation on two-channel maps only.
//
// Core guidelines read: I.1 and Con.5 (every decision is a pure function
// of descriptor, pixels and options), E.27 (named failures), R.1 (vectors
// own every image), CP.3 (the only shared state is the encoders' tables,
// written once). Performance guidelines read: GDSA.2 (float reductions
// are declared deterministic by fixed order), CDSA.32 (texels transformed
// once into the format the GPU reads), GDSA.18 (block compression is the
// stored code).
namespace bw::content_converter {

inline constexpr std::size_t kDescriptorBytes = 52;
inline constexpr std::uint32_t kFormatDxt1 = 18;
inline constexpr std::uint32_t kFormatDxt2_3 = 19;
inline constexpr std::uint32_t kFormatDxt4_5 = 20;
inline constexpr std::uint32_t kFormat8888 = 6;
inline constexpr std::uint32_t kFormat4444 = 15;
inline constexpr std::uint32_t kFormatDxn = 49;
inline constexpr std::uint32_t kFormatCtx1 = 60;
inline constexpr std::uint32_t kFormat16161616Expand = 29;
inline constexpr std::uint32_t kSignGamma = 3;
inline constexpr std::uint32_t kSignUnsigned = 0;
inline constexpr std::uint32_t kSignSigned = 1;
inline constexpr std::uint32_t kDimension2D = 1;
inline constexpr std::uint32_t kDimensionCube = 3;
inline constexpr std::uint32_t kBc7UberLevel = 2;
// Content face index i is written to slice kCubeFaceOrder[i].
inline constexpr std::array<std::uint32_t, 6> kCubeFaceOrder{0, 1, 2, 3, 4, 5};

struct TextureDescriptor {
  std::array<std::uint32_t, 4> sign{};
  std::uint32_t pitch{};
  bool tiled{};
  std::uint32_t format{};
  std::uint32_t endianness{};
  std::uint32_t base_address{};
  std::uint32_t width{};
  std::uint32_t height{};
  std::uint32_t depth{};
  std::uint32_t num_format{};
  std::uint32_t mip_max_level{};
  std::uint32_t dimension{};
  bool packed_mips{};
  std::uint32_t mip_address{};
  // The four sign fields packed as the content's 8-bit value (63, 0, 85).
  [[nodiscard]] std::uint32_t sign_bits() const noexcept {
    return sign[0] | (sign[1] << 2U) | (sign[2] << 4U) | (sign[3] << 6U);
  }
};

enum class DescriptorError : std::uint8_t {
  None,
  DescriptorSizeInvalid,
};
[[nodiscard]] DescriptorError decode_descriptor(std::span<const std::byte> bytes,
                                                TextureDescriptor& out) noexcept;

struct Rgba8Image {
  std::uint32_t width{};
  std::uint32_t height{};
  // width * height * 4 bytes, rows top to bottom.
  std::vector<std::uint8_t> pixels{};
};
struct Half4Image {
  std::uint32_t width{};
  std::uint32_t height{};
  // width * height * 4 binary16 bit patterns.
  std::vector<std::uint16_t> texels{};
};

struct PngError {
  // 0 is success; otherwise the decoder's code and its text.
  std::uint32_t code{};
  std::string_view text{};
  [[nodiscard]] bool ok() const noexcept { return code == 0U; }
};
[[nodiscard]] PngError decode_png(std::span<const std::byte> bytes, Rgba8Image& out);

enum class ColorSpace : std::uint8_t {
  Srgb,
  Linear,
  Hdr,
};
enum class NormalMapConvention : std::uint8_t {
  AsStored,
  FlipGreen,
};
struct TexturePipelineOptions {
  NormalMapConvention normal_green{NormalMapConvention::AsStored};
  std::uint32_t bc7_uber_level{kBc7UberLevel};
};

struct TexturePlan {
  ColorSpace color_space{ColorSpace::Linear};
  rendering::TextureFormat format{};
  std::uint32_t flags{};
  bool half_texels{};
  std::uint32_t width{};
  std::uint32_t height{};
  std::uint32_t mip_levels{};
};
enum class PlanError : std::uint8_t {
  None,
  FormatUnsupported,
  SignUnsupported,
  // A half image whose PNG width is not twice the descriptor's.
  HalfWidthMismatch,
  ExtentInvalid,
  // convert_texture: a PNG count other than 1 for a 2D record or 6 for a
  // cube.
  ImageCountInvalid,
};
[[nodiscard]] PlanError plan_texture(const TextureDescriptor& descriptor, std::uint32_t png_width,
                                     std::uint32_t png_height, TexturePlan& out) noexcept;

[[nodiscard]] Half4Image expand_half_texels(const Rgba8Image& pairs);

[[nodiscard]] std::vector<Rgba8Image> generate_mips(const Rgba8Image& base, ColorSpace color_space);
[[nodiscard]] std::vector<Half4Image> generate_half_mips(const Half4Image& base);

enum class EncodeError : std::uint8_t {
  None,
  FormatNotBlockCompressed,
  UberLevelOutOfRange,
};
// Block-compresses one mip; `format` is BC7 (either), BC5 or BC4; the
// result is slice_bytes(to_rhi_format(format), extent) bytes.
[[nodiscard]] EncodeError encode_blocks(const Rgba8Image& mip, rendering::TextureFormat format,
                                        const TexturePipelineOptions& options,
                                        std::vector<std::byte>& out);

struct TextureConvertFailure {
  DescriptorError descriptor{DescriptorError::None};
  PngError png{};
  PlanError plan{PlanError::None};
  EncodeError encode{EncodeError::None};
  // The face (0..5) concerned, 0 for a 2D texture.
  std::uint32_t face{};
  [[nodiscard]] bool ok() const noexcept {
    return descriptor == DescriptorError::None && png.ok() && plan == PlanError::None &&
           encode == EncodeError::None;
  }
};

// One record: its descriptor bytes and one PNG (2D) or six PNGs (cube).
[[nodiscard]] TextureConvertFailure
convert_texture(std::span<const std::byte> descriptor,
                std::span<const std::span<const std::byte>> pngs,
                const TexturePipelineOptions& options, TextureAssetData& out);

} // namespace bw::content_converter

#endif
