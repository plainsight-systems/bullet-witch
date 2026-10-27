// Script forwarding reread Core I.4/Con.2 and performance MEM.9 in the
// implementation phase: no alternate pause state or owner escape is added.
// Recovered game pause flags and neutral-input publication. One owner supplies
// menu, world and player observations; no clock or renderer owns copies.
// Preserve the shared script snapshot order and exclude user pause from it.
// Source malformed-stack behavior is replaced by explicit rejection. Design
// C.9/I.1; implementation E.4 (#re-design-invariants) validates before
// mutation; both-phase MEM.9
// (guidelines/memory/MEM.9-embedded-allocate-at-init.md) keeps stack/routing
// state inline with constant per-frame work and no allocation. Ownership
// correction reread Con.2 (#rconst-fct) and COPY.5 (guidelines/copy-move/
// COPY.5-resource-owning-types-are-move-only.md): the menu handle forwards only
// user-pause commands to a stable owner, with no duplicated flags or
// allocation.
#include "mission_control.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace bw::game {
ScriptPauseControl::ScriptPauseControl(MissionControl& owner) noexcept
    : owner_(&owner) {}
bool ScriptPauseControl::mission_script_admitted() const noexcept {
  return owner_->mission_script_admitted();
}
void ScriptPauseControl::push_script_pause(ScriptPause pause) {
  owner_->push_script_pause(pause);
}
void ScriptPauseControl::pop_script_pause() { owner_->pop_script_pause(); }
ScriptPauseControl MissionControl::script_pause_control() noexcept {
  return ScriptPauseControl{*this};
}
UserPauseControl::UserPauseControl(MissionControl& owner) noexcept
    : owner_(&owner) {}
bool UserPauseControl::user_paused() const noexcept {
  return owner_->user_paused();
}
bool UserPauseControl::set_user_paused(bool paused) {
  return owner_->set_user_paused(paused);
}
UserPauseControl MissionControl::user_pause_control() noexcept {
  return UserPauseControl{*this};
}
bool MissionControl::set_user_paused(bool paused) {
  if (paused == user_paused_) return true;
  if (paused && !script_.menu) return false;
  user_paused_ = paused;
  script_.menu = !paused;
  return true;
}
void MissionControl::push_script_pause(ScriptPause pause) {
  if (pause != ScriptPause::simulation && pause != ScriptPause::input) {
    throw std::invalid_argument("Invalid script pause kind");
  }
  if (depth_ == stack_.size()) {
    throw std::length_error("Script pause stack is full");
  }
  stack_.at(depth_) = script_;
  ++depth_;
  script_.menu = false;
  if (pause == ScriptPause::simulation) {
    script_.simulation = true;
  } else {
    script_.input = true;
  }
}
void MissionControl::pop_script_pause() {
  if (depth_ == 0) throw std::logic_error("Script pause stack is empty");
  script_ = stack_.at(--depth_);
}
world::TickAdmission MissionControl::admission(
    bool ui_consumed) const noexcept {
  const bool simulation = !simulation_paused() && !ui_consumed;
  return {simulation, simulation && !script_.input && !input_restricted_};
}
GameplayInput MissionControl::route(GameplayInput sampled, bool ui_consumed) {
  const auto axis = [](float value) {
    return std::isfinite(value) && std::abs(value) <= 1;
  };
  if (!std::ranges::all_of(sampled.movement, axis) ||
      !std::ranges::all_of(sampled.look, axis) ||
      !std::isfinite(sampled.fire_trigger) || sampled.fire_trigger < 0 ||
      sampled.fire_trigger > 1) {
    throw std::invalid_argument("Invalid sampled gameplay input");
  }
  // Menu edges are consumed now, never queued into the resumed world. Held
  // evade has the accepted release-to-rearm rule; other held input can resume.
  if (ui_consumed || user_paused_) {
    evade_rearmed_ = false;
  } else if (!sampled.evade_held) {
    evade_rearmed_ = true;
  }
  if (!admission(ui_consumed).hardware_input) return {};
  sampled.evade_held = sampled.evade_held && evade_rearmed_;
  return sampled;
}
}  // namespace bw::game
