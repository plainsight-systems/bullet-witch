// Prepared original AI settings, separate from live behavior/actor mutation.
// Parse immutable CSTM search and five aim-mode records once; retain copied
// fixed values. CharacterEnvironment fields start at their original constructor
// defaults and the game overlays authored mission/script values before use.
// Resolve player/NPC aim modes and perception inputs without archive reads,
// allocation or hidden global state. Invalid/truncated settings throw before
// publication; independent const readers require no synchronization. Mode
// outside 0..4 preserves original fallback to mode0. Dynamic prediction
// overrides remain the action owner's explicit observation, not a table guess.
// Core I.1 (#ri-explicit) exposes settings dependencies; E.4 validates
// candidates; MEM.9 (guidelines/memory/MEM.9-embedded-allocate-at-init.md)
// fixes retained storage. Both design corpora read. Provenance:
// first-combat-ai.md.
#pragma once
#include <array>

#include "behavior_observations.hpp"
#include "bw/content/formats/customization.hpp"
#include "perception.hpp"
#include "steering.hpp"
namespace bw::game {
struct NpcAimSettings {
  float cone_radians{}, transition_seconds{};
  bool predict_target{};
};
struct NpcNavigationSettings {
  float retreat_arrival_squared{}, idle_threshold_adjustment{};
};
struct NpcMovementSettings {
  std::int32_t original_posture{};
  float radius{};
};
NpcMovementSettings npc_movement_settings(std::int32_t posture) noexcept;
struct NpcActorAiSettings {
  static constexpr float kMoveRange = 100.F, kNearRange = 10.F,
                         kMiddleRange = 50.F, kFarRange = 100.F,
                         kViewDegrees = 90.F, kFarView = 60.F;
  BehaviorScriptSettings script{-1,           0,        kMoveRange, kNearRange,
                                kMiddleRange, kFarRange};
  float view_degrees{kViewDegrees}, close_radius{kNearRange},
      far_radius{kFarView};
};
class NpcAiSettings {
 public:
  static NpcAiSettings parse(const formats::CustomizationDefinition&);
  NpcAimSettings aim(std::int32_t mode, bool player_target) const noexcept;
  NpcNavigationSettings navigation(std::int32_t mode,
                                   bool enabled) const noexcept;
  NpcSteeringSettings steering(std::int32_t mode) const noexcept;
  PerceptionSettings perception(const NpcActorAiSettings&) const;

 private:
  NpcAiSettings() = default;
  static constexpr std::size_t kAimModes = 5;
  std::array<NpcAimSettings, kAimModes> aiming_{};
  std::array<NpcNavigationSettings, kAimModes> navigation_{};
  std::array<NpcSteeringSettings, kAimModes> steering_{};
  float target_change_delay_{}, lost_target_seconds_{}, nearby_radius_{};
};
}  // namespace bw::game
