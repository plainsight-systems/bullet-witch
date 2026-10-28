// Original AI Lua observation surface. The context and query-only provider
// outlive installed callbacks. They borrow no actor mutation capability and
// cannot advance world time. The game supplies source observations from actor,
// perception, path, message and action owners; unavailable producers must
// throw, never fabricate default scores. Scalar getters return original numeric
// Lua values, including predicates (0/1). Parameterized queries validate the
// exact actor identity, pass source indices/names and propagate provider
// failures. ScriptData preserves original case-insensitive prefix names,
// zero-value range defaults and repeated float multiplication for the requested
// power. Binding allocates only at mission preparation; calls use fixed adapter
// metadata and the existing bounded Lua host. Core I.11 (#ri-raw) states
// provider lifetime; MEM.9
// (guidelines/memory/MEM.9-embedded-allocate-at-init.md) prepares bindings
// once. Both design corpora read; provenance in first-combat-ai.md.
#pragma once
#include "bw/script/context.hpp"
namespace bw::game {
enum class BehaviorObservation : std::uint8_t {
  target_model,
  leader_distance_squared,
  enemy_knowledge,
  enemy_distance_squared,
  completion_code,
  level_random,
  elapsed_seconds,
  enemy_can_look,
  actor_life,
  actor_maximum_life,
  enemy_can_shoot_ready_gun,
  enemy_actor_angle,
  enemy_move_angle,
  last_message_id,
  target_is_player,
  leader_angle,
  last_message_distance_squared,
  last_message_actor_angle,
  player_angle,
  player_distance_squared,
  player_can_look,
  last_sound_id
};
struct BehaviorScriptSettings {
  std::int32_t group_number{}, loop_flag{};
  float move_range{}, attack_range_near{}, attack_range_middle{},
      attack_range_far{};
};
class BehaviorObservationSource {
 public:
  BehaviorObservationSource() = default;
  virtual ~BehaviorObservationSource() = default;
  BehaviorObservationSource(const BehaviorObservationSource&) = delete;
  BehaviorObservationSource& operator=(const BehaviorObservationSource&) =
      delete;
  BehaviorObservationSource(BehaviorObservationSource&&) = delete;
  BehaviorObservationSource& operator=(BehaviorObservationSource&&) = delete;
  virtual double scalar(BehaviorObservation) const = 0;
  virtual bool callback_on(std::string_view name) const = 0;
  virtual BehaviorScriptSettings script_settings() const = 0;
  virtual bool previous_labels(std::int32_t depth,
                               std::int32_t label) const = 0;
  virtual double path_distance_squared(std::int32_t point) const = 0;
  virtual double path_angle(std::int32_t point) const = 0;
};
float behavior_script_data(BehaviorScriptSettings, std::string_view name,
                           std::int32_t power);
void bind_behavior_observations(script::Context&, std::uint32_t actor_identity,
                                const BehaviorObservationSource&);
// Shared serial VM route. Identity is library-owned, nonzero only while one
// synchronous evaluation scope is active; both referenced values outlive the
// VM.
void bind_scoped_behavior_observations(script::Context&,
                                       const std::uint32_t& actor_identity,
                                       const BehaviorObservationSource&);
}  // namespace bw::game
