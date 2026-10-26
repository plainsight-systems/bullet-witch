// Validate the recovered RIFF container boundary before the shared WMAv2
// decoder. Preserve two raw segments and authored overlapping switch positions;
// no resampling, fade or native voice is owned here. Core E.6 and MEM.9 (see
// music_decode.hpp), consulted in both phases, retain bounded immutable
// samples.
#include <algorithm>
#include <limits>
#include <stdexcept>
#include <string_view>

#include "bw/audio/music_decode.hpp"
#include "bw/audio/wave.hpp"

namespace bw::audio {
namespace {
constexpr std::size_t kWordBytes = 4;
std::uint32_t word(std::span<const std::uint8_t> bytes, std::size_t offset,
                   std::size_t count = kWordBytes) {
  if (offset > bytes.size() || bytes.size() - offset < count) {
    throw std::invalid_argument("Truncated music metadata");
  }
  std::uint32_t result = 0;
  constexpr unsigned kByteBits = 8;
  for (std::size_t i = 0; i < count; ++i) {
    result |= static_cast<std::uint32_t>(bytes[offset + i]) << (i * kByteBits);
  }
  return result;
}
bool tag(std::span<const std::uint8_t> bytes, std::size_t offset,
         std::string_view expected) {
  return offset <= bytes.size() && expected.size() <= bytes.size() - offset &&
         std::equal(expected.begin(), expected.end(),
                    bytes.begin() + static_cast<std::ptrdiff_t>(offset));
}
struct Segment {
  WmaWave wave;
  std::size_t end{};
};
Segment segment(std::span<const std::uint8_t> bytes, std::size_t start) {
  constexpr std::size_t kRiffHeader = 12, kChunkHeader = 8;
  if (!tag(bytes, start, "RIFF") || !tag(bytes, start + kChunkHeader, "XWMA")) {
    throw std::invalid_argument("Music requires RIFF XWMA segments");
  }
  const auto size = word(bytes, start + 4);
  if (size < 4 || size > bytes.size() - start - kChunkHeader) {
    throw std::invalid_argument("Invalid music RIFF size");
  }
  const auto end = start + kChunkHeader + size;
  std::span<const std::uint8_t> format, seek, data;
  for (auto offset = start + kRiffHeader; offset < end;) {
    if (end - offset < kChunkHeader) {
      throw std::invalid_argument("Truncated music chunk");
    }
    const auto length = word(bytes, offset + 4);
    if (length > end - offset - kChunkHeader) {
      throw std::invalid_argument("Invalid music chunk size");
    }
    auto payload = bytes.subspan(offset + kChunkHeader, length);
    auto assign = [&](std::span<const std::uint8_t>& destination) {
      if (!destination.empty() || payload.empty()) {
        throw std::invalid_argument("Duplicate or empty music chunk");
      }
      destination = payload;
    };
    if (tag(bytes, offset, "fmt ")) {
      assign(format);
    } else if (tag(bytes, offset, "dpds")) {
      assign(seek);
    } else if (tag(bytes, offset, "data")) {
      assign(data);
    }
    offset += kChunkHeader + length;
    if ((length & 1U) != 0) {
      if (offset == end) throw std::invalid_argument("Missing RIFF padding");
      ++offset;
    }
  }
  constexpr std::uint32_t kWmav2 = 0x161, kPcmBits = 16;
  constexpr std::size_t kFormatBytes = 18, kBitsOffset = 14, kExtraOffset = 16,
                        kAverageOffset = 8, kBlockOffset = 12;
  if (format.size() != kFormatBytes || word(format, 0, 2) != kWmav2 ||
      word(format, kBitsOffset, 2) != kPcmBits ||
      word(format, kExtraOffset, 2) != 0 || seek.empty() ||
      seek.size() % kWordBytes != 0 || data.empty()) {
    throw std::invalid_argument("Unsupported music WMAv2 metadata");
  }
  const auto channels = word(format, 2, 2);
  if (channels == 0 || channels > 2) {
    throw std::invalid_argument("Unsupported music channel count");
  }
  const auto decoded_bytes = word(seek, seek.size() - kWordBytes);
  if (decoded_bytes % (channels * 2) != 0) {
    throw std::invalid_argument("Invalid music decoded frame count");
  }
  return {{channels, word(format, 4), word(format, kAverageOffset),
           word(format, kBlockOffset, 2), decoded_bytes / (channels * 2), data,
           seek},
          end};
}
}  // namespace
MusicSamples decode_segmented_music(
    std::span<const std::uint8_t> encoded, std::size_t maximum_samples,
    const std::shared_ptr<memory::Domain>& domain) {
  const auto first = segment(encoded, 0);
  constexpr std::size_t kAlignment = 4;
  const auto next = (first.end + kAlignment - 1) & ~(kAlignment - 1);
  if (next > encoded.size()) {
    throw std::invalid_argument("Truncated music alignment");
  }
  const auto second = segment(encoded, next);
  const auto trailer = (second.end + kAlignment - 1) & ~(kAlignment - 1);
  if (trailer > encoded.size() || encoded.size() - trailer != 2 * kWordBytes) {
    throw std::invalid_argument("Music requires two authored switch positions");
  }
  const auto first_switch = word(encoded, trailer);
  const auto second_switch = word(encoded, trailer + kWordBytes);
  const auto& intro = first.wave;
  const auto& loop = second.wave;
  if (intro.channels != loop.channels ||
      intro.sample_rate != loop.sample_rate || first_switch == 0 ||
      first_switch > intro.duration || second_switch <= first_switch ||
      second_switch - first_switch > loop.duration ||
      intro.duration - first_switch > second_switch - first_switch ||
      loop.duration - (second_switch - first_switch) >
          second_switch - first_switch ||
      intro.duration >
          std::numeric_limits<std::uint32_t>::max() - loop.duration ||
      static_cast<std::size_t>(intro.duration) + loop.duration >
          maximum_samples / intro.channels) {
    throw std::invalid_argument("Invalid or over-budget music segments");
  }
  MusicSamples result{memory::Vector<float>(memory::Allocator<float>(domain)),
                      intro.sample_rate,
                      intro.channels,
                      intro.duration,
                      loop.duration,
                      first_switch,
                      second_switch - first_switch};
  result.samples.reserve(
      (static_cast<std::size_t>(intro.duration) + loop.duration) *
      intro.channels);
  for (const auto& wave : {intro, loop}) {
    const auto decoded = PreparedWave::decode(wave, maximum_samples, domain);
    result.samples.insert(result.samples.end(), decoded.samples().begin(),
                          decoded.samples().end());
  }
  return result;
}
}  // namespace bw::audio
