// Retained original lethal melee launch/flight/landing phases. The animation
// owner supplies completed motion and frame crossings; the world supplies time
// and shared character contact. No private clock, collision solver or pose
// owner. Source 683100/683470/682b20/682e60. Fixed state and copied requests;
// design Core I.4 (#ri-typed) separates phases from action IDs and MEM.9
// (guidelines/memory/MEM.9-embedded-allocate-at-init.md) avoids tick
// allocations.
#pragma once
#include "action_motion_plan.hpp"
#include "bw/math/matrix.hpp"
#include "bw/math/vector.hpp"
namespace bw::game {
enum class MeleeBlastPhase : std::uint8_t { inactive, start, flight, landing };
struct MeleeBlastStep {
  bool motion_completed{}, grounded{}, crossed_death_frame{};
};
struct MeleeBlastTransition {
  std::optional<ActionMotionRequest> motion;
  bool release_hit_flags{}, physical_handoff{};
};
class MeleeBlast {
 public:
  ActionMotionRequest begin(math::Vector3<float> incoming,
                            const math::Matrix<float, 4, 4>& model);
  math::Vector3<float> facing(math::Vector3<float> current,
                              float delta_seconds) const;
  // Called after animation advances, before shared world contact. Flight
  // consumes its authored Y velocity until completion, then integrates gravity.
  math::Vector3<float> displacement(math::Vector3<float> root,
                                    float delta_seconds, bool motion_completed);
  MeleeBlastTransition finish(const MeleeBlastStep&);
  MeleeBlastPhase phase() const noexcept { return phase_; }
  math::Vector3<float> settled_facing() const noexcept { return heading_; }
  bool active() const noexcept { return phase_ != MeleeBlastPhase::inactive; }

 private:
  MeleeBlastPhase phase_{};
  std::size_t direction_{};
  math::Vector3<float> heading_{};
  float vertical_velocity_{};
  bool ballistic_{};
};
}  // namespace bw::game
