// Sequence existing game owners, without reproducing their algorithms.
// Interaction owns pause/equipment; input owns ring arbitration; resource and
// actor owners execute; cockpit maps observations. I.1/E.4 and MEM.9 were
// reread for implementation: one checked phase handoff, fixed frame storage,
// no wall clock or permanent-profile authority.
#include "mission_gameplay.hpp"

#include <cmath>
#include <stdexcept>

namespace bw::game {
MissionGameplay::MissionGameplay(
    world::WorldClock& clock, MissionControl& control,
    MissionPlayerState& player, PlayerResources& resources,
    MissionLoading& loading, WorldRuntime& stage, GameplayHudOutput& hud,
    MissionPauseOutput& pause_output, SourceRandom& random,
    audio::CueOutput& audio, HelpControl help, OptionsServices settings,
    CockpitActivation activation)
    : clock_(clock),
      control_(control),
      player_(player),
      resources_(resources),
      stage_(stage),
      interaction_(control, player, loading, stage, pause_output, audio, help,
                   settings),
      cockpit_(hud, random, activation) {
  stage_.bind_action_resources(resources_.actions());
}
MissionFrameDisposition MissionGameplay::begin(float presentation_seconds,
                                               const MissionInput& input) {
  if (frame_) {
    throw std::logic_error("Mission frame already open");
  }
  if (!std::isfinite(presentation_seconds) || presentation_seconds < 0) {
    throw std::invalid_argument("Invalid mission presentation interval");
  }
  const auto interaction = interaction_.admit(presentation_seconds, input);
  frame_.emplace(clock_.publish(presentation_seconds,
                                control_.admission(interaction.ui_consumed)));
  interval_ = {frame_->sequence(), frame_->delta_seconds()};
  simulation_interval_ = {frame_->sequence(), frame_->admission().simulation
                                                  ? frame_->delta_seconds()
                                                  : 0.F};
  resources_.begin_frame(*frame_);
  const auto routed =
      input_.route(*frame_, interaction.actions, player_, resources_);
  actions_ = routed.actions;
  interaction_.observe_spell(routed.spell_status);
  if (actions_.weapon_cycle_pressed) {
    interaction_.cycle_weapon();
  } else if (actions_.weapon_cycle_backward_pressed) {
    interaction_.cycle_weapon(-1);
  }
  return {interaction.ui_consumed, interaction.resource_transaction,
          interaction_.transition_requested()};
}
void MissionGameplay::advance() {
  if (!frame_) {
    throw std::logic_error("Mission frame has not begun");
  }
  stage_.advance(
      *frame_,
      {.movement = {actions_.movement, actions_.evade_held,
                    actions_.crouch_pressed},
       .look = {actions_.look[0], actions_.look[1], actions_.turn_speed,
                actions_.mouse_bias, actions_.mouse_look},
       .presentation = {.fire_trigger = actions_.fire_trigger,
                        .reload_pressed = actions_.reload_pressed,
                        .attack_pressed = actions_.attack_pressed,
                        .aim_mode_pressed = actions_.aim_mode_pressed}});
  resources_.finish_frame();
  const auto damage_channels = stage_.damage_direction_mask();
  constexpr unsigned kDamageDirectionCount = 4;
  for (unsigned channel = 0; channel < kDamageDirectionCount; ++channel) {
    if ((damage_channels & (1U << channel)) != 0) {
      cockpit_.notify_damage(static_cast<DamageDirection>(channel));
    }
  }
  std::optional<MissionGoalObservation> goal_observation;
  if (const auto* camera = stage_.camera_state()) {
    if (const auto* player = stage_.player_state()) {
      goal_observation = MissionGoalObservation{
          camera->pose, camera->perspective, player->pose.position};
    }
  }
  cockpit_.advance(*frame_, resources_.snapshot(),
                   stage_.targeting_state().active(), input_.ring(),
                   goal_observation ? &*goal_observation : nullptr);
  frame_.reset();
}
void MissionGameplay::present() {
  if (frame_) {
    throw std::logic_error("Mission presentation precedes frame completion");
  }
  if (interaction_.paused()) {
    interaction_.present();
  } else {
    cockpit_.show({});
  }
}
}  // namespace bw::game
