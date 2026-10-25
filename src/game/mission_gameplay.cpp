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
    MissionLoading& loading, StageInspection& stage, GameplayHudOutput& hud,
    MenuSurface& menu, SourceRandom& random, bool developer_review,
    CockpitActivation activation)
    : clock_(clock),
      control_(control),
      player_(player),
      resources_(resources),
      stage_(stage),
      menu_(menu),
      interaction_(control, player, loading, stage, developer_review),
      cockpit_(hud, random, activation) {
  stage_.bind_action_resources(resources_.actions());
}
MissionFrameDisposition MissionGameplay::begin(float presentation_seconds,
                                               const MissionInput& input) {
  if (frame_) throw std::logic_error("Mission frame already open");
  if (!std::isfinite(presentation_seconds) || presentation_seconds < 0) {
    throw std::invalid_argument("Invalid mission presentation interval");
  }
  const auto interaction = interaction_.admit(input);
  frame_.emplace(clock_.publish(presentation_seconds,
                                control_.admission(interaction.ui_consumed)));
  resources_.begin_frame(*frame_);
  const auto routed =
      input_.route(*frame_, interaction.actions, player_, resources_);
  actions_ = routed.actions;
  interaction_.observe_spell(routed.spell_status);
  if (actions_.weapon_cycle_pressed) interaction_.cycle_weapon();
  return {interaction.ui_consumed, interaction.resource_transaction};
}
void MissionGameplay::advance() {
  if (!frame_) throw std::logic_error("Mission frame has not begun");
  stage_.advance(*frame_, {.movement = {actions_.movement, actions_.evade_held,
                                        actions_.crouch_pressed},
                           .look = {actions_.look[0], actions_.look[1]},
                           .presentation = {
                               .fire_trigger = actions_.fire_trigger,
                               .reload_pressed = actions_.reload_pressed,
                               .attack_pressed = actions_.attack_pressed,
                               .aim_mode_pressed = actions_.aim_mode_pressed}});
  resources_.finish_frame();
  cockpit_.advance(*frame_, resources_.snapshot(),
                   stage_.targeting_state().active(), input_.ring());
  frame_.reset();
}
void MissionGameplay::present(bool developer_overlay) {
  if (frame_) {
    throw std::logic_error("Mission presentation precedes frame completion");
  }
  if (interaction_.paused()) {
    menu_.show(interaction_.presentation());
  } else {
    const auto notice = interaction_.notice();
    cockpit_.show(developer_overlay
                      ? (notice.empty() ? stage_.review_status() : notice)
                      : std::string_view{});
  }
}
}  // namespace bw::game
