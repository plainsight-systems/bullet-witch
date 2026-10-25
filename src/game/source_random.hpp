// Shared game-session random stream reconstructed from 5df8c0/5df8e0. The
// constructor seeds1 and clears the draw counter; each bounded draw advances
// uint32 state by state*69069+1, masks the sign bit and takes the remainder.
// Callers share one explicitly owned stream, including audio variation and
// future game consumers. No hidden subsystem seed, locking or allocation.
// Zero bounds reject before mutation. One owner thread; deterministic replay
// may supply an explicit initial seed. Core SL.con.3 (#rsl-bounds) makes the
// valid bound explicit; Performance MEM.9 (guidelines/memory/MEM.9-embedded-
// allocate-at-init.md) keeps runtime state fixed. Both design/implementation
// corpora consulted; source witnesses in animation-audio-presentation.md.
#pragma once
#include <cstdint>
#include <stdexcept>
namespace bw::game {
class SourceRandom {
 public:
  explicit SourceRandom(std::uint32_t seed = 1) : state_(seed) {}
  std::uint32_t next(std::uint32_t upper) {
    if (upper == 0) {
      throw std::invalid_argument("Random bound must be positive");
    }
    ++draws_;
    state_ = state_ * kMultiplier + 1;
    return (state_ & kPositiveMask) % upper;
  }
  std::uint32_t state() const { return state_; }
  std::uint32_t draws() const { return draws_; }
  // Source5df900 draws from the same stream, performs signed-positive modulo
  // 0x7fffffff, then float conversion and multiplication by exact 2^-31.
  float unit() {
    constexpr float kUnitScale = 0x1p-31F;
    return static_cast<float>(next(kPositiveMask)) * kUnitScale;
  }

 private:
  static constexpr std::uint32_t kMultiplier = 0x10dcd,
                                 kPositiveMask = 0x7fffffff;
  std::uint32_t state_, draws_{};
};
}  // namespace bw::game
