#include "bw/content_converter/texture_reader.hpp"

#include "bw/content_converter/asset_data.hpp"
#include "bw/rendering/rhi/format.hpp"
#include "bw/rendering/texture_asset.hpp"

#include "bytes.hpp"

#include <bc7enc.h>
#include <lodepng.h>
#include <rgbcx.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

// Responsibility: the out-of-line part of
// bw/content_converter/texture_reader.hpp: the descriptor decode, the PNG
// decode, the plan, the half-texel expansion, the mip chains, the block
// encode and their composition.
//
// The descriptor's fields, by word and bit, are the GPU texture fetch
// constant's; the record's slot bytes are the descriptor.
// The fetch constant's other fields (clamp modes, swizzle, filters, LOD
// bias, border colour, request size, stacking) and descriptor words 0..6
// are not read: the converter writes its own sampler-independent payload,
// and nothing in it depends on them.
//
// The PNG decoder is the vendored LodePNG through its C entry point, asked
// for 8-bit RGBA; it converts every other colour type, bit depth and the
// Adam7 interlace itself (a 16-bit sample keeps its high byte). It is
// compiled without its ancillary-chunk section, so gAMA, cHRM, iCCP and sRGB
// are skipped as unknown chunks and change no pixel. Its CRC and Adler-32
// checks are left at their defaults, which are on; the CRC is checked on
// every chunk the decoder reads (IHDR, PLTE, tRNS, IDAT, IEND) and not on a
// chunk it skips, which reaches no pixel. The decoded buffer comes
// from the library's malloc and is released with free on every path. One
// reading the header does not state: an RGB file with a tRNS colour key
// gives alpha 0 where the key matches, as LodePNG decodes it (tRNS is read
// even without the ancillary section); no shipped texture has a tRNS chunk.
//
// The block encoders are the vendored rgbcx (BC4, BC5) and bc7enc (BC7,
// modes 1, 5, 6 and 7). Both keep tables in globals that their init
// functions write, so both inits run once, under std::call_once, before the
// first block is encoded; after that the encoders write only the caller's
// block and their own stack. BC7 parameters: the library defaults, then
// linear weights (perceptual off, every channel weight 1), the mode mask
// limited to the four modes the encoder implements, 64 partitions and the
// caller's uber level (at most BC7ENC_MAX_UBER_LEVEL). Each 4x4 block is
// copied, edge-clamped, into a 4-byte-aligned local array first: rgbcx reads
// pixels through a 4-byte struct.
//
// Readings of the header that the code encodes:
//   plan_texture checks in the header's order: the format code and the
//   descriptor's dimension (a dimension other than 2D or cube is
//   FormatUnsupported, as stated), then the sign (a value other than 63, 0
//   or 85, or 85 on a format other than 16_16_16_16_EXPAND, is
//   SignUnsupported, as stated: an 8-bit image cannot hold HDR values), then
//   the half width, then the extent (zero, over kMaxTextureExtent, or a cube
//   face that is not square).
//   A two-channel map flagged gamma keeps the sRGB colour space for its mip
//   chain (its samples are gamma-encoded as stored) and is written BC5Unorm,
//   which has no sRGB form; 114 normal-map slots are so flagged.
//   Nothing the content holds plans to BC4 (no single-channel format code);
//   encode_blocks implements it for the pipeline's future sources.
//   The uber level is checked for BC7 only; BC4 and BC5 have no such knob.
//   encode_blocks requires the image invariant Rgba8Image states (width *
//   height * 4 bytes, width and height at least 1).
//   convert_texture: a descriptor whose dimension is cube takes six PNGs and
//   any other takes one; another count is PlanError::ImageCountInvalid, as
//   the header states. A face whose extent differs from face 0's is
//   PlanError::ExtentInvalid. The first failing face is reported.
//   sRGB conversions use tables built once from the sRGB transfer function
//   with a fifth root found by Newton's method in double arithmetic, using
//   only IEEE basic operations, so the tables are the same bits on every
//   host whatever its maths library. The averaged linear value goes back to
//   the code whose encoded interval holds it (boundaries at code + 0.5).
//   Linear 8-bit channels and alpha average in integers, rounding half up.
//   The library is compiled with floating-point contraction off.
namespace bw::content_converter {

namespace {

using rendering::TextureFormat;

constexpr std::uint32_t kSignGammaBits = 63;
constexpr std::uint32_t kSignUnsignedBits = 0;
constexpr std::uint32_t kSignSignedBits = 85;
constexpr std::uint32_t kBc7ModeMask = (1U << 1U) | (1U << 5U) | (1U << 6U) | (1U << 7U);
constexpr std::size_t kBlockPixels = 16;

std::uint32_t bits(std::uint32_t word, std::uint32_t first, std::uint32_t count) noexcept {
  return (word >> first) & ((1U << count) - 1U);
}

// ---- sRGB tables ----------------------------------------------------------

// y^(1/5) for y in (0, 1], from above, by Newton's method; stops when an
// iteration no longer decreases the estimate.
double fifth_root(double y) noexcept {
  double r = 1.0;
  for (int i = 0; i < 200; ++i) {
    const double r2 = r * r;
    const double r4 = r2 * r2;
    const double next = ((4.0 * r) + (y / r4)) / 5.0;
    if (!(next < r)) {
      break;
    }
    r = next;
  }
  return r;
}

double srgb_to_linear(double encoded) noexcept {
  if (encoded <= 0.04045) {
    return encoded / 12.92;
  }
  const double y = (encoded + 0.055) / 1.055;
  const double root = fifth_root(y);
  // y^2.4 = y^2 * (y^(1/5))^2.
  return (y * y) * (root * root);
}

struct SrgbTables {
  std::array<float, 256> to_linear{};
  // The linear value at the boundary between code c and c + 1.
  std::array<double, 255> boundary{};
};

const SrgbTables& srgb_tables() {
  static const SrgbTables tables = [] {
    SrgbTables t{};
    for (std::size_t c = 0; c < t.to_linear.size(); ++c) {
      t.to_linear[c] = static_cast<float>(srgb_to_linear(static_cast<double>(c) / 255.0));
    }
    for (std::size_t c = 0; c < t.boundary.size(); ++c) {
      t.boundary[c] = srgb_to_linear((static_cast<double>(c) + 0.5) / 255.0);
    }
    return t;
  }();
  return tables;
}

std::uint8_t linear_to_srgb(const SrgbTables& t, float linear) noexcept {
  const double value = linear;
  const auto upper = std::upper_bound(t.boundary.begin(), t.boundary.end(), value);
  return static_cast<std::uint8_t>(upper - t.boundary.begin());
}

// ---- binary16 -------------------------------------------------------------

float half_to_float(std::uint16_t h) noexcept {
  const std::uint32_t sign = (static_cast<std::uint32_t>(h) & 0x8000U) << 16U;
  const std::uint32_t exponent = (static_cast<std::uint32_t>(h) >> 10U) & 0x1FU;
  std::uint32_t mantissa = static_cast<std::uint32_t>(h) & 0x3FFU;
  if (exponent == 0U) {
    if (mantissa == 0U) {
      return std::bit_cast<float>(sign);
    }
    std::uint32_t shift = 0;
    while ((mantissa & 0x400U) == 0U) {
      mantissa <<= 1U;
      ++shift;
    }
    mantissa &= 0x3FFU;
    return std::bit_cast<float>(sign | ((113U - shift) << 23U) | (mantissa << 13U));
  }
  if (exponent == 0x1FU) {
    return std::bit_cast<float>(sign | 0x7F800000U | (mantissa << 13U));
  }
  return std::bit_cast<float>(sign | ((exponent + 112U) << 23U) | (mantissa << 13U));
}

// Round to nearest, ties to even; overflow to infinity; NaN stays NaN.
std::uint16_t float_to_half(float value) noexcept {
  std::uint32_t f = std::bit_cast<std::uint32_t>(value);
  const std::uint32_t sign = f & 0x80000000U;
  f ^= sign;
  constexpr std::uint32_t kInfinity = 255U << 23U;
  constexpr std::uint32_t kHalfOverflow = (127U + 16U) << 23U;
  constexpr std::uint32_t kDenormMagic = ((127U - 15U) + (23U - 10U) + 1U) << 23U;
  std::uint32_t out = 0;
  if (f >= kHalfOverflow) {
    out = f > kInfinity ? 0x7E00U : 0x7C00U;
  } else if (f < (113U << 23U)) {
    // Below the smallest normal half: let one float addition round the
    // mantissa into place, ties to even, as IEEE addition does.
    const float sum = std::bit_cast<float>(f) + std::bit_cast<float>(kDenormMagic);
    out = std::bit_cast<std::uint32_t>(sum) - kDenormMagic;
  } else {
    const std::uint32_t odd = (f >> 13U) & 1U;
    f += ((15U - 127U) << 23U) + 0xFFFU;
    f += odd;
    out = f >> 13U;
  }
  return static_cast<std::uint16_t>(out | (sign >> 16U));
}

// ---- encoders -------------------------------------------------------------

void init_encoders() {
  static std::once_flag once;
  std::call_once(once, [] {
    rgbcx::init(rgbcx::bc1_approx_mode::cBC1Ideal);
    bc7enc_compress_block_init();
  });
}

bool is_bc7(TextureFormat format) noexcept {
  return format == TextureFormat::BC7Unorm || format == TextureFormat::BC7UnormSrgb;
}

} // namespace

DescriptorError decode_descriptor(std::span<const std::byte> bytes_in,
                                  TextureDescriptor& out) noexcept {
  if (bytes_in.size() != kDescriptorBytes) {
    return DescriptorError::DescriptorSizeInvalid;
  }
  const auto word = [&](std::size_t i) { return bytes::u32(bytes_in, 4U * i); };
  TextureDescriptor d{};
  const std::uint32_t w7 = word(7);
  for (std::uint32_t i = 0; i < 4; ++i) {
    d.sign[i] = bits(w7, 2U + (2U * i), 2);
  }
  d.pitch = bits(w7, 22, 9);
  d.tiled = bits(w7, 31, 1) != 0U;
  const std::uint32_t w8 = word(8);
  d.format = bits(w8, 0, 6);
  d.endianness = bits(w8, 6, 2);
  d.base_address = bits(w8, 12, 20);
  const std::uint32_t w9 = word(9);
  d.width = bits(w9, 0, 13) + 1U;
  d.height = bits(w9, 13, 13) + 1U;
  d.depth = bits(w9, 26, 6) + 1U;
  d.num_format = bits(word(10), 0, 1);
  d.mip_max_level = bits(word(11), 6, 4);
  const std::uint32_t w12 = word(12);
  d.dimension = bits(w12, 9, 2);
  d.packed_mips = bits(w12, 11, 1) != 0U;
  d.mip_address = bits(w12, 12, 20);
  out = d;
  return DescriptorError::None;
}

PngError decode_png(std::span<const std::byte> png, Rgba8Image& out) {
  out = Rgba8Image{};
  unsigned char* decoded = nullptr;
  unsigned width = 0;
  unsigned height = 0;
  // Viewing bytes as unsigned char is the one cast the language allows for
  // this (unsigned char may alias any object).
  const unsigned code = lodepng_decode32(
      &decoded, &width, &height, reinterpret_cast<const unsigned char*>(png.data()), png.size());
  // The library allocates with malloc, on failure too; free(nullptr) is a
  // no-op, so the owner releases whatever came back on every path.
  const std::unique_ptr<unsigned char, decltype(&std::free)> owner(decoded, &std::free);
  if (code != 0U) {
    return PngError{code, lodepng_error_text(code)};
  }
  const std::size_t bytes = static_cast<std::size_t>(width) * height * 4U;
  out.width = width;
  out.height = height;
  out.pixels.assign(owner.get(), owner.get() + bytes);
  return PngError{};
}

// The signature is the public header's; the two extents are named at every
// call.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
PlanError plan_texture(const TextureDescriptor& descriptor, std::uint32_t png_width,
                       std::uint32_t png_height, TexturePlan& out) noexcept {
  out = TexturePlan{};
  TexturePlan plan{};
  bool block_compressed = true;
  switch (descriptor.format) {
  case kFormatDxt1:
  case kFormatDxt2_3:
  case kFormatDxt4_5:
  case kFormat8888:
  case kFormat4444:
    plan.format = TextureFormat::BC7Unorm;
    break;
  case kFormatDxn:
  case kFormatCtx1:
    plan.format = TextureFormat::BC5Unorm;
    plan.flags = rendering::kTwoChannelNormal;
    break;
  case kFormat16161616Expand:
    plan.format = TextureFormat::R16G16B16A16Float;
    plan.half_texels = true;
    block_compressed = false;
    break;
  default:
    return PlanError::FormatUnsupported;
  }
  // A 1D or 3D descriptor is FormatUnsupported (the header; none is shipped).
  if (descriptor.dimension != kDimension2D && descriptor.dimension != kDimensionCube) {
    return PlanError::FormatUnsupported;
  }
  switch (descriptor.sign_bits()) {
  case kSignGammaBits:
    plan.color_space = ColorSpace::Srgb;
    break;
  case kSignUnsignedBits:
    plan.color_space = ColorSpace::Linear;
    break;
  case kSignSignedBits:
    plan.color_space = ColorSpace::Hdr;
    break;
  default:
    return PlanError::SignUnsupported;
  }
  // Sign 85 only with 16_16_16_16_EXPAND (the header; no shipped descriptor
  // has it on another format).
  if (plan.color_space == ColorSpace::Hdr && block_compressed) {
    return PlanError::SignUnsupported;
  }
  if (plan.format == TextureFormat::BC7Unorm && plan.color_space == ColorSpace::Srgb) {
    plan.format = TextureFormat::BC7UnormSrgb;
  }
  if (plan.half_texels &&
      static_cast<std::uint64_t>(png_width) != 2U * static_cast<std::uint64_t>(descriptor.width)) {
    return PlanError::HalfWidthMismatch;
  }
  plan.width = plan.half_texels ? png_width / 2U : png_width;
  plan.height = png_height;
  if (plan.width == 0U || plan.height == 0U || plan.width > rendering::kMaxTextureExtent ||
      plan.height > rendering::kMaxTextureExtent ||
      (descriptor.dimension == kDimensionCube && plan.width != plan.height)) {
    return PlanError::ExtentInvalid;
  }
  plan.mip_levels = rendering::full_mip_count({plan.width, plan.height});
  out = plan;
  return PlanError::None;
}

Half4Image expand_half_texels(const Rgba8Image& pairs) {
  Half4Image out;
  out.width = pairs.width / 2U;
  out.height = pairs.height;
  out.texels.resize(static_cast<std::size_t>(out.width) * out.height * 4U);
  const auto pixel = [&](std::size_t x, std::size_t y, std::size_t channel) -> std::uint32_t {
    return pairs.pixels[(((y * pairs.width) + x) * 4U) + channel];
  };
  for (std::size_t y = 0; y < out.height; ++y) {
    for (std::size_t x = 0; x < out.width; ++x) {
      const std::size_t even = 2U * x;
      const std::size_t odd = even + 1U;
      const std::size_t at = ((y * out.width) + x) * 4U;
      out.texels[at + 0] =
          static_cast<std::uint16_t>(pixel(even, y, 0) | (pixel(even, y, 1) << 8U));
      out.texels[at + 1] =
          static_cast<std::uint16_t>(pixel(even, y, 2) | (pixel(even, y, 3) << 8U));
      out.texels[at + 2] = static_cast<std::uint16_t>(pixel(odd, y, 2) | (pixel(odd, y, 1) << 8U));
      out.texels[at + 3] = static_cast<std::uint16_t>(pixel(odd, y, 0) | (pixel(odd, y, 3) << 8U));
    }
  }
  return out;
}

std::vector<Rgba8Image> generate_mips(const Rgba8Image& base, ColorSpace color_space) {
  std::vector<Rgba8Image> chain;
  const std::uint32_t levels = rendering::full_mip_count({base.width, base.height});
  chain.reserve(levels);
  chain.push_back(base);
  const SrgbTables& srgb = srgb_tables();
  const bool gamma = color_space == ColorSpace::Srgb;
  for (std::uint32_t level = 1; level < levels; ++level) {
    const Rgba8Image& src = chain.back();
    Rgba8Image dst;
    dst.width = std::max(1U, src.width >> 1U);
    dst.height = std::max(1U, src.height >> 1U);
    dst.pixels.resize(static_cast<std::size_t>(dst.width) * dst.height * 4U);
    for (std::size_t y = 0; y < dst.height; ++y) {
      const std::size_t y0 = std::min<std::size_t>(2U * y, src.height - 1U);
      const std::size_t y1 = std::min<std::size_t>((2U * y) + 1U, src.height - 1U);
      for (std::size_t x = 0; x < dst.width; ++x) {
        const std::size_t x0 = std::min<std::size_t>(2U * x, src.width - 1U);
        const std::size_t x1 = std::min<std::size_t>((2U * x) + 1U, src.width - 1U);
        const std::array<std::size_t, 4> at{
            ((y0 * src.width) + x0) * 4U, ((y0 * src.width) + x1) * 4U,
            ((y1 * src.width) + x0) * 4U, ((y1 * src.width) + x1) * 4U};
        const std::size_t out_at = ((y * dst.width) + x) * 4U;
        for (std::size_t c = 0; c < 4; ++c) {
          if (gamma && c < 3U) {
            float sum = srgb.to_linear[src.pixels[at[0] + c]];
            sum += srgb.to_linear[src.pixels[at[1] + c]];
            sum += srgb.to_linear[src.pixels[at[2] + c]];
            sum += srgb.to_linear[src.pixels[at[3] + c]];
            dst.pixels[out_at + c] = linear_to_srgb(srgb, sum * 0.25F);
          } else {
            const std::uint32_t sum = static_cast<std::uint32_t>(src.pixels[at[0] + c]) +
                                      src.pixels[at[1] + c] + src.pixels[at[2] + c] +
                                      src.pixels[at[3] + c];
            dst.pixels[out_at + c] = static_cast<std::uint8_t>((sum + 2U) / 4U);
          }
        }
      }
    }
    chain.push_back(std::move(dst));
  }
  return chain;
}

std::vector<Half4Image> generate_half_mips(const Half4Image& base) {
  std::vector<Half4Image> chain;
  const std::uint32_t levels = rendering::full_mip_count({base.width, base.height});
  chain.reserve(levels);
  chain.push_back(base);
  for (std::uint32_t level = 1; level < levels; ++level) {
    const Half4Image& src = chain.back();
    Half4Image dst;
    dst.width = std::max(1U, src.width >> 1U);
    dst.height = std::max(1U, src.height >> 1U);
    dst.texels.resize(static_cast<std::size_t>(dst.width) * dst.height * 4U);
    for (std::size_t y = 0; y < dst.height; ++y) {
      const std::size_t y0 = std::min<std::size_t>(2U * y, src.height - 1U);
      const std::size_t y1 = std::min<std::size_t>((2U * y) + 1U, src.height - 1U);
      for (std::size_t x = 0; x < dst.width; ++x) {
        const std::size_t x0 = std::min<std::size_t>(2U * x, src.width - 1U);
        const std::size_t x1 = std::min<std::size_t>((2U * x) + 1U, src.width - 1U);
        const std::array<std::size_t, 4> at{
            ((y0 * src.width) + x0) * 4U, ((y0 * src.width) + x1) * 4U,
            ((y1 * src.width) + x0) * 4U, ((y1 * src.width) + x1) * 4U};
        const std::size_t out_at = ((y * dst.width) + x) * 4U;
        for (std::size_t c = 0; c < 4; ++c) {
          float sum = half_to_float(src.texels[at[0] + c]);
          sum += half_to_float(src.texels[at[1] + c]);
          sum += half_to_float(src.texels[at[2] + c]);
          sum += half_to_float(src.texels[at[3] + c]);
          dst.texels[out_at + c] = float_to_half(sum * 0.25F);
        }
      }
    }
    chain.push_back(std::move(dst));
  }
  return chain;
}

EncodeError encode_blocks(const Rgba8Image& mip, TextureFormat format,
                          const TexturePipelineOptions& options, std::vector<std::byte>& out) {
  out.clear();
  const bool bc7 = is_bc7(format);
  if (!bc7 && format != TextureFormat::BC5Unorm && format != TextureFormat::BC4Unorm) {
    return EncodeError::FormatNotBlockCompressed;
  }
  if (bc7 && options.bc7_uber_level > BC7ENC_MAX_UBER_LEVEL) {
    return EncodeError::UberLevelOutOfRange;
  }
  init_encoders();
  const rhi::Format rhi_format = rendering::to_rhi_format(format);
  const rhi::FormatInfo info = rhi::format_info(rhi_format);
  out.resize(static_cast<std::size_t>(rhi::slice_bytes(rhi_format, {mip.width, mip.height})));
  bc7enc_compress_block_params params{};
  bc7enc_compress_block_params_init(&params);
  bc7enc_compress_block_params_init_linear_weights(&params);
  params.m_mode_mask = kBc7ModeMask;
  params.m_max_partitions = BC7ENC_MAX_PARTITIONS;
  params.m_uber_level = options.bc7_uber_level;
  const std::size_t blocks_x = (mip.width + 3U) / 4U;
  const std::size_t blocks_y = (mip.height + 3U) / 4U;
  for (std::size_t by = 0; by < blocks_y; ++by) {
    for (std::size_t bx = 0; bx < blocks_x; ++bx) {
      alignas(4) std::array<std::uint8_t, kBlockPixels * 4U> block{};
      for (std::size_t py = 0; py < 4; ++py) {
        const std::size_t y = std::min<std::size_t>((by * 4U) + py, mip.height - 1U);
        for (std::size_t px = 0; px < 4; ++px) {
          const std::size_t x = std::min<std::size_t>((bx * 4U) + px, mip.width - 1U);
          const std::size_t from = ((y * mip.width) + x) * 4U;
          std::copy_n(mip.pixels.begin() + static_cast<std::ptrdiff_t>(from), 4,
                      block.begin() + static_cast<std::ptrdiff_t>(((py * 4U) + px) * 4U));
        }
      }
      std::byte* dst = out.data() + (((by * blocks_x) + bx) * info.block_bytes);
      if (bc7) {
        bc7enc_compress_block(dst, block.data(), &params);
      } else if (format == TextureFormat::BC5Unorm) {
        rgbcx::encode_bc5(dst, block.data(), 0, 1, 4);
      } else {
        rgbcx::encode_bc4(dst, block.data(), 4);
      }
    }
  }
  return EncodeError::None;
}

TextureConvertFailure convert_texture(std::span<const std::byte> descriptor_bytes,
                                      std::span<const std::span<const std::byte>> pngs,
                                      const TexturePipelineOptions& options,
                                      TextureAssetData& out) {
  out = TextureAssetData{};
  TextureConvertFailure failure;
  TextureDescriptor descriptor;
  failure.descriptor = decode_descriptor(descriptor_bytes, descriptor);
  if (failure.descriptor != DescriptorError::None) {
    return failure;
  }
  const bool cube = descriptor.dimension == kDimensionCube;
  const std::size_t faces = cube ? kCubeFaceOrder.size() : 1U;
  if (pngs.size() != faces) {
    failure.plan = PlanError::ImageCountInvalid;
    return failure;
  }
  std::vector<Rgba8Image> images(faces);
  for (std::size_t f = 0; f < faces; ++f) {
    failure.png = decode_png(pngs[f], images[f]);
    if (!failure.png.ok()) {
      failure.face = static_cast<std::uint32_t>(f);
      return failure;
    }
  }
  TexturePlan plan;
  for (std::size_t f = 0; f < faces; ++f) {
    TexturePlan face_plan;
    failure.plan = plan_texture(descriptor, images[f].width, images[f].height, face_plan);
    // Each cube face has face 0's extent (the header).
    if (failure.plan == PlanError::None && f != 0U &&
        (face_plan.width != plan.width || face_plan.height != plan.height)) {
      failure.plan = PlanError::ExtentInvalid;
    }
    if (failure.plan != PlanError::None) {
      failure.face = static_cast<std::uint32_t>(f);
      return failure;
    }
    if (f == 0U) {
      plan = face_plan;
    }
  }
  out.width = plan.width;
  out.height = plan.height;
  out.faces = static_cast<std::uint32_t>(faces);
  out.mip_levels = plan.mip_levels;
  out.format = plan.format;
  out.dimension =
      cube ? rendering::TextureAssetDimension::Cube : rendering::TextureAssetDimension::Texture2D;
  out.flags = plan.flags;
  out.subresources.resize(faces * plan.mip_levels);
  const rhi::Format rhi_format = rendering::to_rhi_format(plan.format);
  for (std::size_t f = 0; f < faces; ++f) {
    const std::size_t slice = cube ? kCubeFaceOrder[f] : 0U;
    if (plan.half_texels) {
      const std::vector<Half4Image> chain = generate_half_mips(expand_half_texels(images[f]));
      for (std::size_t level = 0; level < chain.size(); ++level) {
        SubresourceData& sub = out.subresources[(slice * plan.mip_levels) + level];
        sub.row_pitch = rhi::row_bytes(rhi_format, chain[level].width);
        sub.bytes.resize(chain[level].texels.size() * 2U);
        for (std::size_t i = 0; i < chain[level].texels.size(); ++i) {
          sub.bytes[2U * i] = static_cast<std::byte>(chain[level].texels[i] & 0xFFU);
          sub.bytes[(2U * i) + 1U] = static_cast<std::byte>(chain[level].texels[i] >> 8U);
        }
      }
      continue;
    }
    Rgba8Image& base = images[f];
    if ((plan.flags & rendering::kTwoChannelNormal) != 0U &&
        options.normal_green == NormalMapConvention::FlipGreen) {
      for (std::size_t i = 1; i < base.pixels.size(); i += 4U) {
        base.pixels[i] = static_cast<std::uint8_t>(255U - base.pixels[i]);
      }
    }
    const std::vector<Rgba8Image> chain = generate_mips(base, plan.color_space);
    for (std::size_t level = 0; level < chain.size(); ++level) {
      SubresourceData& sub = out.subresources[(slice * plan.mip_levels) + level];
      sub.row_pitch = rhi::row_bytes(rhi_format, chain[level].width);
      failure.encode = encode_blocks(chain[level], plan.format, options, sub.bytes);
      if (failure.encode != EncodeError::None) {
        failure.face = static_cast<std::uint32_t>(f);
        out = TextureAssetData{};
        return failure;
      }
    }
  }
  return failure;
}

} // namespace bw::content_converter
