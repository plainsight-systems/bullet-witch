// Prepared source scalar-channel animation. Construction validates/copies the
// FLAN times, per-channel interpolation flags and frame values into the
// supplied domain. Input bytes need not survive construction. Versions 1.0–1.8
// map to their recovered channel counts; unknown versions, nonfinite data and
// empty frames reject. Times are nondecreasing; exact duplicate keys choose the
// last. Sampling uses the last key <= time, clamps outside the track and
// optionally interpolates zero-flag channels. Other channels hold the lower
// key. Results contain 186 owned lanes, with absent version lanes zero. Lookup
// O(log frames), sampling O(channels); no allocation, mutation or
// synchronization during sample. Source4ac280/4ac3f0; see
// lighting-scene-producer-contract.md. Design: Core CP.3 (#rconc-data) keeps
// prepared data immutable; performance MEM.9
// (guidelines/memory/MEM.9-embedded-allocate-at-init.md) allocates only at
// setup.
#pragma once
#include <array>
#include <span>

#include "bw/memory/allocator.hpp"
namespace bw::animation {
inline constexpr std::size_t kMaximumScalarChannels = 186;
using ScalarAnimationSample = std::array<float, kMaximumScalarChannels>;
class ScalarAnimation {
 public:
  ScalarAnimation(std::span<const std::uint8_t> bytes,
                  const std::shared_ptr<memory::Domain>& domain);
  ScalarAnimationSample sample(float time, bool interpolate = false) const;
  std::uint32_t version() const { return version_; }
  std::size_t channel_count() const { return channels_.size(); }

 private:
  std::uint32_t version_{};
  memory::Vector<float> times_, values_;
  memory::Vector<std::uint8_t> channels_;
};
}  // namespace bw::animation
