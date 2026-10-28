// Public scalar/RNG contracts, including the source's unusual terminal result
// and conditional second draw. No files, processes or synthetic parity claim.
// Core SL.con.3 (#rsl-bounds) checks invalid sampling without state mutation;
// performance MEM.9 (guidelines/memory/MEM.9-embedded-allocate-at-init.md)
// keeps production sampling allocation-free. Fixture storage is fixed-size.
#include "bw/effects/particle_sampling.hpp"

#include <cmath>
#include <limits>
#include <stdexcept>
#include <type_traits>

namespace {
void require(bool value, const char* message) {
  if (!value) throw std::runtime_error(message);
}
void near(float actual, float expected) {
  require(std::abs(actual - expected) < 0.00001F,
          "Unexpected source particle sample");
}
}  // namespace

void particle_sampling_tests() {
  using bw::effects::ParticleRandom;
  using bw::effects::sample_scalar_curve;
  static_assert(!std::is_copy_constructible_v<ParticleRandom>);
  static_assert(!std::is_move_constructible_v<ParticleRandom>);
  bw::formats::EffectScalarCurve curve;
  curve.values[0] = 2;
  curve.values[1] = 6;
  curve.values.back() = 9;
  near(sample_scalar_curve(curve, 0, 0.25F), 3);
  near(sample_scalar_curve(curve, 31, 0), 9);
  curve.mode = 3;
  curve.values[0] = 0;
  curve.values[1] = 2;
  curve.values[2] = 0.25F;
  curve.values[3] = 6;
  curve.values[4] = 0.5F;
  curve.values[5] = 10;
  near(sample_scalar_curve(curve, 3, 0.875F), 4);
  // Native terminal is float word mode, not the last key value.
  near(sample_scalar_curve(curve, 30, 0), 0.25F);
  near(sample_scalar_curve(curve, 31, 0), 9);
  ParticleRandom random;
  require(
      random.next() == 16838 && random.next() == 5758 && random.next() == 10113,
      "Original LCG sequence");
  ParticleRandom guard, comparison;
  (void)comparison.between(-1, 1);
  const auto expected = 2 + comparison.between(-1, 1);
  near(guard.guarded_jitter(2, 1, 0.01F), expected);
  require(guard.state() == comparison.state(), "Guard consumes second draw");
  ParticleRandom failed_guard, one_draw;
  (void)one_draw.next();
  near(failed_guard.guarded_jitter(-2, 0, 0.01F), 0.01F);
  require(failed_guard.state() == one_draw.state(),
          "Failed minimum consumes only comparison draw");
  ParticleRandom counts;
  require(counts.count(3, 0) == 3 && counts.state() != 1,
          "Zero count jitter still consumes original random draw");
  const auto state = counts.state();
  bool rejected = false;
  try {
    (void)counts.count(0, -1);
  } catch (const std::invalid_argument&) {
    rejected = true;
  }
  require(rejected && counts.state() == state,
          "Invalid random count preserves shared stream");
}
