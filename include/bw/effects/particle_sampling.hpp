// Original particle curve and random-sampling primitives. Curves are immutable
// caller-owned decoded values; the noncopyable random stream is a single serial
// authority shared by all effect instances in original update order. It owns no
// clock or game state. Sampling returns values without allocation. Invalid
// sample coordinates/count ranges throw invalid_argument before RNG mutation.
// Algorithms preserve the native keyed-curve terminal index, unsigned LCG,
// integer modulo and conditional second random draw in minimum guards.
// O(1) random work; at most 16 curve keys. This is not an effect simulation.
// Core I.11 (#ri-raw) leaves curve ownership with its resource, SL.con.3
// (#rsl-bounds) bounds sample indexes. Performance MEM.9
// (guidelines/memory/MEM.9-embedded-allocate-at-init.md) keeps these operations
// allocation-free. Both corpora consulted in design and implementation;
// CDSA.32 (guidelines/cpu-dsa/
// CDSA.32-transform-static-data-into-the-consumer-layout.md) prepares curves
// once.
#pragma once

#include "bw/content/formats/effect_pack.hpp"

namespace bw::effects {
float sample_scalar_curve(const formats::EffectScalarCurve& curve,
                          std::uint32_t sample_index, float fraction);

class ParticleRandom {
 public:
  explicit ParticleRandom(std::uint32_t seed = 1) noexcept : state_(seed) {}
  ParticleRandom(const ParticleRandom&) = delete;
  ParticleRandom& operator=(const ParticleRandom&) = delete;
  ParticleRandom(ParticleRandom&&) = delete;
  ParticleRandom& operator=(ParticleRandom&&) = delete;
  ~ParticleRandom() = default;
  std::uint16_t next() noexcept;
  // Finite bounds are required; their order is preserved, not normalized.
  float between(float lower, float upper);
  // Original guards draw to test the minimum, then draw AGAIN when it passes.
  // The second result is not clamped back to minimum. This ordering matters.
  float guarded_jitter(float center, float radius, float minimum);
  std::int32_t count(std::int32_t center, std::int32_t radius);
  std::uint32_t state() const noexcept { return state_; }

 private:
  std::uint32_t state_;
};
}  // namespace bw::effects
