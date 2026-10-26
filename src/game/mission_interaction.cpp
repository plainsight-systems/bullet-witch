// Apply pause/equipment requests through their existing owners, preserving
// resource publication and catalog rebinding order. Recoverable transactions
// retain prior appearance/equipment on failure. Core I.1/E.4 and MEM.9 reread
// for implementation: no frame-time allocation outside explicit resource work.
#include "mission_interaction.hpp"

#include <iostream>
#include <stdexcept>

#include "mission_loading.hpp"
#include "player_input_routing.hpp"

namespace bw::game {
MissionInteraction::MissionInteraction(
    MissionControl& control, MissionPlayerState& player,
    MissionLoading& loading, StageInspection& stage,
    MissionPauseOutput& pause_output, audio::CueOutput& audio, HelpControl help,
    PauseOptionsServices settings)
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
      options_(settings),
      developer_(control.user_pause_control(), loading.current_player_costume(),
                 true) {
  bind_review_catalogs();
}
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
void MissionInteraction::bind_review_catalogs() {
  developer_.observe_costume(loading_.current_player_costume());
  const auto* body = stage_.review_catalog(false);
  const auto* weapon = stage_.review_catalog(true);
  if (body != nullptr && weapon != nullptr) {
    developer_.set_available_weapons(player_.levels().weapons);
    developer_.bind_review_catalogs(*body, *weapon,
                                    loading_.current_weapon_class(),
                                    stage_.review_pose_semantics(false),
                                    stage_.review_pose_semantics(true));
  }
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
  // The preserved diagnostic controller has no user-facing admission until
  // its build-specific interface is designed. Normal pause is independent.
  if (!developer_active_ && !ui_frame) {
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
          resource_transaction |=
              pause_output_.activate(MissionPauseView::help);
          help_.start(pause_output_.help_line_counts());
        } else {
          options_.start(loading_.current_player_costume());
        }
      }
    }
  }
  InteractionResult result{control_.route(input.player, ui_frame), ui_frame,
                           resource_transaction};
  stage_.synchronize_pause();
  menu_audio_.submit(pause_.take_sounds());
  menu_audio_.submit(help_.take_sounds());
  menu_audio_.submit(options_.take_sounds());
  if (const auto costume = (developer_active_ ? developer_.requested_costume()
                                              : options_.requested_costume())) {
    bool success = false;
    try {
      stage_.change_costume(loading_, *costume);
      success = true;
    } catch (const std::exception& error) {
      loading_.discard_player_costume();
      std::cerr << "Costume change: " << error.what() << '\n';
    }
    if (developer_active_) {
      developer_.complete_costume(success);
    } else {
      options_.complete_costume(success);
    }
    if (success) {
      bind_review_catalogs();
    }
    result.resource_transaction = true;
  }
  if (const auto weapon_class = developer_.requested_weapon()) {
    bool success = false;
    try {
      if (!player_.weapon_available(*weapon_class)) {
        throw std::invalid_argument("Weapon has not been acquired");
      }
      stage_.change_weapon(loading_, *weapon_class);
      player_.select_weapon(*weapon_class);
      success = true;
    } catch (const std::exception& error) {
      loading_.discard_weapon_class();
      std::cerr << "Weapon preview change: " << error.what() << '\n';
    }
    developer_.complete_weapon(success);
    if (success) {
      bind_review_catalogs();
    }
    result.resource_transaction = true;
  }
  if (const auto review = developer_.requested_review()) {
    developer_.complete_review(stage_.review(*review));
    stage_.synchronize_pause();
    notice_ = {};
  }
  if (!control_.user_paused()) {
    developer_active_ = false;
  }
  view_ = MissionPauseView::none;
  if (developer_active_) {
    view_ = MissionPauseView::developer;
  } else if (pause_.active()) {
    if (pause_.presentation().phase != PauseMenuPhase::child) {
      view_ = MissionPauseView::root;
    } else if (pause_.request() == PauseMenuAction::help) {
      view_ = MissionPauseView::help;
    } else {
      const auto page = options_.presentation().page;
      view_ = page == PauseOptionsPage::costume    ? MissionPauseView::costume
              : page == PauseOptionsPage::controls ? MissionPauseView::controls
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
    case MissionPauseView::costume:
      pause_output_.show(options_.costume_presentation());
      break;
    case MissionPauseView::developer:
      pause_output_.show(developer_.presentation());
      break;
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
    bind_review_catalogs();
    notice_ = "Weapon selected; ammunition and recharge state retained.";
  } else {
    notice_ = "Weapon preview unavailable; current weapon retained.";
  }
}
}  // namespace bw::game
