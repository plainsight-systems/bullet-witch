// Source staging now includes NONE-filter zero padding for odd dimensions and
// no-dither nearest RGBA4 quantization. Temporary storage uses the supplied
// domain; publish after complete conversion. E.6/I.11 and COPY.5 (references
// in texture_mips.hpp and texture-mip-filter.md) were read in both phases.
// World texture pixel conversion contract in world_texture.hpp; native source
// The original HDR texture constructor selects HALF4 storage and copies
// bytes from an A8R8G8B8 surface of twice the declared width. Preserve bit
// patterns through in-place RGBA-to-BGRA byte ordering, not normalized color
// conversion. Bounds and unsupported color/filter cases are checked before
// publication; all provisional storage unwinds on errors. Cost
// O(encoded+pixels), one pixel allocation plus libpng's external internal
// state. Both phases consulted SL.con.3 (#rsl-bounds), R.2 (#rr-use-ptr), MEM.6
// (guidelines/memory/ MEM.6-inject-allocators-behind-one-interface.md).
// Research source comparison and evidence: stage-content-decoding.md. No
// guessed mip filtering is implemented here; texture_mips owns filtering.
#include "bw/formats/world_texture.hpp"

#include <algorithm>

#include "bw/formats/texture_mips.hpp"
#include "reader.hpp"
namespace bw::formats {
namespace {
constexpr std::size_t kFormatOffset = 32;
constexpr std::size_t kSizeOffset = 36;
constexpr std::uint32_t kFormatMask = 63;
constexpr std::uint32_t kHalfFormat = 29;
constexpr std::uint32_t kPackedFourBitFormat = 15;
constexpr std::uint32_t kDimensionMask = 8191;
constexpr unsigned kHeightShift = 13;
std::uint32_t big_word(const resources::detail::Reader& reader,
                       std::uint64_t offset) {
  reader.range(offset, 4);
  const auto bytes = reader.b.subspan(static_cast<std::size_t>(offset), 4);
  constexpr unsigned kByteBits = 8;
  std::uint32_t value = 0;
  for (auto byte : bytes) {
    value = (value << kByteBits) | byte;
  }
  return value;
}
void packed_png_contract(std::span<const std::uint8_t> encoded) {
  const resources::detail::Reader png{encoded};
  constexpr std::size_t kPngHeaderBytes = 33;
  constexpr std::uint8_t kEightBitDepth = 8;
  constexpr std::uint8_t kRgbaColorType = 6;
  constexpr std::size_t kPngSignatureBytes = 8;
  constexpr std::size_t kChunkTypeAndCrcBytes = 8;
  constexpr std::size_t kChunkEnvelopeBytes = 12;
  png.range(0, kPngHeaderBytes);
  constexpr std::size_t kBitDepthOffset = 24;
  constexpr std::size_t kColorTypeOffset = 25;
  if (png.b[kBitDepthOffset] != kEightBitDepth ||
      png.b[kColorTypeOffset] != kRgbaColorType) {
    resources::detail::unsupported(
        "Packed HALF4 requires eight-bit RGBA source bytes");
  }
  // The actual packed assets have no gamma/profile transforms. Refuse inputs
  // that could cause the shared PNG decoder to alter the half-word bytes.
  std::uint64_t cursor = kPngSignatureBytes;
  while (cursor < encoded.size()) {
    const auto size = big_word(png, cursor);
    png.range(cursor + 4,
              kChunkTypeAndCrcBytes + static_cast<std::uint64_t>(size));
    const auto type = png.tag_view(cursor + 4);
    if (type == "gAMA" || type == "iCCP" || type == "sRGB" || type == "cHRM") {
      resources::detail::unsupported(
          "Packed HALF4 PNG contains color-transform metadata");
    }
    cursor += kChunkEnvelopeBytes + static_cast<std::uint64_t>(size);
    if (type == "IEND") {
      break;
    }
  }
}
WorldTextureImage decode_face(
    WorldTextureSource source,
    const memory::Allocator<std::uint8_t>& allocator) {
  const auto encoded = source.encoded;
  const auto metadata = source.metadata;
  const bool cube = source.shape == WorldTextureShape::cube;
  const resources::detail::Reader description{metadata};
  const auto format = description.u32(kFormatOffset) & kFormatMask;
  if (format == kHalfFormat) {
    packed_png_contract(encoded);
  }
  auto image = decode_png(encoded, {}, allocator);
  if (format != kHalfFormat) {
    auto width = image.width + (image.width & 1U);
    auto height = image.height + (image.height & 1U);
    if (cube) {
      const auto raw_edge = description.u32(kSizeOffset) & kDimensionMask;
      const auto edge = raw_edge + (raw_edge & 1U);
      if (edge == 0 || (image.width != raw_edge && image.width != edge) ||
          (image.height != raw_edge && image.height != edge)) {
        resources::detail::malformed(
            "Cube face dimensions disagree with metadata");
      }
      width = edge;
      height = edge;
    }
    // NONE conversion copies the source into the upper-left and explicitly
    // zeroes rounded destination padding, as witnessed against original D3DX.
    if (width != image.width || height != image.height) {
      constexpr std::size_t kChannels = 4;
      memory::Vector<std::uint8_t> padded(
          static_cast<std::size_t>(width) * height * kChannels, 0, allocator);
      for (std::uint32_t y = 0; y < image.height; ++y) {
        const auto source =
            std::span<const std::uint8_t>(image.rgba)
                .subspan(static_cast<std::size_t>(y) * image.width * kChannels,
                         static_cast<std::size_t>(image.width) * kChannels);
        const auto destination = std::span(padded).subspan(
            static_cast<std::size_t>(y) * width * kChannels, source.size());
        std::ranges::copy(source, destination.begin());
      }
      image.rgba = std::move(padded);
    }
    WorldTextureImage result{width, height, WorldTextureFormat::rgba8,
                             std::move(image.rgba)};
    // The original 2D constructor uses NONE/no-dither for staging conversion;
    // cubes retain eight-bit staging even when metadata names the four-bit
    // code.
    if (format == kPackedFourBitFormat && !cube) quantize_texture_rgba4(result);
    return result;
  }
  const auto packed = description.u32(kSizeOffset);
  const auto raw_width = packed & kDimensionMask;
  const auto raw_height =
      cube ? raw_width : (packed >> kHeightShift) & kDimensionMask;
  const auto width = raw_width + (raw_width & 1U);
  const auto height = raw_height + (raw_height & 1U);
  if (width == 0 || height == 0 || image.width != width * 2 ||
      image.height != height) {
    resources::detail::malformed(
        "Packed HALF4 PNG dimensions disagree with native metadata");
  }
  // A8R8G8B8 memory is B,G,R,A on the original little-endian host. Two such
  // source pixels are exactly one R16,G16,B16,A16 target texel after the copy.
  constexpr std::size_t kSourcePixelBytes = 4;
  for (std::size_t offset = 0; offset < image.rgba.size();
       offset += kSourcePixelBytes) {
    std::swap(image.rgba[offset], image.rgba[offset + 2]);
  }
  return {width, height, WorldTextureFormat::rgba16_float,
          std::move(image.rgba)};
}
}  // namespace
WorldTextureImage decode_world_texture(
    WorldTextureSource source,
    const memory::Allocator<std::uint8_t>& allocator) {
  if (source.shape == WorldTextureShape::two_dimensional) {
    return decode_face(source, allocator);
  }
  // Original DDSC holds six offset/length pairs, then independent PNG faces.
  // Validate every range before allocating or publishing any decoded image.
  constexpr std::size_t kFaces = 6;
  constexpr std::size_t kWordBytes = 4;
  constexpr std::size_t kSizeTableOffset = kFaces * kWordBytes;
  constexpr std::size_t kFaceTableBytes = kSizeTableOffset * 2;
  const resources::detail::Reader encoded{source.encoded};
  encoded.range(0, kFaceTableBytes);
  std::array<resources::Range, kFaces> ranges{};
  for (std::size_t face = 0; face < kFaces; ++face) {
    const auto offset = encoded.u32(face * kWordBytes);
    const auto size = encoded.u32(kSizeTableOffset + face * kWordBytes);
    if (offset < kFaceTableBytes || size == 0) {
      resources::detail::malformed("Invalid cube face offset/size");
    }
    encoded.range(offset, size);
    const resources::Range range{offset, size};
    for (std::size_t previous = 0; previous < face; ++previous) {
      const auto& other = ranges.at(previous);
      if (range.offset < other.offset + other.size &&
          other.offset < range.offset + range.size) {
        resources::detail::malformed("Overlapping cube face ranges");
      }
    }
    ranges.at(face) = range;
  }
  WorldTextureImage output{0, 0, WorldTextureFormat::rgba8,
                           memory::Vector<std::uint8_t>(allocator),
                           WorldTextureShape::cube};
  for (const auto& range : ranges) {
    auto face = decode_face(
        {source.encoded.subspan(static_cast<std::size_t>(range.offset),
                                static_cast<std::size_t>(range.size)),
         source.metadata, WorldTextureShape::cube},
        allocator);
    if (output.width == 0) {
      output.width = face.width;
      output.height = face.height;
      output.format = face.format;
      output.pixels.reserve(face.pixels.size() * kFaces);
    } else if (face.width != output.width || face.height != output.height ||
               face.format != output.format) {
      resources::detail::malformed("Cube face format or extent mismatch");
    }
    output.pixels.insert(output.pixels.end(), face.pixels.begin(),
                         face.pixels.end());
  }
  return output;
}
}  // namespace bw::formats
