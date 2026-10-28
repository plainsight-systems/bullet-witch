// Original motion-hit receiver lifetime, independent of physics and damage.
// The actor owns four joint-keyed active slots and consumes copied event phases
// after pose completion. The world owns attack serials, overlap queries,
// receiver deduplication and damage dispatch. Emit borrows one copied packet
// synchronously; it cannot advance the world. The emission service outlives
// this nonmoving owner. Reset/exit retires every matching joint. Full slots
// preserve original ignored admission. No frame allocation or local clock.
// Core I.1 (#ri-explicit) separates receiver authority; MEM.9
// (guidelines/memory/MEM.9-embedded-allocate-at-init.md) fixes four slots.
// Both design corpora read; first-combat-ai.md records native provenance.
#pragma once
#include <functional>

#include "bw/math/vector.hpp"
#include "game/actors/animation/motion_presentation.hpp"
namespace bw::game {
struct MeleeContactVolume {
  std::uint64_t source{};
  std::uint32_t serial{};
  math::Vector3<float> center{}, previous{}, actor_origin{};
  float radius{}, damage{}, far_damage{};
  std::int32_t effect{-1};
};
class MeleeEmission {
 public:
  MeleeEmission() = default;
  virtual ~MeleeEmission() = default;
  MeleeEmission(const MeleeEmission&) = delete;
  MeleeEmission& operator=(const MeleeEmission&) = delete;
  MeleeEmission(MeleeEmission&&) = delete;
  MeleeEmission& operator=(MeleeEmission&&) = delete;
  // Original serial increments through INT_MAX, then wraps to one.
  virtual std::uint32_t begin_attack() = 0;
  virtual void emit(const MeleeContactVolume&) = 0;
};
class MeleeMotionEvents {
 public:
  MeleeMotionEvents(std::uint64_t source, MeleeEmission&);
  MeleeMotionEvents(const MeleeMotionEvents&) = delete;
  MeleeMotionEvents& operator=(const MeleeMotionEvents&) = delete;
  MeleeMotionEvents(MeleeMotionEvents&&) = delete;
  MeleeMotionEvents& operator=(MeleeMotionEvents&&) = delete;
  ~MeleeMotionEvents() = default;
  void consume(MeleeMotionCommand, animation::MotionEventPhase,
               const math::Vector3<float>& actor_origin,
               const math::Vector3<float>& completed_joint);
  void reset() noexcept;

 private:
  struct Slot {
    std::int32_t joint{-1};
    std::uint32_t serial{};
    math::Vector3<float> previous{};
  };
  std::uint64_t source_{};
  std::reference_wrapper<MeleeEmission> emission_;
  std::array<Slot, 4> slots_{};
};
}  // namespace bw::game
