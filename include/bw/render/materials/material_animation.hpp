// Validated borrowed float2 material key curve. The source clip owner must
// outlive this value. Construction accepts only the recovered uncompressed
// float2 encoding, positive finite duration/rate, nonempty strictly increasing
// finite frame times and finite values. Invalid input throws resources::Error.
// Sampling consumes finite nonnegative elapsed seconds and returns two values;
// no ownership changes, allocations or mutable global state occur. Algorithm:
// wrap/clamp elapsed time using the authored repeat flag, clamp keyed
// endpoints, binary search the adjacent keys, then linearly interpolate. Setup
// O(keys); sampling O(log keys), fixed storage. Validated key ranges remain
// immutable; steady-state loads are unaligned-safe and skip range revalidation.
// Both-phase P.9 (#rp-waste) removes redundant decoding work. Timing/encoding
// provenance and reference comparison live in stage-material-animation.md. Both
// MCP phases: R.2
// (#rr-use-ptr) uses bounded spans for borrowed bytes; MEM.9
// (guidelines/memory/ MEM.9-embedded-allocate-at-init.md) validates once before
// steady-state use.
#pragma once

#include "bw/content/formats/stage_definitions.hpp"

namespace bw::render {
class MaterialFloat2Curve {
 public:
  static MaterialFloat2Curve bind(
      const formats::MaterialAnimationDefinition& clip, std::size_t track);
  std::array<float, 2> sample(double elapsed_seconds) const;

 private:
  std::span<const std::uint8_t> times_, values_;
  std::uint32_t keys_{};
  float duration_frames_{}, frames_per_second_{};
  bool repeat_{};
};
}  // namespace bw::render
