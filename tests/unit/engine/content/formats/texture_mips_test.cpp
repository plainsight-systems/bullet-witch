// In-process texture-chain public contracts with synthetic colors and original
// DLL-derived numerical expectations. No files/native GPU/process dependency.
// Exercise base ownership, independent full-base filtering, serpentine dither,
// precision staging, face isolation, malformed input and allocation rollback.
// Core SL.con.3 (#rsl-bounds) and Performance MEM.6 (guidelines/memory/
// MEM.6-inject-allocators-behind-one-interface.md), read during both design and
// implementation, govern fixture bounds and injected owner lifetimes.
#include "bw/content/formats/texture_mips.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>

#include "../../../../fixtures/media_memory_fixture.hpp"
#include "bw/content/formats/common.hpp"
#include "bw/math/half.hpp"

namespace {
using bw::formats::WorldTextureFormat;
using bw::formats::WorldTextureImage;
using bw::formats::WorldTextureShape;
void check(bool condition, const char* message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}
WorldTextureImage image(
    std::uint32_t width, std::uint32_t height,
    WorldTextureFormat format = WorldTextureFormat::rgba8,
    WorldTextureShape shape = WorldTextureShape::two_dimensional) {
  WorldTextureImage result{width, height, format, {}, shape};
  result.pixels.resize(static_cast<std::size_t>(width) * height *
                       (format == WorldTextureFormat::rgba16_float ? 8U : 4U) *
                       (shape == WorldTextureShape::cube ? 6U : 1U));
  return result;
}
void rejects(const WorldTextureImage& source) {
  try {
    (void)bw::formats::generate_texture_mips(source, {});
  } catch (const bw::resources::Error&) {
    return;
  }
  throw std::runtime_error("Invalid mip staging image accepted");
}
void byte_filter_contracts() {
  auto base = image(8, 8);
  std::fill_n(base.pixels.begin(), 4, 255);
  const auto chain = bw::formats::generate_texture_mips(base, {});
  check(chain.mip_levels == 4 && chain.pixels.size() == 340 &&
            std::equal(base.pixels.begin(), base.pixels.end(),
                       chain.pixels.begin()),
        "Mip result preserves exact immutable base and complete chain layout");
  check(chain.pixels[256] == 64 && chain.pixels[320] == 13 &&
            chain.pixels[324] == 2 && chain.pixels[328] == 2 &&
            chain.pixels[332] == 0 && chain.pixels[336] == 4,
        "Original full-base footprint wraps instead of recursively averaging");
  base.pixels[0] = 0;
  check(chain.pixels[0] == 255, "Generated base storage is independent");

  auto quarter = image(6, 6);
  for (std::uint32_t y = 0; y < 6; y += 2) {
    for (std::uint32_t x = 0; x < 6; x += 2) {
      for (std::size_t channel = 0; channel < 4; ++channel) {
        quarter.pixels[(static_cast<std::size_t>(y) * 6 + x) * 4 + channel] = 1;
      }
    }
  }
  const auto dithered = bw::formats::generate_texture_mips(quarter, {});
  constexpr std::array<std::uint8_t, 9> kQuarter{1, 0, 1, 0, 0, 0, 1, 0, 1};
  for (std::size_t pixel = 0; pixel < kQuarter.size(); ++pixel) {
    check(dithered.pixels[144 + pixel * 4] == kQuarter.at(pixel),
          "Eight-bit output applies original four-by-four ordered dither");
  }
  // Half a byte distinguishes the reversed threshold positions on odd rows
  // when the destination width is not a multiple of four.
  for (std::size_t pixel = 0; pixel < quarter.pixels.size(); pixel += 4) {
    const auto x = (pixel / 4) % 6;
    for (std::size_t channel = 0; channel < 4; ++channel) {
      quarter.pixels[pixel + channel] = static_cast<std::uint8_t>(x % 2 == 0);
    }
  }
  const auto half = bw::formats::generate_texture_mips(quarter, {});
  check(half.pixels[144 + 3 * 4] == 0 && half.pixels[144 + 4 * 4] == 1 &&
            half.pixels[144 + 5 * 4] == 0,
        "Odd scanlines use source serpentine dither phase");
}
void precision_contracts() {
  auto low = image(2, 2);
  constexpr std::array<std::uint8_t, 16> kInput{
      8, 9, 25, 26, 127, 128, 250, 255, 0, 17, 34, 51, 68, 85, 102, 119};
  std::ranges::copy(kInput, low.pixels.begin());
  bw::formats::quantize_texture_rgba4(low);
  constexpr std::array<std::uint8_t, 16> kExpanded{
      0, 17, 17, 34, 119, 136, 255, 255, 0, 17, 34, 51, 68, 85, 102, 119};
  check(low.format == WorldTextureFormat::rgba4 &&
            std::ranges::equal(low.pixels, kExpanded),
        "Initial four-bit staging rounds without dither and retains precision");
  const auto low_chain = bw::formats::generate_texture_mips(low, {});
  check(std::ranges::all_of(low_chain.pixels,
                            [](std::uint8_t value) { return value % 17 == 0; }),
        "Every lower four-bit level retains original precision");

  auto hdr = image(4, 2, WorldTextureFormat::rgba16_float);
  // Both output pixels land halfway between HALF codes 45a1 and 45a2.
  constexpr std::array<std::uint16_t, 2> kValues{0x4507, 0x463c};
  for (std::size_t pixel = 0; pixel < 8; ++pixel) {
    const auto bits = kValues.at(pixel % 2);
    for (std::size_t channel = 0; channel < 4; ++channel) {
      hdr.pixels[(pixel * 4 + channel) * 2] = static_cast<std::uint8_t>(bits);
      hdr.pixels[(pixel * 4 + channel) * 2 + 1] =
          static_cast<std::uint8_t>(bits >> 8U);
    }
  }
  const auto hdr_chain = bw::formats::generate_texture_mips(hdr, {});
  check(hdr_chain.pixels[64] == 0xa1 && hdr_chain.pixels[65] == 0x45 &&
            hdr_chain.pixels[72] == 0xa1 && hdr_chain.pixels[73] == 0x45,
        "Original paired HALF packing rounds a halfway magnitude downward");
  check(
      bw::math::float_to_finite_half(5.630859375F) == 0x45a2 &&
          bw::math::float_to_finite_half(65536.0F) == 0x7c00 &&
          bw::math::float_to_finite_half(131008.0F) == 0x7fff &&
          bw::math::float_to_finite_half(
              100000.0F, bw::math::FiniteHalfEncoding::packed_pair) == 0x7c00 &&
          bw::math::float_to_finite_half(-0.0F) == 0x8000 &&
          bw::math::float_to_finite_half(std::ldexp(1.0F, -24)) == 1,
      "Scalar finite-HALF encoding retains even ties, extended range and "
      "zero sign");
}
void shape_and_owner_contracts() {
  auto cube = image(4, 4, WorldTextureFormat::rgba8, WorldTextureShape::cube);
  for (std::size_t face = 0; face < 6; ++face) {
    std::fill_n(cube.pixels.begin() + static_cast<std::ptrdiff_t>(face * 64),
                64, static_cast<std::uint8_t>(face * 40));
  }
  const auto chain = bw::formats::generate_texture_mips(cube, {});
  check(chain.mip_levels == 3 && chain.pixels.size() == 504,
        "Cube chain accounts all six faces at every level");
  for (std::size_t face = 0; face < 6; ++face) {
    check(chain.pixels[384 + face * 16] == face * 40 &&
              chain.pixels[480 + face * 4] == face * 40,
          "Mip cube levels retain face order without cross-face taps");
  }
  auto invalid = image(2, 2);
  invalid.pixels.pop_back();
  rejects(invalid);
  invalid = image(2, 2);
  invalid.mip_levels = 2;
  rejects(invalid);
  invalid = image(3, 2, WorldTextureFormat::rgba8, WorldTextureShape::cube);
  rejects(invalid);
  invalid = image(2, 2, WorldTextureFormat::rgba4);
  invalid.pixels[0] = 1;
  rejects(invalid);
  invalid = image(1, 1);
  invalid.width = 0;
  rejects(invalid);

  const auto backing = std::make_shared<bw::test::MediaBacking>();
  const auto domain = bw::memory::Domain::create(backing);
  {
    const auto owned = bw::formats::generate_texture_mips(
        cube, bw::memory::Allocator<std::uint8_t>(domain));
    check(domain->snapshot().live_bytes == owned.pixels.size(),
          "Temporary filter buffers are released before chain publication");
  }
  check(domain->snapshot().live_allocations == 0 &&
            backing->acquired == backing->released,
        "Generated chain releases all domain allocations");
  const auto limited =
      bw::memory::Domain::create(backing, {.max_live_bytes = 1});
  bool rejected = false;
  try {
    (void)bw::formats::generate_texture_mips(
        cube, bw::memory::Allocator<std::uint8_t>(limited));
  } catch (const std::bad_alloc&) {
    rejected = true;
  }
  check(rejected && limited->snapshot().live_allocations == 0 &&
            cube.pixels.size() == 384,
        "Budget failure publishes nothing and preserves source ownership");
}
}  // namespace
void texture_mip_contracts() {
  byte_filter_contracts();
  precision_contracts();
  shape_and_owner_contracts();
}
