// Both-phase P.9 (#rp-waste) removes repeated range decoding after bind;
// MEM.9 keeps the sampled immutable representation allocation-free.
// Validate borrowed authored keys once, then sample without allocation or
// shared state. The strict supported encoding and explicit time units are in
// material_animation.hpp; source/reference notes are in
// stage-material-animation.md. Both MCP phases: R.2 (#rr-use-ptr) bounds every
// input region; MEM.9 (guidelines/memory/MEM.9-embedded-allocate-at-init.md)
// keeps binary-search evaluation on fixed scalar storage after construction.
#include "bw/render/material_animation.hpp"

#include <bit>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>

namespace bw::render {
namespace {
constexpr std::size_t kWordBytes = sizeof(std::uint32_t);
std::span<const std::uint8_t> region(std::span<const std::uint8_t> bytes,
                                     std::size_t offset, std::size_t count) {
  if (offset > bytes.size() || count > bytes.size() - offset) {
    throw resources::Error(resources::ErrorCode::malformed,
                           "Material animation range exceeds clip");
  }
  return bytes.subspan(offset, count);
}
std::uint32_t word(std::span<const std::uint8_t> bytes, std::size_t offset) {
  const auto value = region(bytes, offset, kWordBytes);
  std::uint32_t result = 0;
  for (std::size_t i = 0; i < kWordBytes; ++i) {
    result |= static_cast<std::uint32_t>(value[i])
              << (i * std::numeric_limits<std::uint8_t>::digits);
  }
  return result;
}
float scalar(std::span<const std::uint8_t> bytes, std::size_t offset) {
  return std::bit_cast<float>(word(bytes, offset));
}
// Bind proves each complete key region. Index arithmetic below stays within
// that immutable region; memcpy permits unaligned source data without aliasing.
float key_scalar(std::span<const std::uint8_t> bytes, std::size_t offset) {
  if constexpr (std::endian::native == std::endian::little) {
    float result{};
    std::memcpy(&result, bytes.subspan(offset, kWordBytes).data(),
                sizeof(result));
    return result;
  } else {
    return scalar(bytes, offset);
  }
}
}  // namespace
MaterialFloat2Curve MaterialFloat2Curve::bind(
    const formats::MaterialAnimationDefinition& clip, std::size_t track) {
  const auto& source = clip.tracks().at(track);
  constexpr std::uint32_t kFloat2Encoding = 0x46000000;
  constexpr std::size_t kCountOffset = 8, kStrideOffset = 12;
  constexpr std::size_t kDurationOffset = 8, kRateOffset = 12;
  constexpr std::size_t kRepeatOffset = 16;
  if (word(source.header, 0) != kFloat2Encoding ||
      word(source.header, kStrideOffset) != 2 * kWordBytes) {
    throw resources::Error(resources::ErrorCode::unsupported,
                           "Unsupported material animation key encoding");
  }
  MaterialFloat2Curve result;
  result.keys_ = word(source.header, kCountOffset);
  result.duration_frames_ = scalar(clip.bytes(), kDurationOffset);
  result.frames_per_second_ = scalar(clip.bytes(), kRateOffset);
  result.repeat_ = word(clip.bytes(), kRepeatOffset) != 0;
  if (result.keys_ == 0 || !std::isfinite(result.duration_frames_) ||
      result.duration_frames_ <= 0 ||
      !std::isfinite(result.frames_per_second_) ||
      result.frames_per_second_ <= 0) {
    throw resources::Error(resources::ErrorCode::malformed,
                           "Invalid material animation clock/key count");
  }
  result.times_ = region(clip.bytes(), source.first_data_offset,
                         static_cast<std::size_t>(result.keys_) * kWordBytes);
  result.values_ =
      region(clip.bytes(), source.second_data_offset,
             static_cast<std::size_t>(result.keys_) * 2 * kWordBytes);
  float previous = -1;
  for (std::size_t key = 0; key < result.keys_; ++key) {
    const float time = scalar(result.times_, key * kWordBytes);
    if (!std::isfinite(time) || time < 0 || time <= previous ||
        time > result.duration_frames_ ||
        !std::isfinite(scalar(result.values_, key * 2 * kWordBytes)) ||
        !std::isfinite(scalar(result.values_, (key * 2 + 1) * kWordBytes))) {
      throw resources::Error(resources::ErrorCode::malformed,
                             "Invalid material animation key time/value");
    }
    previous = time;
  }
  return result;
}

std::array<float, 2> MaterialFloat2Curve::sample(double elapsed_seconds) const {
  if (!std::isfinite(elapsed_seconds) || elapsed_seconds < 0 || keys_ == 0) {
    throw std::invalid_argument(
        "Material animation time must be finite and nonnegative");
  }
  const double duration_seconds =
      static_cast<double>(duration_frames_) / frames_per_second_;
  const double seconds = repeat_ ? std::fmod(elapsed_seconds, duration_seconds)
                                 : std::min(elapsed_seconds, duration_seconds);
  const float frame = static_cast<float>(seconds * frames_per_second_);
  // Find the first key strictly later than this frame, preserving exact keys.
  std::size_t first = 0, count = keys_;
  while (count != 0) {
    const auto step = count / 2;
    const auto middle = first + step;
    if (key_scalar(times_, middle * kWordBytes) <= frame) {
      first = middle + 1;
      count -= step + 1;
    } else {
      count = step;
    }
  }
  const auto lower = first == 0 ? 0 : first - 1;
  const auto upper = first == keys_ ? lower : first;
  const float lower_time = key_scalar(times_, lower * kWordBytes);
  const float alpha =
      lower == upper
          ? 0
          : (frame - lower_time) /
                (key_scalar(times_, upper * kWordBytes) - lower_time);
  std::array<float, 2> result{};
  for (std::size_t component = 0; component < result.size(); ++component) {
    const float left =
        key_scalar(values_, (lower * 2 + component) * kWordBytes);
    const float right =
        key_scalar(values_, (upper * 2 + component) * kWordBytes);
    result.at(component) = std::lerp(left, right, alpha);
  }
  return result;
}
}  // namespace bw::render
