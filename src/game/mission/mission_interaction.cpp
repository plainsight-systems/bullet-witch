// Debug separation: Core I.1 (#ri-explicit), R.3 (#rr-ptr), and performance
// TLM.1 (guidelines/telemetry/TLM.1-compile-telemetry-out-by-default.md),
// retrieved in design and implementation, keep preview capabilities bounded
// and compile out their owner/state/calls in ordinary production builds.
// Apply pause/equipment requests through their existing owners, preserving
// resource publication and catalog rebinding order. Recoverable transactions
// retain prior appearance/equipment on failure. Core I.1/E.4 and MEM.9 reread
// for implementation: no frame-time allocation outside explicit resource work.
#include "mission_interaction.hpp"

#include <iostream>
#include <stdexcept>

#include "game/mission/loading/mission_loading.hpp"
#include "game/player/input/player_input_routing.hpp"

namespace bw::game {
MissionInteraction::MissionInteraction(
    MissionControl& control, MissionPlayerState& player,
    MissionLoading& loading, WorldRuntime& stage,
    MissionPauseOutput& pause_output, audio::CueOutput& audio, HelpControl help,
    OptionsServices settings)
    : control_(control),
      player_(player),
      loading_(loading),
      stage_(stage),
      pause_output_(pause_output),
      menu_audio_(audio),
      pause_(control.user_pause_control(),
             {.checkpoint = player.origin() != ProgressionOrigin::checkpoint,
              .restart_stage = true,
              .retire = true}),
      help_state_(help, player),
      help_(help_state_),
      options_(settings) {}
MissionTransition MissionInteraction::transition_requested() const {
  const auto request = pause_.request();
  if (request == PauseMenuAction::retire) {
    return MissionTransition::retire;
  }
  if (request == PauseMenuAction::restart_stage) {
    return MissionTransition::restart_stage;
  }
  if (request == PauseMenuAction::checkpoint) {
    return MissionTransition::checkpoint;
  }
  return MissionTransition::none;
}
InteractionResult MissionInteraction::admit(float seconds,
                                            const MissionInput& input) {
  menu_audio_.advance(seconds);
  prompts_ = menu_prompts(input.physical.controller_connected);
  help_icons_ = input.help_icons;
  if (input.pause_pressed || input.player.reload_pressed ||
      input.player.attack_pressed || input.player.aim_mode_pressed ||
      input.player.fire_trigger > 0) {
    notice_ = {};
  }
  bool resource_transaction = false;
  bool ui_frame = false;
  if (pause_.presentation().phase == PauseMenuPhase::child) {
    ui_frame = true;
    if (pause_.request() == PauseMenuAction::help) {
      constexpr float kHelpScrollPixels = 16;
      help_.update(input.menu, seconds,
                   input.menu.vertical_axis * kHelpScrollPixels);
      if (help_.completed()) {
        pause_.complete_child();
      }
    } else {
      options_.update(input.menu, seconds, input.physical);
      if (options_.completed()) {
        pause_.complete_child();
      }
    }
  } else {
    ui_frame = pause_.update(seconds, input.pause_pressed, input.menu);
    if (pause_.presentation().phase == PauseMenuPhase::child) {
      if (pause_.request() == PauseMenuAction::help) {
        resource_transaction |= pause_output_.activate(MissionPauseView::help);
        help_.start(pause_output_.help_line_counts());
      } else {
        options_.start(loading_.current_player_costume());
      }
    }
  }
  InteractionResult result{control_.route(input.player, ui_frame), ui_frame,
                           resource_transaction};
  stage_.synchronize_pause();
  menu_audio_.submit(pause_.take_sounds());
  menu_audio_.submit(help_.take_sounds());
  menu_audio_.submit(options_.take_sounds());
  if (const auto costume = options_.requested_costume()) {
    bool success = false;
    try {
      stage_.change_costume(loading_, *costume);
      success = true;
    } catch (const std::exception& error) {
      loading_.discard_player_costume();
      std::cerr << "Costume change: " << error.what() << '\n';
    }
    options_.complete_costume(success);
    result.resource_transaction = true;
  }
  view_ = MissionPauseView::none;
  if (pause_.active()) {
    if (pause_.presentation().phase != PauseMenuPhase::child) {
      view_ = MissionPauseView::root;
    } else if (pause_.request() == PauseMenuAction::help) {
      view_ = MissionPauseView::help;
    } else {
      const auto page = options_.presentation().page;
      view_ = page == OptionsPage::costume    ? MissionPauseView::costume
              : page == OptionsPage::controls ? MissionPauseView::controls
                                              : MissionPauseView::options;
    }
  }
  result.resource_transaction |= pause_output_.activate(view_);
  return result;
}
float MissionInteraction::presentation_opacity() const {
  return pause_.request() == PauseMenuAction::options
             ? options_.presentation().opacity
             : 1.F;
}
void MissionInteraction::present() {
  switch (view_) {
    case MissionPauseView::none:
      break;
    case MissionPauseView::root: {
      auto view = pause_.presentation();
      view.prompts = prompts_;
      pause_output_.show(view);
      break;
    }
    case MissionPauseView::help: {
      auto view = help_.presentation();
      view.prompts = prompts_;
      view.input_icons = help_icons_;
      pause_output_.show(view);
      break;
    }
    case MissionPauseView::options: {
      auto view = options_.presentation();
      view.prompts = prompts_;
      pause_output_.show(view);
      break;
    }
    case MissionPauseView::controls: {
      auto view = options_.controller_presentation();
      view.prompts = prompts_;
      pause_output_.show(view);
      break;
    }
    case MissionPauseView::costume: {
      // The approved mission costume switch has no frontend entry track. Its
      // owner publishes the settled original card layout and live appearance.
      auto view = options_.costume_presentation();
      constexpr float kCostumeRowPitch = 172;
      view.prompts = prompts_;
      view.child.motion = ChildMenuMotion::active;
      view.child.scroll = -kCostumeRowPitch * static_cast<float>(view.selected);
      view.child.current_costume = loading_.current_player_costume();
      view.child.highlight.at(view.selected) = 1;
      pause_output_.show(view);
      break;
    }
  }
}
void MissionInteraction::observe_spell(SpellRequestStatus status) {
  if (status == SpellRequestStatus::unavailable) {
    notice_ =
        "Spell unavailable: check learned level, magic and mission "
        "restrictions.";
  } else if (status == SpellRequestStatus::missing_world_owner) {
    notice_ =
        "Cast unavailable: required world or target owner is not yet admitted.";
  }
}
void MissionInteraction::cycle_weapon(int direction) {
  const auto* player = stage_.player_state();
  if (player == nullptr || player->phase != PlayerMovementPhase::grounded) {
    notice_ = "Weapon preview change needs grounded Alicia.";
    return;
  }
  bool success = false;
  try {
    if (player_.selected_weapon() != loading_.current_weapon_class()) {
      throw std::logic_error("Player and resident weapon disagree");
    }
    const auto next_weapon = player_.next_weapon(direction);
    if (next_weapon != player_.selected_weapon()) {
      stage_.switch_weapon(loading_, next_weapon);
      player_.select_weapon(next_weapon);
      success = true;
    }
  } catch (const std::exception& error) {
    loading_.discard_weapon_class();
    std::cerr << "Weapon preview change: " << error.what() << '\n';
  }
  if (success) {
    notice_ = "Weapon selected; ammunition and recharge state retained.";
  } else {
    notice_ = "Weapon preview unavailable; current weapon retained.";
  }
}
}  // namespace bw::game
