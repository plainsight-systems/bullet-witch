// Unit test: public contracts of bw/content_converter/texture_reader.hpp;
// each scenario names the declaration it asserts. Block encodes are checked
// against the vendored reference decoders, which the test links as a
// library.
#include "bw/content_converter/asset_data.hpp"
#include "bw/content_converter/texture_reader.hpp"
#include "bw/rendering/rhi/format.hpp"
#include "bw/rendering/texture_asset.hpp"
#include "bw/testing/harness.hpp"

#include <bc7decomp.h>
#include <rgbcx.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <initializer_list>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

namespace cc = bw::content_converter;
namespace r = bw::rendering;
using bw::testing::require;
using Bytes = std::vector<std::byte>;

Bytes words(std::initializer_list<std::uint32_t> values) {
  Bytes bytes;
  for (const std::uint32_t v : values) {
    for (std::size_t i = 0; i < 4; ++i) {
      bytes.push_back(static_cast<std::byte>((v >> (8U * i)) & 0xFFU));
    }
  }
  return bytes;
}

// (texture_reader.hpp, decode_descriptor, TextureDescriptor::sign_bits)
void descriptor_fields() {
  const Bytes record9 = words({3, 1, 0, 0, 0, 0xFFFF0000U, 0xFFFF0000U, 0x840000FEU, 0x0025C052U,
                               0x003FE1FFU, 0x00000D10U, 0x00000240U, 0x0027CA00U});
  cc::TextureDescriptor d;
  require(cc::decode_descriptor(record9, d) == cc::DescriptorError::None, "52 bytes decode");
  require(d.sign == std::array<std::uint32_t, 4>{3, 3, 3, 0} && d.sign_bits() == 63U,
          "sign x, y, z gamma, w unsigned: 63");
  require(d.pitch == 16U && d.tiled, "pitch and tiled");
  require(d.format == cc::kFormatDxt1 && d.endianness == 1U && d.base_address == 0x25CU,
          "format, endianness, base address");
  require(d.width == 512U && d.height == 512U && d.depth == 1U, "extents are stored minus one");
  require(d.num_format == 0U && d.mip_max_level == 9U, "num_format and mip_max_level");
  require(d.dimension == cc::kDimension2D && d.packed_mips && d.mip_address == 0x27CU,
          "dimension, packed mips, mip address");

  const Bytes cube = words({3, 1, 0, 0, 0, 0xFFFF0000U, 0xFFFF0000U, 0x80800156U, 0x0000005DU,
                            0x1407E03FU, 0x00000D11U, 0x00000180U, 0x00030E00U});
  require(cc::decode_descriptor(cube, d) == cc::DescriptorError::None, "the cube decodes");
  require(d.sign_bits() == 85U && d.format == cc::kFormat16161616Expand && d.width == 64U &&
              d.height == 64U && d.depth == 6U && d.num_format == 1U && d.mip_max_level == 6U &&
              d.dimension == cc::kDimensionCube,
          "the signed cube's fields");

  for (const std::size_t size : {std::size_t{51}, std::size_t{53}, std::size_t{0}}) {
    const Bytes wrong(size);
    require(cc::decode_descriptor(wrong, d) == cc::DescriptorError::DescriptorSizeInvalid,
            "a payload of another size");
  }
}

struct DescriptorSpec {
  std::uint32_t format;
  std::uint32_t sign_bits;
  std::uint32_t dimension;
  std::uint32_t width;
};
cc::TextureDescriptor descriptor(const DescriptorSpec& spec) {
  cc::TextureDescriptor d{};
  for (std::uint32_t i = 0; i < 4; ++i) {
    d.sign[i] = (spec.sign_bits >> (2U * i)) & 3U;
  }
  d.format = spec.format;
  d.dimension = spec.dimension;
  d.width = spec.width;
  d.height = spec.width;
  return d;
}

// (texture_reader.hpp, plan_texture: the color space, format, flags and
// extent per descriptor; PlanError in the stated order, format, then sign,
// then the half width, then the extent; a dimension other than 2D or cube
// FormatUnsupported; sign 85 only with 16_16_16_16_EXPAND)
void plan_table() {
  using F = r::TextureFormat;
  using P = cc::PlanError;
  using C = cc::ColorSpace;
  struct Row {
    std::string_view name;
    cc::TextureDescriptor d;
    std::uint32_t png_w;
    std::uint32_t png_h;
    P error;
    C space;
    F format;
    std::uint32_t flags;
    bool half;
    std::uint32_t w;
    std::uint32_t h;
    std::uint32_t mips;
  };
  const std::uint32_t n = r::kTwoChannelNormal;
  const std::array<Row, 14> rows{{
      {"DXT1 gamma", descriptor({cc::kFormatDxt1, 63, 1, 512}), 512, 512, P::None, C::Srgb,
       F::BC7UnormSrgb, 0, false, 512, 512, 10},
      {"DXT5 unsigned, non-power-of-two", descriptor({cc::kFormatDxt4_5, 0, 1, 420}), 420, 270,
       P::None, C::Linear, F::BC7Unorm, 0, false, 420, 270, 9},
      {"DXT2_3", descriptor({cc::kFormatDxt2_3, 63, 1, 128}), 128, 64, P::None, C::Srgb,
       F::BC7UnormSrgb, 0, false, 128, 64, 8},
      {"8_8_8_8", descriptor({cc::kFormat8888, 0, 1, 256}), 256, 256, P::None, C::Linear,
       F::BC7Unorm, 0, false, 256, 256, 9},
      {"4_4_4_4: the PNG's size wins", descriptor({cc::kFormat4444, 0, 1, 2048}), 1024, 1024,
       P::None, C::Linear, F::BC7Unorm, 0, false, 1024, 1024, 11},
      {"DXN", descriptor({cc::kFormatDxn, 0, 1, 64}), 64, 64, P::None, C::Linear, F::BC5Unorm, n,
       false, 64, 64, 7},
      {"CTX1 gamma", descriptor({cc::kFormatCtx1, 63, 1, 64}), 64, 32, P::None, C::Srgb,
       F::BC5Unorm, n, false, 64, 32, 7},
      {"HDR 2D", descriptor({cc::kFormat16161616Expand, 85, 1, 128}), 256, 128, P::None, C::Hdr,
       F::R16G16B16A16Float, 0, true, 128, 128, 8},
      {"HDR cube face", descriptor({cc::kFormat16161616Expand, 85, 3, 64}), 128, 64, P::None,
       C::Hdr, F::R16G16B16A16Float, 0, true, 64, 64, 7},
      {"halfs flagged unsigned", descriptor({cc::kFormat16161616Expand, 0, 1, 8}), 16, 8, P::None,
       C::Linear, F::R16G16B16A16Float, 0, true, 8, 8, 4},
      {"unknown format",
       descriptor({7, 63, 1, 64}),
       64,
       64,
       P::FormatUnsupported,
       {},
       {},
       0,
       false,
       0,
       0,
       0},
      // A dimension other than 2D or cube is FormatUnsupported.
      {"unknown sign",
       descriptor({cc::kFormatDxt1, 21, 1, 64}),
       64,
       64,
       P::SignUnsupported,
       {},
       {},
       0,
       false,
       0,
       0,
       0},
      // Sign 85 on a format other than 16_16_16_16_EXPAND is SignUnsupported.
      {"half width",
       descriptor({cc::kFormat16161616Expand, 85, 1, 128}),
       250,
       128,
       P::HalfWidthMismatch,
       {},
       {},
       0,
       false,
       0,
       0,
       0},
      {"cube not square",
       descriptor({cc::kFormatDxt1, 63, 3, 64}),
       128,
       64,
       P::ExtentInvalid,
       {},
       {},
       0,
       false,
       0,
       0,
       0},
  }};
  for (std::size_t i = 0; i < rows.size(); ++i) {
    const Row& row = rows[i];
    cc::TexturePlan plan;
    require(cc::plan_texture(row.d, row.png_w, row.png_h, plan) == row.error, i, row.name);
    if (row.error != P::None) {
      continue;
    }
    require(plan.color_space == row.space && plan.format == row.format && plan.flags == row.flags &&
                plan.half_texels == row.half && plan.width == row.w && plan.height == row.h &&
                plan.mip_levels == row.mips,
            i, row.name);
  }
}

// (texture_reader.hpp, expand_half_texels)
void half_texels() {
  cc::Rgba8Image pairs;
  pairs.width = 4;
  pairs.height = 1;
  pairs.pixels = {
      0x00, 0x3C, 0x00, 0x40, // texel 0 even: R0 G0 B0 A0
      0x11, 0x42, 0x22, 0x3C, // texel 0 odd: R1 G1 B1 A1
      0x00, 0x44, 0x01, 0xC0, // texel 1 even
      0x00, 0x00, 0x34, 0x3C, // texel 1 odd
  };
  const cc::Half4Image texels = cc::expand_half_texels(pairs);
  require(texels.width == 2U && texels.height == 1U, "half the width, the same height");
  // R = R0 | G0 << 8, G = B0 | A0 << 8, B = B1 | G1 << 8, A = R1 | A1 << 8.
  const std::vector<std::uint16_t> expected{0x3C00, 0x4000, 0x4222, 0x3C11,
                                            0x4400, 0xC001, 0x0034, 0x3C00};
  require(texels.texels == expected, "the odd pixel's R and B bytes are exchanged");
}

cc::Rgba8Image image(bw::rhi::Extent2 extent, std::initializer_list<std::uint8_t> px) {
  cc::Rgba8Image i;
  i.width = extent.width;
  i.height = extent.height;
  i.pixels = px;
  return i;
}

// (texture_reader.hpp, generate_mips, generate_half_mips)
void mip_chain() {
  // Linear: p(x, y, c) = 10x + 50y + c over 5x3.
  cc::Rgba8Image base;
  base.width = 5;
  base.height = 3;
  for (std::uint32_t y = 0; y < 3; ++y) {
    for (std::uint32_t x = 0; x < 5; ++x) {
      for (std::uint32_t c = 0; c < 4; ++c) {
        base.pixels.push_back(static_cast<std::uint8_t>((10U * x) + (50U * y) + c));
      }
    }
  }
  const std::vector<cc::Rgba8Image> chain = cc::generate_mips(base, cc::ColorSpace::Linear);
  require(chain.size() == 3U, "the full chain down to 1x1");
  require(chain[0].pixels == base.pixels, "level 0 is the image");
  require(chain[1].width == 2U && chain[1].height == 1U && chain[2].width == 1U &&
              chain[2].height == 1U,
          "odd dimensions round down");
  // Level 1 drops the fifth column and the third row.
  require(chain[1].pixels == std::vector<std::uint8_t>{30, 31, 32, 33, 50, 51, 52, 53},
          "2x2 box over the first four columns and two rows");
  require(chain[2].pixels == std::vector<std::uint8_t>{40, 41, 42, 43}, "the 1x1 level");

  // sRGB: black and white average in linear light, alpha linearly.
  const cc::Rgba8Image checker =
      image({2, 2}, {0, 0, 0, 0, 255, 255, 255, 255, 0, 0, 0, 0, 255, 255, 255, 255});
  const std::vector<cc::Rgba8Image> srgb = cc::generate_mips(checker, cc::ColorSpace::Srgb);
  require(srgb.size() == 2U && srgb[1].pixels == std::vector<std::uint8_t>{188, 188, 188, 128},
          "linear 0.5 is sRGB 188; alpha averages to 128");
  const std::vector<cc::Rgba8Image> linear = cc::generate_mips(checker, cc::ColorSpace::Linear);
  require(linear[1].pixels == std::vector<std::uint8_t>{128, 128, 128, 128},
          "the same image averaged as linear data");
  const cc::Rgba8Image grey =
      image({2, 2}, {128, 64, 200, 9, 128, 64, 200, 9, 128, 64, 200, 9, 128, 64, 200, 9});
  require(cc::generate_mips(grey, cc::ColorSpace::Srgb)[1].pixels ==
              std::vector<std::uint8_t>{128, 64, 200, 9},
          "a solid sRGB colour keeps its codes");

  // Halfs: averaged in float, rounded to nearest even.
  cc::Half4Image halfs;
  halfs.width = 2;
  halfs.height = 2;
  halfs.texels = {0x3C00, 0x3C00, 0x3C01, 0x3C00, 0x4000, 0x3C01, 0x3C02, 0x3C00,
                  0x4200, 0x3C00, 0x3C01, 0x3C00, 0x4400, 0x3C01, 0x3C02, 0x3C00};
  const std::vector<cc::Half4Image> half_chain = cc::generate_half_mips(halfs);
  require(half_chain.size() == 2U && half_chain[1].width == 1U, "the half chain");
  require(half_chain[1].texels == std::vector<std::uint16_t>{0x4100, 0x3C00, 0x3C02, 0x3C00},
          "2.5; a tie between 0x3C00 and 0x3C01 goes to 0x3C00, between 0x3C01 and 0x3C02 to "
          "0x3C02");
  cc::Half4Image row;
  row.width = 3;
  row.height = 1;
  row.texels = {0x3C00, 0, 0, 0, 0x4000, 0, 0, 0, 0x7BFF, 0, 0, 0};
  const std::vector<cc::Half4Image> row_chain = cc::generate_half_mips(row);
  require(row_chain.size() == 2U && row_chain[1].texels[0] == 0x3E00,
          "the third column is dropped: (1 + 2) / 2 = 1.5");
}

// One channel of interleaved samples: the first sample's index and the
// distance between samples.
struct Channel {
  std::size_t first;
  std::size_t stride;
};
int max_error(std::span<const std::uint8_t> a, std::span<const std::uint8_t> b, Channel channel) {
  int worst = 0;
  for (std::size_t i = channel.first; i < a.size(); i += channel.stride) {
    worst = std::max(worst, std::abs(static_cast<int>(a[i]) - static_cast<int>(b[i])));
  }
  return worst;
}

// The header's figure for encode_blocks: a block the format can hold
// exactly comes back from the vendored decoders within 2 per 8-bit channel.
// The blocks here are such blocks: a 4x4 ramp whose colours lie on one line
// through RGBA space (16 steps of t = x + 4y), opaque or with alpha on the
// same line, for BC7; a ramp of 16 values over a range of 16 for BC4 and
// BC5; solid blocks for all three.
constexpr int kBc7Tolerance = 2;
constexpr int kBc45Tolerance = 2;

// (texture_reader.hpp, encode_blocks)
void blocks_decode_back() {
  const cc::TexturePipelineOptions options{};
  cc::Rgba8Image ramp;
  ramp.width = 4;
  ramp.height = 4;
  cc::Rgba8Image alpha_ramp = ramp;
  for (std::uint32_t t = 0; t < 16; ++t) {
    const std::array<std::uint8_t, 4> px{static_cast<std::uint8_t>(8U + (3U * t)),
                                         static_cast<std::uint8_t>(200U - (2U * t)),
                                         static_cast<std::uint8_t>(100U + t), 255};
    ramp.pixels.insert(ramp.pixels.end(), px.begin(), px.end());
    const std::array<std::uint8_t, 4> pa{
        static_cast<std::uint8_t>(100U + (4U * t)), static_cast<std::uint8_t>(200U - (4U * t)),
        static_cast<std::uint8_t>(50U + (2U * t)), static_cast<std::uint8_t>(64U + (8U * t))};
    alpha_ramp.pixels.insert(alpha_ramp.pixels.end(), pa.begin(), pa.end());
  }
  for (const r::TextureFormat format :
       {r::TextureFormat::BC7Unorm, r::TextureFormat::BC7UnormSrgb}) {
    for (const cc::Rgba8Image* source : {&ramp, &alpha_ramp}) {
      Bytes out;
      require(cc::encode_blocks(*source, format, options, out) == cc::EncodeError::None,
              "BC7 encodes");
      require(out.size() == 16U, "one 16-byte block");
      std::array<bc7decomp::color_rgba, 16> decoded{};
      require(bc7decomp::unpack_bc7(out.data(), decoded.data()), "a valid BC7 block");
      std::vector<std::uint8_t> back;
      for (const bc7decomp::color_rgba& c : decoded) {
        back.insert(back.end(), {c.r, c.g, c.b, c.a});
      }
      for (std::size_t c = 0; c < 4; ++c) {
        const int error = max_error(back, source->pixels, {c, 4});
        require(error <= kBc7Tolerance, c, "BC7 within tolerance, error " + std::to_string(error));
      }
    }
  }

  cc::Rgba8Image two;
  two.width = 4;
  two.height = 4;
  for (std::uint32_t i = 0; i < 16; ++i) {
    two.pixels.insert(two.pixels.end(), {static_cast<std::uint8_t>(100U + i),
                                         static_cast<std::uint8_t>(215U - i), 7, 9});
  }
  Bytes bc5;
  require(cc::encode_blocks(two, r::TextureFormat::BC5Unorm, options, bc5) ==
                  cc::EncodeError::None &&
              bc5.size() == 16U,
          "BC5 encodes one block");
  std::vector<std::uint8_t> back5(64, 0);
  rgbcx::unpack_bc5(bc5.data(), back5.data(), 0, 1, 4);
  require(max_error(back5, two.pixels, {0, 4}) <= kBc45Tolerance &&
              max_error(back5, two.pixels, {1, 4}) <= kBc45Tolerance,
          "BC5 keeps R and G within tolerance");
  Bytes bc4;
  require(cc::encode_blocks(two, r::TextureFormat::BC4Unorm, options, bc4) ==
                  cc::EncodeError::None &&
              bc4.size() == 8U,
          "BC4 encodes one block");
  std::vector<std::uint8_t> back4(16, 0);
  rgbcx::unpack_bc4(bc4.data(), back4.data(), 1);
  std::vector<std::uint8_t> red;
  red.reserve(16);
  for (std::size_t i = 0; i < 16; ++i) {
    red.push_back(two.pixels[4U * i]);
  }
  require(max_error(back4, red, {0, 1}) <= kBc45Tolerance, "BC4 keeps R within tolerance");

  // A 5x5 image is four blocks; the partial ones repeat the last column and
  // row, so the corner block is solid and decodes exactly.
  cc::Rgba8Image five;
  five.width = 5;
  five.height = 5;
  for (std::uint32_t y = 0; y < 5; ++y) {
    for (std::uint32_t x = 0; x < 5; ++x) {
      five.pixels.insert(five.pixels.end(), {static_cast<std::uint8_t>((40U * x) + y), 0, 0, 255});
    }
  }
  Bytes edges;
  require(cc::encode_blocks(five, r::TextureFormat::BC4Unorm, options, edges) ==
                  cc::EncodeError::None &&
              edges.size() == bw::rhi::slice_bytes(bw::rhi::Format::BC4Unorm, {5, 5}),
          "whole blocks, row-major");
  std::vector<std::uint8_t> corner(16, 0);
  rgbcx::unpack_bc4(edges.data() + std::size_t{24}, corner.data(), 1);
  require(std::ranges::all_of(corner, [](std::uint8_t v) { return v == 164U; }),
          "the corner block is pixel (4, 4) clamped to every position");
  std::vector<std::uint8_t> right(16, 0);
  rgbcx::unpack_bc4(edges.data() + 8U, right.data(), 1);
  for (std::uint32_t y = 0; y < 4; ++y) {
    for (std::uint32_t x = 0; x < 4; ++x) {
      require(std::abs(static_cast<int>(right[(4U * y) + x]) - static_cast<int>(160U + y)) <=
                  kBc45Tolerance,
              (4U * y) + x, "the right block repeats column 4 across");
    }
  }

  Bytes unused;
  require(cc::encode_blocks(ramp, r::TextureFormat::R8G8B8A8Unorm, options, unused) ==
              cc::EncodeError::FormatNotBlockCompressed,
          "an uncompressed format");
  require(cc::encode_blocks(ramp, r::TextureFormat::BC1Unorm, options, unused) ==
              cc::EncodeError::FormatNotBlockCompressed,
          "a block format the pipeline does not encode");
  cc::TexturePipelineOptions too_high{};
  too_high.bc7_uber_level = 5;
  require(cc::encode_blocks(ramp, r::TextureFormat::BC7Unorm, too_high, unused) ==
              cc::EncodeError::UberLevelOutOfRange,
          "uber level above the encoder's range");
  require(cc::encode_blocks(two, r::TextureFormat::BC5Unorm, too_high, unused) ==
              cc::EncodeError::None,
          "the uber level is a BC7 setting only");
}

// A 2x2 8-bit RGB PNG (colour type 2, no alpha), pixels red, green / blue,
// (10, 20, 30). Generated once with a python3 -I script using zlib.compress
// and zlib.crc32 for the IDAT stream and the chunk CRCs; the bytes are the
// fixture, nothing regenerates them.
constexpr std::array<std::uint8_t, 76> kPng{
    0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52,
    0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x02, 0x08, 0x02, 0x00, 0x00, 0x00, 0xfd, 0xd4, 0x9a,
    0x73, 0x00, 0x00, 0x00, 0x13, 0x49, 0x44, 0x41, 0x54, 0x78, 0xda, 0x63, 0xf8, 0xcf, 0xc0, 0xc0,
    0x00, 0xc2, 0x0c, 0xff, 0xb9, 0x44, 0xe4, 0x00, 0x1a, 0x58, 0x03, 0x3a, 0xe2, 0x92, 0x6e, 0xd9,
    0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82,
};
constexpr std::size_t kIhdrData = 16;

Bytes png_bytes() {
  Bytes b;
  for (const std::uint8_t v : kPng) {
    b.push_back(static_cast<std::byte>(v));
  }
  return b;
}

// (texture_reader.hpp, decode_png: RGBA8, RGB gaining alpha 255, the CRC
// check on, PngError; convert_texture: the plan's size, format and chain,
// six cube images, ImageCountInvalid, the failing face, the green flip).
void png_decoded_and_errors() {
  const Bytes good = png_bytes();
  cc::Rgba8Image decoded;
  require(cc::decode_png(good, decoded).ok(), "the fixture decodes");
  require(decoded.width == 2U && decoded.height == 2U, "IHDR's size");
  require(decoded.pixels == std::vector<std::uint8_t>{255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255,
                                                      255, 10, 20, 30, 255},
          "tight 8-bit RGBA, RGB gaining alpha 255");

  Bytes bad_crc = good;
  bad_crc[kIhdrData + 3] = std::byte{3}; // width 3 under a CRC computed for 2
  const cc::PngError error = cc::decode_png(bad_crc, decoded);
  require(!error.ok() && !error.text.empty(), "a chunk whose CRC does not match is refused");
  require(error.code == 57U, "the decoder's CRC code: the CRC check is on");

  // convert_texture composes the steps: a DXT1 unsigned 2D record becomes a
  // BC7 texture with its chain; a cube takes six images; another count and a
  // failing face are named.
  const Bytes record = words({3, 1, 0, 0, 0, 0xFFFF0000U, 0xFFFF0000U, 0x80000002U, 0x00000052U,
                              0x00002001U, 0x00000D10U, 0x00000040U, 0x00000A00U});
  const std::array<std::span<const std::byte>, 1> one{good};
  cc::TextureAssetData texture;
  const cc::TexturePipelineOptions options{};
  require(cc::convert_texture(record, one, options, texture).ok(), "a 2D record converts");
  require(texture.width == 2U && texture.height == 2U && texture.faces == 1U &&
              texture.mip_levels == 2U && texture.format == r::TextureFormat::BC7Unorm &&
              texture.dimension == r::TextureAssetDimension::Texture2D &&
              texture.subresources.size() == 2U && texture.subresources[0].bytes.size() == 16U &&
              texture.subresources[0].row_pitch == 16U,
          "the plan's size, format and full chain");
  cc::EncodeFailure failure;
  require(!cc::encode_texture_asset(texture, failure).empty() && failure.ok(),
          "the converted texture encodes and decodes");
  const Bytes colour_level0 = texture.subresources[0].bytes;

  Bytes cube_record = record;
  // Word 9 depth-1 = 5, word 12 dimension = 3.
  cube_record[(9U * 4U) + 3U] = std::byte{0x14};
  cube_record[(12U * 4U) + 1U] = std::byte{0x0E};
  const std::array<std::span<const std::byte>, 6> six{good, good, good, good, good, good};
  require(cc::convert_texture(cube_record, six, options, texture).ok(), "a cube converts");
  require(texture.faces == 6U && texture.dimension == r::TextureAssetDimension::Cube &&
              texture.subresources.size() == 12U &&
              !cc::encode_texture_asset(texture, failure).empty(),
          "six faces, slice-major");
  require(cc::convert_texture(cube_record, one, options, texture).plan ==
              cc::PlanError::ImageCountInvalid,
          "a cube with one image");
  require(cc::convert_texture(record, six, options, texture).plan ==
              cc::PlanError::ImageCountInvalid,
          "a 2D record with six images");

  std::array<std::span<const std::byte>, 6> broken = six;
  broken[3] = bad_crc;
  const cc::TextureConvertFailure face = cc::convert_texture(cube_record, broken, options, texture);
  require(face.png.code == 57U && face.face == 3U, "the failing face is named");

  // The green flip applies to two-channel maps only: a DXN record's G is
  // 255 - G before encoding (pixel 0's G of 0 decodes high), and a colour
  // record's bytes do not change.
  Bytes dxn = record;
  dxn[std::size_t{32}] = std::byte{0x71}; // word 8: format 49, endianness 1
  cc::TexturePipelineOptions flip{};
  flip.normal_green = cc::NormalMapConvention::FlipGreen;
  cc::TextureAssetData stored;
  cc::TextureAssetData flipped;
  require(cc::convert_texture(dxn, one, options, stored).ok() &&
              cc::convert_texture(dxn, one, flip, flipped).ok() &&
              flipped.format == r::TextureFormat::BC5Unorm && flipped.flags == r::kTwoChannelNormal,
          "a two-channel map converts to BC5");
  std::vector<std::uint8_t> as_stored(64, 0);
  std::vector<std::uint8_t> as_flipped(64, 0);
  rgbcx::unpack_bc5(stored.subresources[0].bytes.data(), as_stored.data(), 0, 1, 4);
  rgbcx::unpack_bc5(flipped.subresources[0].bytes.data(), as_flipped.data(), 0, 1, 4);
  require(as_stored[1] < 64U && as_flipped[1] > 191U && as_stored[0] == as_flipped[0],
          "G flipped, R untouched");
  cc::TextureAssetData colour_flipped;
  require(cc::convert_texture(record, one, flip, colour_flipped).ok() &&
              colour_flipped.subresources[0].bytes == colour_level0,
          "a colour map ignores the flip");
}

constexpr std::array<bw::testing::Scenario<void>, 6> kScenarios{{
    {"descriptor_fields", descriptor_fields},
    {"plan_table", plan_table},
    {"half_texels", half_texels},
    {"mip_chain", mip_chain},
    {"blocks_decode_back", blocks_decode_back},
    {"png_decoded_and_errors", png_decoded_and_errors},
}};

} // namespace

int main(int argc, char** argv) { return bw::testing::run_scenario(argc, argv, kScenarios); }
