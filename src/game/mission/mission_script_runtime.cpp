// Apply original mission script commands through their real owners. The first
// authored transform commits staged world construction before frame admission.
// Source fade 485a20 consumes control delta, while acknowledged text owns its
// own task. No unsupported callback is dispatched as successful gameplay.
// Implementation Core I.1 (#ri-explicit), E.4 (#re-design-invariants), E.6
// (#re-raii), and performance MEM.9 (guidelines/memory/
// MEM.9-embedded-allocate-at-init.md) retain validated bounded state.
#include "mission_script_runtime.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

#include "game/mission/loading/mission_loading.hpp"
#include "mission_ancillary_state.hpp"

namespace bw::game {
MissionScriptRuntime::MissionScriptRuntime(
    MissionDriver& driver, ScriptPauseControl control, MissionMessage& message,
    ActivateWorld activate, MusicCommands music,
    const formats::BackgroundPlacementDefinition* background,
    MissionTrapContains contains, const formats::TextTable& system_text)
    : driver_(driver),
      control_(control),
      message_(message),
      system_message_(system_text),
      activate_(std::move(activate)),
      music_(music),
      contains_(std::move(contains)),
      background_(background) {
  if (!activate_ || !contains_) {
    throw std::invalid_argument(
        "Mission startup requires world and music owners");
  }
}
MissionScriptRuntime::~MissionScriptRuntime() {
  driver_.retire_script();
  message_.cancel();
  if (game_over_surface_) game_over_surface_->end();
}
MissionWorldActivation& MissionScriptRuntime::world() {
  if (!world_) throw std::logic_error("Mission world is not active");
  return *world_;
}
MissionFrameDisposition MissionScriptRuntime::begin(float seconds,
                                                    const MissionInput& input) {
  auto& gameplay = world().gameplay.get();
  const bool message_frame = message_.pending();
  const bool defeated = world().resources.get().snapshot().health <= 0;
  auto admitted = input;
  if (message_frame || defeated) {
    admitted.player = {};
    admitted.pause_pressed = false;
    admitted.menu = {};
  }
  auto result = gameplay.begin(seconds, admitted);
  const auto interval = gameplay.control_interval();
  system_message_.advance(interval.delta_seconds);
  if (game_over_surface_ && game_over_audio_) {
    game_over_audio_->advance(interval.delta_seconds);
    game_over_.update(interval.delta_seconds, input.menu);
    game_over_audio_->submit(game_over_.take_sounds());
    if (const auto outcome = game_over_.outcome()) {
      result.transition = *outcome == MissionGameOverOutcome::restart
                              ? MissionTransition::restart_stage
                              : MissionTransition::retire;
    }
  }
  if (control_.mission_script_admitted()) {
    message_.update(input.menu, interval.delta_seconds);
    if (fading_) {
      fade_remaining_ -= interval.delta_seconds;
      opacity_ =
          std::clamp(opacity_ + fade_rate_ * interval.delta_seconds, 0.F, 1.F);
      if (fade_remaining_ <= 0) {
        fade_remaining_ = 0;
        fading_ = false;
        opacity_ = 1;
      }
    }
  }
  result.ui_consumed = result.ui_consumed || message_frame;
  return result;
}
void MissionScriptRuntime::advance() {
  auto& gameplay = world().gameplay.get();
  gameplay.advance();
  callbacks_.tick(gameplay.simulation_interval().delta_seconds);
}
void MissionScriptRuntime::present() {
  world().gameplay.get().present();
  if (message_.pending()) message_.present();
  if (game_over_surface_ &&
      game_over_.presentation().phase != MissionGameOverPhase::inactive) {
    game_over_surface_->show(game_over_.presentation());
  }
  // Original 4993b0 draws system text after independently gated cockpit/modal
  // tasks, including hidden cockpit and user pause. This consumes no time.
  world().gameplay.get().present_system_message(system_message_.view());
}
float MissionScriptRuntime::presentation_opacity() const {
  return world_ ? opacity_ * world_->gameplay.get().presentation_opacity() : 0;
}
void MissionScriptRuntime::pause(ScriptPause kind) {
  control_.push_script_pause(kind);
}
void MissionScriptRuntime::resume() { control_.pop_script_pause(); }
void MissionScriptRuntime::set_background() {
  if (world_) {
    throw std::logic_error("Background admission follows world activation");
  }
  background_requested_ = true;
}
void MissionScriptRuntime::set_player_position(ScriptedPlayerPose pose) {
  if (world_) {
    throw std::logic_error(
        "Live scripted teleport requires its actor pose receiver");
  }
  if (!std::isfinite(pose.yaw_degrees) ||
      !std::ranges::all_of(pose.position,
                           [](float value) { return std::isfinite(value); })) {
    throw std::invalid_argument("Invalid initial scripted player pose");
  }
  world_ = activate_(pose, background_requested_);
  world().world.set_player_visible(player_visible_);
  world().gameplay.get().set_cockpit_visible(cockpit_visible_);
}
void MissionScriptRuntime::set_player_visible(bool visible) {
  player_visible_ = visible;
  world().world.set_player_visible(visible);
}
void MissionScriptRuntime::set_cockpit_visible(bool visible) {
  cockpit_visible_ = visible;
  if (world_) world().gameplay.get().set_cockpit_visible(visible);
}
void MissionScriptRuntime::set_trap_callback(std::string_view function,
                                             MissionTrapEvent event) {
  callbacks_.add_trap(function, event);
}
void MissionScriptRuntime::set_timer_callback(std::string_view function,
                                              MissionTimerEvent timer) {
  callbacks_.add_timer(function, timer);
}
void MissionScriptRuntime::scan_callbacks() {
  const auto player = world().world.player_pose();
  if (!player) {
    throw std::logic_error("Mission callbacks require player");
  }
  callbacks_.scan(player->position, contains_,
                  world().resources.get().snapshot().health <= 0);
}
std::optional<MissionCallbackName> MissionScriptRuntime::take_callback() {
  return callbacks_.take();
}
void MissionScriptRuntime::set_goal_target(
    const std::array<float, 3>& position) {
  if (!std::ranges::all_of(position,
                           [](float value) { return std::isfinite(value); })) {
    throw std::invalid_argument("Nonfinite mission goal target");
  }
  goal_ = position;
  world().gameplay.get().set_goal_target(position);
}
void MissionScriptRuntime::reset_goal_target() {
  goal_.reset();
  if (world_) world().gameplay.get().clear_goal_target();
}
void MissionScriptRuntime::show_system_message(std::uint32_t index) {
  system_message_.start(index);
}
void MissionScriptRuntime::cancel_system_message() { system_message_.cancel(); }
std::uint32_t MissionScriptRuntime::checkpoint() const {
  return checkpoint_ ? checkpoint_->identifier : 0;
}
void MissionScriptRuntime::set_checkpoint(std::uint32_t identifier) {
  // Capture supported producers each time the original script checkpoints.
  // Persistence and restoration remain a separate save/load boundary.
  const auto& active = world();
  const auto player = active.world.player_pose();
  if (!player) {
    throw std::logic_error("Checkpoint has no physical player");
  }
  const auto resource = active.resources.get().snapshot();
  const auto& progression = active.player.get();
  checkpoint_ = MissionInitialCheckpoint{
      identifier,
      {progression.levels(), progression.selected_weapon(), resource.health,
       resource.magic},
      *player,
      active.world.population_checkpoint(),
      background_requested_ ? background_ : nullptr,
      background_requested_ && background_ ? background_->placements().size()
                                           : 0};
}
void MissionScriptRuntime::show_message(std::uint32_t message) {
  message_.start(message);
}
bool MissionScriptRuntime::message_pending() const {
  return message_.pending();
}
void MissionScriptRuntime::fade_in(float seconds) {
  if (!std::isfinite(seconds) || seconds < 0) {
    throw std::invalid_argument("Invalid mission fade duration");
  }
  fade_remaining_ = seconds;
  fade_rate_ = seconds == 0 ? 0 : 1.F / seconds;
  fading_ = true;
}
MissionScriptInterval MissionScriptRuntime::control_interval() const {
  return world_ ? world_->gameplay.get().control_interval()
                : MissionScriptInterval{};
}
void MissionScriptRuntime::play_music(std::uint32_t index) {
  music_.play({index, 1});
}
void MissionScriptRuntime::stop_music(float seconds) { music_.stop(seconds); }
void MissionScriptRuntime::bind_defeat(
    MissionGameOverSurface& surface, audio::CueOutput& audio,
    std::span<const MissionCallbackDefinition> definitions) {
  if (game_over_surface_ || !surface.ready()) {
    throw std::logic_error(
        "Defeat assets must be prepared once before binding");
  }
  constexpr std::int32_t kPlayerDefeated = 1000;
  const auto found = std::ranges::find(definitions, kPlayerDefeated,
                                       &MissionCallbackDefinition::event);
  if (found == definitions.end()) {
    throw std::logic_error("Original player defeat callback is absent");
  }
  callbacks_.add_player_defeated(found->function);
  game_over_audio_.emplace(audio);
  game_over_surface_ = &surface;
}
void MissionScriptRuntime::cancel_realtime_event_actors() {
  MissionAncillaryState::cancel_realtime_event_actors();
}
void MissionScriptRuntime::set_environment_sound(std::uint32_t identifier) {
  MissionAncillaryState::set_environment_sound(identifier);
}
void MissionScriptRuntime::remove_background() {
  world().world.remove_background();
}
void MissionScriptRuntime::remove_population(PopulationFamily family) {
  world().world.remove_population(family);
}
void MissionScriptRuntime::stop_magic() {
  MissionAncillaryState::stop_magic_after_defeat(
      world().resources.get().snapshot().health <= 0);
}
void MissionScriptRuntime::begin_game_over() {
  if (!game_over_surface_) throw std::logic_error("Defeat UI is not admitted");
  game_over_.begin();
}
bool MissionScriptRuntime::game_over_pending() const {
  if (!game_over_surface_) throw std::logic_error("Defeat UI is not admitted");
  return !game_over_.outcome().has_value();
}
}  // namespace bw::game
