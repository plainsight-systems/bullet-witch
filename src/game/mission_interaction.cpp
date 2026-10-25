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
MissionInteraction::MissionInteraction(MissionControl& control,
                                       MissionPlayerState& player,
                                       MissionLoading& loading,
                                       StageInspection& stage,
                                       bool developer_review)
    : control_(control),
      player_(player),
      loading_(loading),
      stage_(stage),
      pause_(control.user_pause_control(), loading.current_player_costume(),
             developer_review) {
  bind_review_catalogs();
}
void MissionInteraction::bind_review_catalogs() {
  const auto* body = stage_.review_catalog(false);
  const auto* weapon = stage_.review_catalog(true);
  if (body != nullptr && weapon != nullptr) {
    pause_.set_available_weapons(player_.levels().weapons);
    pause_.bind_review_catalogs(*body, *weapon, loading_.current_weapon_class(),
                                stage_.review_pose_semantics(false),
                                stage_.review_pose_semantics(true));
  }
}
InteractionResult MissionInteraction::admit(const MissionInput& input) {
  if (input.pause_pressed || input.player.reload_pressed ||
      input.player.attack_pressed || input.player.aim_mode_pressed ||
      input.player.fire_trigger > 0) {
    notice_ = {};
  }
  const bool ui_frame = pause_.update(input.pause_pressed, input.menu);
  InteractionResult result{control_.route(input.player, ui_frame), ui_frame,
                           false};
  stage_.synchronize_pause();
  if (const auto costume = pause_.requested_costume()) {
    bool success = false;
    try {
      stage_.change_costume(loading_, *costume);
      success = true;
    } catch (const std::exception& error) {
      loading_.discard_player_costume();
      std::cerr << "Costume change: " << error.what() << '\n';
    }
    pause_.complete_costume(success);
    if (success) bind_review_catalogs();
    result.resource_transaction = true;
  }
  if (const auto weapon_class = pause_.requested_weapon()) {
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
    pause_.complete_weapon(success);
    if (success) bind_review_catalogs();
    result.resource_transaction = true;
  }
  if (const auto review = pause_.requested_review()) {
    pause_.complete_review(stage_.review(*review));
    stage_.synchronize_pause();
    notice_ = {};
  }
  return result;
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
void MissionInteraction::cycle_weapon() {
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
    const auto next_weapon = player_.next_weapon();
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
