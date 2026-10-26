// Implements the immutable CPU staging contract in texture_mips.hpp. Validate
// before allocating/publishing, retain base bytes, build each level from that
// base, and quantize to its original staging precision. Separable coefficients
// and scratch rows are domain owned; no graphics/settings state is borrowed.
// O(base pixels * levels) work, O(reduced width * base height)
// scratch-equivalent storage plus coefficients/output. No hidden global
// allocation or recursion. Both phases: Core SL.con.3 (#rsl-bounds) bounds all
// image/table access; Performance MEM.6 (guidelines/memory/
// MEM.6-inject-allocators-behind-one-interface.md) injects all buffers.
// Original numerical dispatch/dither evidence: texture-mip-filter.md.
#include "bw/content/formats/texture_mips.hpp"

#include <algorithm>
#include <array>
#include <cmath>

#include "bw/content/formats/binary_reader.hpp"
#include "bw/math/half.hpp"

namespace bw::formats {
namespace {
constexpr std::size_t kChannels = 4;
constexpr std::size_t kHalfBytes = 2;
constexpr std::uint32_t kCubeFaces = 6;
constexpr float kByteMaximum = 255.0F;
constexpr unsigned kNibbleMaximum = 15;
constexpr unsigned kNibbleExpansion = 17;
constexpr double kHalf = 0.5;
constexpr std::array<unsigned, 16> kDitherOrder{0, 8,  2, 10, 12, 4, 14, 6,
                                                3, 11, 1, 9,  15, 7, 13, 5};
using Pixel = std::array<float, kChannels>;
struct Tap {
  std::uint32_t source{};
  float weight{};
};
struct Axis {
  memory::Vector<std::size_t> offsets;
  memory::Vector<Tap> taps;
};
std::uint32_t reduced(std::uint32_t extent) {
  return std::max(1U, extent / 2U);
}
std::size_t pixel_bytes(WorldTextureFormat format) {
  switch (format) {
    case WorldTextureFormat::rgba8:
    case WorldTextureFormat::rgba4:
      return kChannels;
    case WorldTextureFormat::rgba16_float:
      return kChannels * kHalfBytes;
  }
  resources::detail::unsupported("Unknown mip staging format");
}
std::uint32_t validate(const WorldTextureImage& base) {
  if (base.width == 0 || base.height == 0 ||
      base.width > kMaximumImageDimension ||
      base.height > kMaximumImageDimension || base.mip_levels != 1) {
    resources::detail::malformed("Invalid mip base dimensions or level count");
  }
  std::uint32_t faces = 1;
  switch (base.shape) {
    case WorldTextureShape::two_dimensional:
      break;
    case WorldTextureShape::cube:
      if (base.width != base.height) {
        resources::detail::malformed("Mip cube faces must be square");
      }
      faces = kCubeFaces;
      break;
    default:
      resources::detail::unsupported("Unknown mip texture shape");
  }
  const auto bytes = static_cast<std::size_t>(base.width) * base.height *
                     pixel_bytes(base.format) * faces;
  if (bytes != base.pixels.size() || bytes > kImageByteBudget) {
    resources::detail::malformed("Mip base byte extent exceeds its contract");
  }
  if (base.format == WorldTextureFormat::rgba4 &&
      std::ranges::any_of(base.pixels, [](std::uint8_t value) {
        return value % kNibbleExpansion != 0;
      })) {
    resources::detail::malformed("Four-bit staging contains unquantized bytes");
  }
  return faces;
}
// Integral of the unit triangular reconstruction kernel. Keeping the analytic
// support avoids approximating a footprint by a chosen number of samples.
double triangle_integral(double coordinate) {
  if (coordinate <= -1.0) {
    return 0.0;
  }
  if (coordinate < 0.0) {
    const double shifted = coordinate + 1.0;
    return kHalf * shifted * shifted;
  }
  if (coordinate < 1.0) {
    const double shifted = 1.0 - coordinate;
    return 1.0 - kHalf * shifted * shifted;
  }
  return 1.0;
}
Axis axis(std::uint32_t source, std::uint32_t destination, bool box,
          const memory::Allocator<std::uint8_t>& allocator) {
  Axis result{memory::Vector<std::size_t>(allocator),
              memory::Vector<Tap>(allocator)};
  result.offsets.reserve(static_cast<std::size_t>(destination) + 1);
  result.taps.reserve(static_cast<std::size_t>(source) + 2ULL * destination);
  const double scale = static_cast<double>(source) / destination;
  for (std::uint32_t output = 0; output < destination; ++output) {
    result.offsets.push_back(result.taps.size());
    if (box) {
      const auto count = std::min(2U, source);
      for (std::uint32_t sample = 0; sample < count; ++sample) {
        result.taps.push_back(
            {output * count + sample, 1.0F / static_cast<float>(count)});
      }
      continue;
    }
    const double low = static_cast<double>(output) * scale;
    const double high = static_cast<double>(output + 1U) * scale;
    const auto begin = static_cast<int>(std::floor(low - kHalf));
    const auto end = static_cast<int>(std::ceil(high + kHalf));
    for (int sample = begin; sample < end; ++sample) {
      const double center = static_cast<double>(sample) + kHalf;
      const auto weight = static_cast<float>(
          (triangle_integral(high - center) - triangle_integral(low - center)) /
          scale);
      if (weight == 0.0F) {
        continue;
      }
      const auto wrapped = static_cast<std::uint32_t>(
          (sample + static_cast<int>(source)) % static_cast<int>(source));
      result.taps.push_back({wrapped, weight});
    }
  }
  result.offsets.push_back(result.taps.size());
  return result;
}
float component(std::span<const std::uint8_t> bytes, std::size_t index,
                WorldTextureFormat format) {
  if (format != WorldTextureFormat::rgba16_float) {
    if (format == WorldTextureFormat::rgba4) {
      return static_cast<float>(bytes[index] >> 4U) *
             (1.0F / static_cast<float>(kNibbleMaximum));
    }
    return static_cast<float>(bytes[index]) * (1.0F / kByteMaximum);
  }
  constexpr unsigned kByteBits = 8;
  const auto offset = index * kHalfBytes;
  const auto bits = static_cast<std::uint16_t>(
      static_cast<unsigned>(bytes[offset]) |
      (static_cast<unsigned>(bytes[offset + 1]) << kByteBits));
  return math::finite_half_to_float(bits);
}
float dither(std::uint32_t x, std::uint32_t y, std::uint32_t width) {
  constexpr unsigned kDitherSide = 4;
  constexpr float kDitherLevels = 16.0F;
  const auto column = (y % 2U == 0 ? x : width - 1U - x) % kDitherSide;
  const auto order = kDitherOrder.at((y % kDitherSide) * kDitherSide + column);
  return (kDitherLevels - static_cast<float>(kHalf) -
          static_cast<float>(order)) /
         kDitherLevels;
}
void encode(std::span<std::uint8_t> destination, std::size_t pixel,
            const Pixel& value, WorldTextureFormat format, float threshold,
            bool paired_half) {
  for (std::size_t channel = 0; channel < kChannels; ++channel) {
    const auto index = pixel * kChannels + channel;
    if (format == WorldTextureFormat::rgba16_float) {
      constexpr unsigned kByteBits = 8;
      const auto bits = math::float_to_finite_half(
          value.at(channel), paired_half ? math::FiniteHalfEncoding::packed_pair
                                         : math::FiniteHalfEncoding::scalar);
      destination[index * kHalfBytes] = static_cast<std::uint8_t>(bits);
      destination[index * kHalfBytes + 1] =
          static_cast<std::uint8_t>(bits >> kByteBits);
    } else {
      const bool four_bit = format == WorldTextureFormat::rgba4;
      const float maximum = four_bit ? kNibbleMaximum : kByteMaximum;
      const float quantized = std::floor(
          std::clamp(value.at(channel), 0.0F, 1.0F) * maximum + threshold);
      const auto integer = static_cast<unsigned>(std::min(quantized, maximum));
      destination[index] = static_cast<std::uint8_t>(
          integer * (four_bit ? kNibbleExpansion : 1U));
    }
  }
}
// Reuse each horizontal result across the vertical footprint. Faces never share
// taps, and every level points back to the immutable base face.
void filter_face(const WorldTextureImage& base,
                 std::span<const std::uint8_t> source,
                 std::span<std::uint8_t> destination, const Axis& horizontal,
                 const Axis& vertical, memory::Vector<Pixel>& scratch,
                 bool box) {
  const auto width = static_cast<std::uint32_t>(horizontal.offsets.size() - 1U);
  const auto height = static_cast<std::uint32_t>(vertical.offsets.size() - 1U);
  scratch.resize(static_cast<std::size_t>(width) * base.height);
  for (std::uint32_t y = 0; y < base.height; ++y) {
    for (std::uint32_t x = 0; x < width; ++x) {
      Pixel sum{};
      for (auto tap = horizontal.offsets[x]; tap < horizontal.offsets[x + 1];
           ++tap) {
        const auto& sample = horizontal.taps[tap];
        const auto index =
            (static_cast<std::size_t>(y) * base.width + sample.source) *
            kChannels;
        for (std::size_t channel = 0; channel < kChannels; ++channel) {
          const auto contribution =
              component(source, index + channel, base.format) * sample.weight;
          if (box && tap == horizontal.offsets[x]) {
            sum.at(channel) = contribution;
          } else {
            sum.at(channel) += contribution;
          }
        }
      }
      scratch[static_cast<std::size_t>(y) * width + x] = sum;
    }
  }
  for (std::uint32_t y = 0; y < height; ++y) {
    for (std::uint32_t x = 0; x < width; ++x) {
      Pixel sum{};
      for (auto tap = vertical.offsets[y]; tap < vertical.offsets[y + 1];
           ++tap) {
        const auto& sample = vertical.taps[tap];
        const auto& row =
            scratch[static_cast<std::size_t>(sample.source) * width + x];
        for (std::size_t channel = 0; channel < kChannels; ++channel) {
          const auto contribution = row.at(channel) * sample.weight;
          if (box && tap == vertical.offsets[y]) {
            sum.at(channel) = contribution;
          } else {
            sum.at(channel) += contribution;
          }
        }
      }
      encode(destination, static_cast<std::size_t>(y) * width + x, sum,
             base.format, dither(x, y, width), x < (width & ~1U));
    }
  }
}
}  // namespace

WorldTextureImage generate_texture_mips(
    const WorldTextureImage& base,
    const memory::Allocator<std::uint8_t>& allocator) {
  const auto faces = validate(base);
  const auto stride = pixel_bytes(base.format);
  std::size_t bytes = 0;
  std::uint32_t levels = 0;
  for (auto width = base.width, height = base.height;;
       width = reduced(width), height = reduced(height)) {
    bytes += static_cast<std::size_t>(width) * height * stride * faces;
    ++levels;
    if (width == 1 && height == 1) {
      break;
    }
  }
  WorldTextureImage result{base.width,  base.height,
                           base.format, memory::Vector<std::uint8_t>(allocator),
                           base.shape,  levels};
  result.pixels.resize(bytes);
  std::ranges::copy(base.pixels, result.pixels.begin());
  memory::Vector<Pixel> scratch(allocator);
  const auto base_face_bytes = base.pixels.size() / faces;
  std::size_t offset = base.pixels.size();
  auto width = base.width;
  auto height = base.height;
  for (std::uint32_t level = 1; level < levels; ++level) {
    width = reduced(width);
    height = reduced(height);
    const bool box =
        width == reduced(base.width) && height == reduced(base.height);
    const auto horizontal = axis(base.width, width, box, allocator);
    const auto vertical = axis(base.height, height, box, allocator);
    const auto face_bytes = static_cast<std::size_t>(width) * height * stride;
    for (std::uint32_t face = 0; face < faces; ++face) {
      filter_face(base,
                  std::span(base.pixels)
                      .subspan(face * base_face_bytes, base_face_bytes),
                  std::span(result.pixels).subspan(offset, face_bytes),
                  horizontal, vertical, scratch, box);
      offset += face_bytes;
    }
  }
  return result;
}
void quantize_texture_rgba4(WorldTextureImage& image) {
  (void)validate(image);
  if (image.format != WorldTextureFormat::rgba8) {
    resources::detail::unsupported(
        "Four-bit conversion requires RGBA8 staging");
  }
  constexpr unsigned kNearestBias = 127;
  constexpr unsigned kByteMaximumInteger = 255;
  for (auto& byte : image.pixels) {
    byte = static_cast<std::uint8_t>(
        ((static_cast<unsigned>(byte) * kNibbleMaximum + kNearestBias) /
         kByteMaximumInteger) *
        kNibbleExpansion);
  }
  image.format = WorldTextureFormat::rgba4;
}
}  // namespace bw::formats
