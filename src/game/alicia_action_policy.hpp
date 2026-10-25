// Alicia's recovered grounded/Shooter animation request policy. Values cross
// this boundary by original slot; the actor adapter resolves catalog aliases,
// parameters and prepared clips independently for body, upper and weapon.
// No resources, playback clocks, collision or combat outcomes are owned here.
// Callers supply processed movement and accepted action observations. Methods
// return bounded requests in source submission order; they never infer hits,
// ammunition, generic clip-end transitions or a new input deadzone.
// Invalid finite/range/direction contracts throw before producing a plan.
// Fixed O(1) value operations allocate nothing. Preserve-phase means the
// source negative-start sentinel, resolved against the destination owner by
// the adapter; it is not elapsed time zero. Rates must also be applied to an
// already selected resource because alias admission preserves its clock.
// Both MCP design corpora: I.1 (#ri-explicit) makes action authorization and
// channel ownership explicit; E.4 (#re-design-invariants) validates inputs;
// MEM.9 (guidelines/memory/MEM.9-embedded-allocate-at-init.md) bounds plans.
// Source functions and unresolved state policies: alicia-action-policy.md.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

#include "bw/formats/customization.hpp"
#include "bw/math/vector.hpp"

namespace bw::game {
enum class ActionMotionBank : std::uint8_t {
  body,
  upper_body,
  additive,
  weapon
};
enum class ActionMotionStart : std::uint8_t { seconds, frames, preserve_phase };
enum class AliciaStance : std::uint8_t { standing, crouched };

inline constexpr double kSourceActionBlendSeconds = .3;
struct ActionMotionRequest {
  ActionMotionBank bank{ActionMotionBank::body};
  std::uint32_t slot{}, channel{};
  ActionMotionStart start{ActionMotionStart::seconds};
  double start_seconds{}, start_frames{};
  double blend_seconds{kSourceActionBlendSeconds};
  float rate{1};
  bool fresh{};
};

struct ActionMotionPlan {
  // An accepted shot submits upper, weapon and additive, the largest covered
  // receiver. Clear-upper represents source bank retirement before requests,
  // including raise admission and source ordinary-state exit.
  std::array<ActionMotionRequest, 3> requests{};
  std::size_t count{};
  bool clear_upper{};
  std::optional<std::uint32_t> weapon_rest_slot;
  std::span<const ActionMotionRequest> motions() const {
    return std::span(requests).first(count);
  }
};

// Caller has identified an outgoing ordinary state and a destination outside
// its standing/crouch and generic-jump exceptions. Retire upper before
// requests.
ActionMotionPlan ordinary_shooter_exit(ActionMotionPlan entry = {}) noexcept;

struct GroundMotionInput {
  AliciaStance stance{AliciaStance::standing};
  float magnitude{};                      // Processed hardware magnitude [0,1].
  math::Vector3<float> movement_world{};  // Nonzero horizontal when moving.
  math::Vector3<float> camera_forward{0, 0, 1};  // Nonzero horizontal.
  float signed_turn_rate{};
  bool weapon_raised{};
  float weapon_movement_scale{1};  // Original weapon parameter +0xb0.
  std::optional<std::uint32_t> upper_slot;
  double upper_sampled_seconds{};
};

struct GroundMotionSelection {
  ActionMotionRequest motion;
  std::uint32_t locomotion_slot{};  // Before idle mirrors an upper action.
  bool camera_facing{};
};

// Source 667000 + standing6667e0/crouched665bd0. Camera-biased directional
// selection uses the original +15 degree reference and inclusive side bounds.
GroundMotionSelection select_ground_motion(const GroundMotionInput& input);

struct GroundIdleSelection {
  GroundMotionSelection ground;
  bool special_idle{};  // Retained stay overrides the ordinary idle request.
  bool refresh_direction{true};  // Source updates this only without old stay.
};

// Standing state owns the deterministic 60-second idle timer. Strictly crossing
// the threshold arms stay for the NEXT update; movement, an upper action or
// completion interrupts it. Crouching resets this standing-only owner. Call
// reset on every other standing-state exit; pausing means not calling advance.
// Completion refers to current_body_slot, never an unrelated outgoing clip.
// Finite nonnegative float delta and valid input are checked before mutation.
// I.7 (#ri-post) specifies admission/reset phase; MEM.9 keeps fixed O(1) state.
class GroundIdlePolicy {
 public:
  GroundIdleSelection advance(const GroundMotionInput& input,
                              std::optional<std::uint32_t> current_body_slot,
                              bool current_body_completed, float delta_seconds);
  void reset() noexcept;

 private:
  float idle_seconds_{};
  bool stay_pending_{};
};

// Crossing the raised-state boundary submits upper raise/lower and an
// independently clocked weapon request. Equal states yield an empty plan.
ActionMotionPlan change_weapon_raised(AliciaStance stance,
                                      std::uint32_t body_slot, bool was_raised,
                                      bool raised);

// These are accepted receiver messages, not raw button presses. Weapon class
// is 0..3. Reload also invokes original weapon ammo mutation outside this
// presentation-only seam; callers must not fabricate acceptance.
struct AcceptedWeaponAction {
  AliciaStance stance{AliciaStance::standing};
  std::uint32_t body_slot{}, weapon_class{};
  bool grounded{true};
};
ActionMotionPlan accepted_reload(const AcceptedWeaponAction& action);
ActionMotionPlan accepted_shot(const AcceptedWeaponAction& action);
ActionMotionPlan accepted_weapon_change(AliciaStance stance,
                                        std::uint32_t body_slot);

// The support-hand improvement may coexist with discharge recoil, but must
// release for every other upper action (reload, switch, raise/lower, unknown).
// This classifies source binding families, never clip names or frame timing.
// Empty upper ownership leaves permission with the controller's action gate.
// Both-phase I.1/MEM.9: pure value policy, fixed work, no retained authority.
bool permits_support_hand(std::optional<std::uint32_t> upper_slot) noexcept;

// Decode the base weapon specification once, before the input loop. Native
// 617260 resolves witch/weapon/waN/00_spec. Enchanted alternatives and
// ammunition ownership are separate consumers; this immutable view does not
// admit combat.
struct WeaponPresentationSettings {
  float rate_per_second{}, movement_scale{};
  static WeaponPresentationSettings parse(
      const formats::CustomizationDefinition& tree, unsigned weapon_class);
};

// Live cadence/ammunition now belong to PlayerResources. Animation policy
// consumes its accepted action result; no second presentation cooldown exists.

// Source 62e400 migrates existing upper-family standing/crouched variants
// without restarting their phase. No request is produced outside its range.
std::optional<ActionMotionRequest> adapt_upper_stance(AliciaStance stance,
                                                      std::uint32_t upper_slot);

// Source617a30 restores the explicit desired weapon stance only for a
// completed nonrepeating current clip. No equivalent body-idle rule exists.
std::optional<ActionMotionRequest> completed_weapon_motion(
    bool repeating, bool completed, std::uint32_t rest_slot);
}  // namespace bw::game
