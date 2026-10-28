// Persistent mission script runtime owns commands, callbacks and retained UI
// tasks from authored opening through active mission updates.
// Assets are prepared before start. The composition callback admits the world
// at the first authored player transform, before any world frame; no temporary
// placement is simulated. All returned owners remain stable until this owner
// retires its VM callbacks. Commands are synchronous and owner-thread only.
// Updates observe the sole world-clock receipt; no independent clock exists.
// Fixed callback/checkpoint storage, no steady-state allocation. Design Core
// I.11 (#ri-raw), E.6 (#re-raii), and performance MEM.9
// (guidelines/memory/MEM.9-embedded-allocate-at-init.md) bound borrows/storage.
// Source and deferred producer boundaries: mission-startup-implementation.md.
#pragma once
#include <functional>

#include "game/session/audio/music_playback.hpp"
#include "game/ui/hud/mission_system_message.hpp"
#include "game/ui/mission/mission_game_over.hpp"
#include "game/ui/mission/mission_message.hpp"
#include "mission_driver.hpp"
#include "mission_gameplay.hpp"

namespace bw::game {
struct MissionCallbackDefinition;
struct MissionWorldActivation {
  ScriptWorldControl world;
  std::reference_wrapper<MissionGameplay> gameplay;
  std::reference_wrapper<const PlayerResources> resources;
  std::reference_wrapper<const MissionPlayerState> player;
};
struct MissionInitialCheckpoint {
  std::uint32_t identifier{};
  CheckpointPlayer player;
  ScriptedPlayerPose placement;
  PopulationCheckpoint population;
  // Borrowed immutable baseline; MissionLoading outlives this checkpoint.
  const formats::BackgroundPlacementDefinition* background{};
  std::size_t background_instances{};
};
class MissionScriptRuntime final : public MissionScriptServices {
 public:
  using ActivateWorld =
      std::function<MissionWorldActivation(ScriptedPlayerPose, bool)>;
  MissionScriptRuntime(MissionDriver&, ScriptPauseControl, MissionMessage&,
                       ActivateWorld, MusicCommands,
                       const formats::BackgroundPlacementDefinition* background,
                       MissionTrapContains contains,
                       const formats::TextTable& system_text);
  ~MissionScriptRuntime() override;
  MissionScriptRuntime(const MissionScriptRuntime&) = delete;
  MissionScriptRuntime& operator=(const MissionScriptRuntime&) = delete;
  MissionScriptRuntime(MissionScriptRuntime&&) = delete;
  MissionScriptRuntime& operator=(MissionScriptRuntime&&) = delete;
  MissionFrameDisposition begin(float seconds, const MissionInput&);
  void advance();
  void present();
  SystemMessageView system_message_view() const {
    return system_message_.view();
  }
  bool world_active() const noexcept { return world_.has_value(); }
  float presentation_opacity() const;
  const std::optional<MissionInitialCheckpoint>& initial_checkpoint() const {
    return checkpoint_;
  }
  const std::optional<std::array<float, 3>>& goal_target() const {
    return goal_;
  }
  std::size_t trap_count() const noexcept { return callbacks_.trap_count(); }
  void pause(ScriptPause) override;
  void resume() override;
  void set_background() override;
  void set_player_position(ScriptedPlayerPose) override;
  void set_player_visible(bool) override;
  void set_cockpit_visible(bool) override;
  void set_trap_callback(std::string_view, MissionTrapEvent) override;
  void set_timer_callback(std::string_view, MissionTimerEvent) override;
  void scan_callbacks() override;
  std::optional<MissionCallbackName> take_callback() override;
  void reset_goal_target() override;
  void show_system_message(std::uint32_t) override;
  void cancel_system_message() override;
  void set_goal_target(const std::array<float, 3>&) override;
  std::uint32_t checkpoint() const override;
  void set_checkpoint(std::uint32_t) override;
  void show_message(std::uint32_t) override;
  bool message_pending() const override;
  void fade_in(float) override;
  bool fade_pending() const override { return fading_; }
  MissionScriptInterval control_interval() const override;
  void play_music(std::uint32_t) override;
  void stop_music(float) override;
  // Bind prepared original defeat assets and copied callback names before
  // start. The surface and session audio outlive this stable script owner.
  void bind_defeat(MissionGameOverSurface&, audio::CueOutput&,
                   std::span<const MissionCallbackDefinition>);
  void remove_population(PopulationFamily) override;
  void remove_background() override;
  void cancel_realtime_event_actors() override;
  void set_environment_sound(std::uint32_t) override;
  void stop_magic() override;
  void begin_game_over() override;
  bool game_over_pending() const override;

 private:
  MissionWorldActivation& world();
  MissionDriver& driver_;
  ScriptPauseControl control_;
  MissionMessage& message_;
  MissionSystemMessage system_message_;
  ActivateWorld activate_;
  MusicCommands music_;
  std::optional<MissionWorldActivation> world_;
  MissionCallbacks callbacks_;
  MissionTrapContains contains_;
  const formats::BackgroundPlacementDefinition* background_{};
  std::optional<std::array<float, 3>> goal_;
  std::optional<MissionInitialCheckpoint> checkpoint_;
  bool background_requested_{}, cockpit_visible_{}, player_visible_{};
  float opacity_{}, fade_remaining_{}, fade_rate_{};
  bool fading_{};
  MissionGameOverFlow game_over_;
  MissionGameOverSurface* game_over_surface_{};
  std::optional<MenuAudio> game_over_audio_;
};
}  // namespace bw::game
