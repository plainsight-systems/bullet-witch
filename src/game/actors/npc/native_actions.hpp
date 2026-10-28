// Native authored-action execution, including ordered companion actions.
// BehaviorRuntime owns selection, stack/cadence and elapsed time. This owner
// retains only per-slot action phases, random choices and movement/aim state;
// it never advances a world, owns physics or writes actor/render state.
// Prepared definitions and the query/command capabilities outlive this
// nonmoving owner. Decode admitted primary/companion records at preparation,
// enter their source controllers in order, advance from runtime observations,
// emit typed commands, and preserve primary completion codes across
// suspension/resumption. Companion outcomes never replace the primary result.
// Commands are synchronous and must not reenter BehaviorRuntime; behavior
// replacements are queued by the receiver. Missing source contracts fail
// explicitly. Storage is domain-owned and sized at admission; no allocation in
// advance. Core I.1 (#ri-explicit) separates observation from mutation, ES.103
// (#res-overflow) bounds source records; perf CDSA.32
// (guidelines/cpu-dsa/CDSA.32-transform-static-data-into-the-consumer-layout.md)
// prepares immutable actions once. Both design corpora read; see
// first-combat-physics-boundary.md for dispatch and source ownership.
#pragma once

#include <memory>
#include <variant>

#include "ai_settings.hpp"
#include "behavior_runtime.hpp"
#include "bw/math/vector.hpp"
#include "game/session/compatibility/source_random.hpp"
#include "navigation_controller.hpp"
#include "scene_navigation.hpp"

namespace bw::game {
struct NpcActionTarget {
  math::Vector3<float> position{}, aim_position{};
  std::uint64_t identity{};
  bool player{};
  math::Vector3<float> forward{};
};
struct NpcActionPose {
  math::Vector3<float> position{}, forward{}, weapon_origin{};
  float projectile_speed{}, body_radius{};
  // State ownership, clip loop mode and completion latch are independent.
  bool reaction_active{}, motion_looping{}, motion_completed{}, dead{};
  std::uint8_t target_knowledge{};
  float body_height{};
};
struct NpcNavigationObservation {
  math::Vector3<float> heading{};
  std::int32_t side{};
  bool active{}, traversable{};
};
class NpcActionQueries {
 public:
  NpcActionQueries() = default;
  virtual ~NpcActionQueries() = default;
  NpcActionQueries(const NpcActionQueries&) = delete;
  NpcActionQueries& operator=(const NpcActionQueries&) = delete;
  NpcActionQueries(NpcActionQueries&&) = delete;
  NpcActionQueries& operator=(NpcActionQueries&&) = delete;
  virtual NpcActionPose pose() const = 0;
  virtual math::Vector3<float> initial_position() const = 0;
  // Reaction-facing selectors have source-specific direction semantics;
  // they are not actor lookup keys.
  virtual math::Vector3<float> reaction_facing(std::int32_t selector) const = 0;
  virtual const NpcRouteQueries& navigation_queries() const = 0;
  virtual NpcNavigationObservation navigation_state() const = 0;
  // Original selectors are retained at this compatibility boundary: -1 player,
  // -2/-12 current enemy, otherwise authored actor/group target resolution.
  virtual std::optional<NpcActionTarget> target(
      std::int32_t selector) const = 0;
  virtual std::optional<NpcActionTarget> movement_target(
      std::int32_t selector, bool alternate) const = 0;
  // Absent until the original environment receives a position-bearing message.
  virtual std::optional<math::Vector3<float>> last_message_position() const = 0;
  virtual math::Vector3<float> ground_destination(const math::Vector3<float>&,
                                                  float up,
                                                  float down) const = 0;
  virtual std::span<const math::Vector3<float>> patrol_points(
      std::int32_t path) const = 0;
  virtual float movement_speed(float authored_speed) const = 0;
  virtual float movement_range(std::uint32_t category) const = 0;
  virtual bool can_shoot(const math::Vector3<float>& target) const = 0;
  virtual bool can_see(const math::Vector3<float>& target) const = 0;
  virtual NpcAimSettings aim_settings(std::int32_t mode,
                                      bool player_target) const = 0;
  virtual bool dodge_running() const = 0;
  virtual bool melee_idle() const = 0;
  virtual bool voice_finished() const = 0;
  // Completion of this actor's retained reaction voice request, distinct from
  // the AI event-bit mask exposed to Lua.
  virtual bool reaction_voice_finished() const = 0;
  virtual std::string_view return_behavior() const = 0;
};
struct NpcPostureCommand {
  std::int32_t posture{};
  float speed_scale{1};
  bool stop{};
};
struct NpcMoveCommand {
  math::Vector3<float> direction{};
  float speed_scale{1};
  bool face_direction{true};
};
struct NpcFaceCommand {
  math::Vector3<float> direction{};
};
enum class NpcReactionRequestKind : std::uint8_t { ordinary, sequence };
struct NpcReactionCommand {
  std::string_view name;
  std::uint32_t flags{};
  float blend_seconds{};
  bool clear{};
  NpcReactionRequestKind admission{NpcReactionRequestKind::ordinary};
};
struct NpcAimCommand {
  math::Vector3<float> direction{};
};
struct NpcReadyWeaponCommand {
  // Fixed noncamera NPC ready duration. Hardware camera-mode bookkeeping
  // has no consumer here; see the selected-path source coverage report.
  float seconds{};
};
struct NpcFireCommand {
  math::Vector3<float> target{};
  bool explicit_target{true};
};
struct NpcBehaviorCommand {
  std::string_view name;
};
struct NpcVoiceCommand {
  std::int32_t message{}, face{}, mode{};
};
struct NpcTargetCommand {
  std::int32_t selector{}, throw_mode{};
  float near_strength{}, far_strength{}, radius{}, duration{};
};
struct NpcReactionVoiceCommand {
  std::uint32_t action{};
};
struct NpcDodgeCommand {
  math::Vector3<float> target{};
};
struct NpcMeleeCommand {
  std::uint64_t target{};
  std::uint32_t attack{};
};
struct NpcBroadcastCommand {
  std::int32_t selector{}, mode{};
  float radius{};
};
using NpcActionCommand =
    std::variant<NpcPostureCommand, NpcMoveCommand, NpcFaceCommand,
                 NpcReactionCommand, NpcAimCommand, NpcReadyWeaponCommand,
                 NpcFireCommand, NpcBehaviorCommand, NpcVoiceCommand,
                 NpcTargetCommand, NpcBroadcastCommand, NpcReactionVoiceCommand,
                 NpcDodgeCommand, NpcMeleeCommand>;
class NpcActionReceiver {
 public:
  NpcActionReceiver() = default;
  virtual ~NpcActionReceiver() = default;
  NpcActionReceiver(const NpcActionReceiver&) = delete;
  NpcActionReceiver& operator=(const NpcActionReceiver&) = delete;
  NpcActionReceiver(NpcActionReceiver&&) = delete;
  NpcActionReceiver& operator=(NpcActionReceiver&&) = delete;
  // Atomic admission includes shared cooldown and actor motion-slot lookup.
  // No mutable admission is hidden behind a const observation query.
  virtual bool begin_reaction(const NpcReactionCommand&) = 0;
  virtual void submit(const NpcActionCommand&) = 0;
  // Synchronous submission to the actor-owned retained navigation controller.
  // Receiver uses the current admitted behavior frame, not a second clock.
  virtual bool navigate(const NpcNavigationRequest&) = 0;
  virtual void reset_navigation() = 0;
  virtual void set_navigation_side(std::int32_t side) = 0;
};
class NativeActionLibrary {
 public:
  NativeActionLibrary(std::span<const formats::BehaviorTreeDefinition* const>,
                      const std::shared_ptr<memory::Domain>&);
  NativeActionLibrary(const NativeActionLibrary&) = delete;
  NativeActionLibrary& operator=(const NativeActionLibrary&) = delete;
  NativeActionLibrary(NativeActionLibrary&&) = delete;
  NativeActionLibrary& operator=(NativeActionLibrary&&) = delete;
  ~NativeActionLibrary();

 private:
  friend class NativeActions;
  struct Impl;
  std::shared_ptr<const Impl> impl_;
};
class NativeActions final : public BehaviorActions {
 public:
  NativeActions(const NativeActionLibrary&, const NpcActionQueries&,
                NpcActionReceiver&, SourceRandom&,
                const std::shared_ptr<memory::Domain>&);
  NativeActions(const NativeActions&) = delete;
  NativeActions& operator=(const NativeActions&) = delete;
  NativeActions(NativeActions&&) = delete;
  NativeActions& operator=(NativeActions&&) = delete;
  ~NativeActions() override;
  BehaviorActionState state(std::size_t slot) const override;
  void enter(std::size_t slot, const BehaviorActionRequest&) override;
  void advance(std::size_t slot, BehaviorActionFrame) override;
  void exit(std::size_t slot) override;
  void suspend(std::size_t slot) override;
  void resume(std::size_t slot) override;

 private:
  struct Impl;
  std::shared_ptr<Impl> impl_;
};
}  // namespace bw::game
