// Original scalar sampler with prepared owned frame storage. Header owns source
// version/key/interpolation contract. Implementation: CP.3 (#rconc-data) keeps
// sample const and independent; MEM.9 (embedded-allocate-at-init) confines
// decoding/allocation to construction. Both implementation corpora consulted.
#include "bw/animation/scalar_animation.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <stdexcept>

#include "../formats/reader.hpp"
#include "bw/formats/stage_definitions.hpp"
namespace bw::animation {
namespace {
constexpr std::uint32_t kVersionFirst = 0x100, kVersionLast = 0x108;
constexpr std::array<std::size_t, 9> kChannelCounts{25,  31,  98,  131, 140,
                                                    147, 148, 178, 186};
constexpr std::size_t kWord = sizeof(float);
}  // namespace
ScalarAnimation::ScalarAnimation(std::span<const std::uint8_t> bytes,
                                 const std::shared_ptr<memory::Domain>& domain)
    : times_(memory::Allocator<float>(domain)),
      values_(memory::Allocator<float>(domain)),
      channels_(memory::Allocator<std::uint8_t>(domain)) {
  const auto definition = formats::ScalarAnimationDefinition::parse(bytes);
  const resources::detail::Reader reader{bytes};
  constexpr std::uint64_t kMajorOffset = 4, kMinorOffset = 6,
                          kDirectoryOffset = 20;
  constexpr unsigned kVersionShift = 8;
  version_ =
      (static_cast<std::uint32_t>(reader.u16(kMajorOffset)) << kVersionShift) |
      reader.u16(kMinorOffset);
  if (version_ < kVersionFirst || version_ > kVersionLast ||
      definition.frame_count == 0) {
    throw std::invalid_argument("Unsupported or empty scalar animation");
  }
  const auto count = kChannelCounts.at(version_ - kVersionFirst);
  if (definition.channels.size() < count) {
    throw std::invalid_argument("Truncated scalar interpolation flags");
  }
  const auto directory = reader.u32(kDirectoryOffset);
  reader.range(directory,
               static_cast<std::uint64_t>(definition.frame_count) * kWord);
  times_.reserve(definition.frame_count);
  values_.reserve(static_cast<std::size_t>(definition.frame_count) * count);
  channels_.assign(
      definition.channels.begin(),
      definition.channels.begin() + static_cast<std::ptrdiff_t>(count));
  const resources::detail::Reader times{definition.times};
  for (std::uint32_t frame = 0; frame < definition.frame_count; ++frame) {
    const float time = std::bit_cast<float>(times.u32(frame * kWord));
    if (!std::isfinite(time) || (!times_.empty() && time < times_.back())) {
      throw std::invalid_argument("Invalid scalar animation key order");
    }
    times_.push_back(time);
    const auto offset = reader.u32(directory + frame * kWord);
    reader.range(offset, count * kWord);
    for (std::size_t channel = 0; channel < count; ++channel) {
      const float value =
          std::bit_cast<float>(reader.u32(offset + channel * kWord));
      if (!std::isfinite(value)) {
        throw std::invalid_argument("Nonfinite scalar animation value");
      }
      values_.push_back(value);
    }
  }
}
ScalarAnimationSample ScalarAnimation::sample(float time,
                                              bool interpolate) const {
  if (!std::isfinite(time)) {
    throw std::invalid_argument("Nonfinite scalar sample time");
  }
  const auto upper = std::upper_bound(times_.begin(), times_.end(), time);
  const auto high = upper == times_.end()
                        ? times_.size() - 1
                        : static_cast<std::size_t>(upper - times_.begin());
  const auto low = upper == times_.begin()
                       ? 0
                       : static_cast<std::size_t>(upper - times_.begin() - 1);
  const float weight = interpolate && low != high
                           ? (time - times_[low]) / (times_[high] - times_[low])
                           : 0;
  ScalarAnimationSample result{};
  for (std::size_t channel = 0; channel < channels_.size(); ++channel) {
    const float first = values_[low * channels_.size() + channel];
    const float second = values_[high * channels_.size() + channel];
    result.at(channel) =
        channels_[channel] == 0 ? (second - first) * weight + first : first;
  }
  return result;
}
}  // namespace bw::animation
