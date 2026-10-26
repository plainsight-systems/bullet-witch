// Original post-result category/upgrade/confirmation navigation and purchase
// transactions. UI publishes copied values, with no competing progression copy.
// Categories/rows use vertical repeated input; the shared Yes/No dialog uses
// horizontal input and Back selects No without closing. No generic Back row.
// Source mapping: power-up-presentation.md. Core C.9/I.4/Con.2 design and E.4
// implementation preserve restricted, checked purchases; both-phase MEM.9
// bounds state without frame allocation.
#include "power_up_flow.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace bw::game {
namespace {
constexpr std::uint32_t kConfirm = 0x1010, kCancel = 0x2020, kUp = 0x01000000,
                        kDown = 0x02000000, kLeft = 0x04000000,
                        kRight = 0x08000000;
constexpr float kOuterEntrySeconds = 1.2F, kChildEntrySeconds = .7F,
                kDialogTolerance = .01F, kDialogInputSeconds = .3F,
                kExitConcealSeconds = .4F, kDialogRate = 4.4F,
                kWalletRate = 3.F, kCategoryRate = 2.3F,
                kDescriptionReset = .5F, kPulseMaximum = .5F,
                kCategoryBobAmplitude = 9.496299743652344F,
                kCategoryBobPeriod = 3.530513048171997F, kPromptRate = 4.F,
                kSelectionKick = 26.136789321899414F,
                kSelectionFollowSeconds = .01024600025266409F,
                kSelectionKickSeconds = .020464999601244926F;
constexpr std::size_t kContinue = 3, kCategoryRows = 4, kPurchasableSpells = 6,
                      kDialogChoices = 2;
}  // namespace
PowerUpFlow::PowerUpFlow(UpgradePurchaseControl purchases)
    : purchases_(purchases) {
  start();
}
MenuSoundRequests PowerUpFlow::take_sounds() {
  return std::exchange(sounds_, {});
}
void PowerUpFlow::start() {
  sounds_ = {};
  sounds_.add(MenuSound::panel);
  conceal_requested_ = false;
  page_ = PowerUpPage::categories;
  category_ = UpgradeCategory::stat;
  focused_ = 0;
  upgrade_row_ = 0;
  notice_ = PowerUpNotice::none;
  phase_ = Phase::outer_entry;
  phase_seconds_ = outer_seconds_ = child_seconds_ = dialog_progress_ = 0.F;
  wallet_opacity_ = description_target_ = 0.F;
  category_strength_ = category_target_ = 1.F;
  description_ = {};
  category_motion_ = {};
  child_pulse_seconds_ = child_highlight_ = 0.F;
  dialog_gate_open_ = purchase_confirmed_ = false;
  category_bob_seconds_ = category_bob_ = 0.F;
  outer_prompt_ = child_prompt_ = outer_prompt_target_ = child_prompt_target_ =
      0.F;
}
void PowerUpFlow::transition(Phase phase) {
  phase_ = phase;
  phase_seconds_ = 0.F;
}
bool PowerUpFlow::accepts_input() const noexcept {
  return !conceal_requested_ &&
         (phase_ == Phase::categories || phase_ == Phase::upgrades ||
          phase_ == Phase::dialog);
}
PowerUpPresentation PowerUpFlow::presentation() const {
  PowerUpPresentation result;
  result.page = page_;
  result.category = category_;
  result.focused = focused_;
  result.upgrade_row = upgrade_row_;
  result.progression = purchases_.progression();
  result.notice = notice_;
  result.outer_seconds = outer_seconds_;
  result.child_seconds = child_seconds_;
  result.dialog_progress = dialog_progress_;
  result.outer_exiting = phase_ == Phase::outer_exit;
  result.child_exiting = phase_ == Phase::child_exit;
  result.dialog_exiting = phase_ == Phase::dialog_close;
  result.wallet_opacity = wallet_opacity_;
  result.category_description_opacity = description_.value;
  result.category_selection_strength = category_strength_;
  result.child_highlight_strength = child_highlight_;
  result.category_bob = category_bob_;
  for (std::size_t row = 0; row < category_motion_.size(); ++row) {
    result.category_offsets.at(row) =
        category_motion_.at(row).displacement.value;
  }
  result.outer_prompt_opacity = outer_prompt_;
  result.child_prompt_opacity = child_prompt_;
  result.dialog_continue_visible =
      dialog_gate_open_ && notice_ != PowerUpNotice::none;
  return result;
}
std::size_t PowerUpFlow::row_count() const {
  if (page_ == PowerUpPage::categories) return kCategoryRows;
  if (page_ == PowerUpPage::confirmation) {
    return notice_ == PowerUpNotice::none ? kDialogChoices : 1;
  }
  switch (category_) {
    case UpgradeCategory::stat:
      return player_stat_count;
    case UpgradeCategory::weapon:
      return player_weapon_count;
    case UpgradeCategory::spell:
      return kPurchasableSpells;
  }
  throw std::logic_error("Invalid power-up category");
}
UpgradeSelection PowerUpFlow::selected_upgrade() const {
  return {category_, upgrade_row_};
}
void PowerUpFlow::show_upgrades() {
  page_ = PowerUpPage::upgrades;
  focused_ = upgrade_row_;
  notice_ = PowerUpNotice::none;
  dialog_gate_open_ = false;
  transition(Phase::child_reveal);
}
void PowerUpFlow::navigate(int direction) {
  sounds_.add(MenuSound::cursor);
  const auto count = row_count();
  if (direction < 0) {
    focused_ = focused_ == 0 ? count - 1 : focused_ - 1;
  } else {
    focused_ = (focused_ + 1) % count;
  }
  if (page_ == PowerUpPage::categories) {
    description_.value = kDescriptionReset;
    category_motion_.at(focused_).kick.value =
        direction < 0 ? -kSelectionKick : kSelectionKick;
  } else if (page_ == PowerUpPage::upgrades) {
    upgrade_row_ = static_cast<unsigned>(focused_);
  }
}
void PowerUpFlow::accept() {
  if (page_ != PowerUpPage::confirmation || notice_ == PowerUpNotice::none) {
    sounds_.add(MenuSound::confirm);
  }
  if (page_ == PowerUpPage::confirmation) {
    sounds_.add(MenuSound::dialog_close);
  }
  if (page_ == PowerUpPage::categories) {
    outer_prompt_target_ = 0.F;
    if (focused_ == kContinue) {
      category_target_ = 0.F;
      transition(Phase::outer_hide);
      return;
    }
    category_ = static_cast<UpgradeCategory>(focused_);
    upgrade_row_ = 0;
    category_target_ = 0.F;
    transition(Phase::child_start);
    return;
  }
  if (page_ == PowerUpPage::upgrades) {
    upgrade_row_ = static_cast<unsigned>(focused_);
    transition(Phase::quote_hide);
    return;
  }
  purchase_confirmed_ = notice_ == PowerUpNotice::none && focused_ == 0;
  transition(Phase::dialog_accept);
}
void PowerUpFlow::open_dialog() {
  sounds_.add(MenuSound::dialog_open);
  const auto progression = purchases_.progression();
  const auto cost = upgrade_cost(progression, selected_upgrade());
  notice_ = !cost                        ? PowerUpNotice::maximum_level
            : *cost > progression.points ? PowerUpNotice::insufficient_points
                                         : PowerUpNotice::none;
  focused_ = 0;
  page_ = PowerUpPage::confirmation;
  dialog_progress_ = 0.F;
  dialog_gate_open_ = false;
  transition(Phase::dialog_open);
}
void PowerUpFlow::finish_dialog() {
  if (!purchase_confirmed_) {
    show_upgrades();
    return;
  }
  // The original task commits after the shared dialog reports retirement.
  // Profile revalidates the current Record; a displayed quote has no authority.
  const auto result = purchases_.purchase(selected_upgrade());
  purchase_confirmed_ = false;
  if (result == PurchaseResult::purchased) {
    sounds_.add(MenuSound::upgrade_purchase);
    show_upgrades();
    return;
  }
  notice_ = result == PurchaseResult::insufficient_points
                ? PowerUpNotice::insufficient_points
                : PowerUpNotice::maximum_level;
  focused_ = 0;
  dialog_progress_ = 0.F;
  dialog_gate_open_ = false;
  sounds_.add(MenuSound::dialog_open);
  transition(Phase::dialog_open);
}

void PowerUpFlow::cancel() {
  sounds_.add(MenuSound::back);
  if (page_ == PowerUpPage::confirmation) {
    if (notice_ == PowerUpNotice::none) focused_ = 1;
  } else if (page_ == PowerUpPage::upgrades) {
    transition(Phase::child_hide);
  } else if (focused_ == kContinue) {
    transition(Phase::outer_hide);
  } else {
    // Original category Back selects Continue first; a subsequent Back exits.
    focused_ = kContinue;
    category_motion_.at(focused_).kick.value = kSelectionKick;
    description_.value = kDescriptionReset;
  }
}
void PowerUpFlow::advance_visuals(float seconds) {
  constexpr float kDamping = .17F;
  phase_seconds_ += seconds;
  outer_seconds_ = phase_ == Phase::outer_exit
                       ? outer_seconds_ + seconds
                       : std::min(kOuterEntrySeconds, outer_seconds_ + seconds);
  if (page_ != PowerUpPage::categories) {
    child_seconds_ =
        phase_ == Phase::child_exit
            ? child_seconds_ + seconds
            : std::min(kChildEntrySeconds, child_seconds_ + seconds);
    // Original periodic response samples before advancing its one-second phase.
    child_highlight_ = math::periodic_sine_interpolate(
        math::PeriodicSineEndpoints<float>{0.F, kPulseMaximum},
        child_pulse_seconds_);
    child_pulse_seconds_ = std::fmod(child_pulse_seconds_ + seconds, 1.F);
  }
  // Source selector updates the follower from the previous kick value before
  // damping the kick toward zero. All four rows retain their own velocity.
  for (auto& motion : category_motion_) {
    motion.displacement = math::damped_response(
        motion.displacement, {motion.kick.value, kSelectionFollowSeconds},
        seconds);
    motion.kick = math::damped_response(motion.kick,
                                        {0.F, kSelectionKickSeconds}, seconds);
  }
  category_bob_ = math::periodic_sine_interpolate(
      math::PeriodicSineEndpoints<float>{-kCategoryBobAmplitude,
                                         kCategoryBobAmplitude},
      category_bob_seconds_ / kCategoryBobPeriod);
  category_bob_seconds_ =
      std::fmod(category_bob_seconds_ + seconds, kCategoryBobPeriod);
  const auto prompt = [seconds](float value, float target) {
    return value < target ? std::min(target, value + kPromptRate * seconds)
                          : std::max(target, value - kPromptRate * seconds);
  };
  outer_prompt_ = prompt(outer_prompt_, outer_prompt_target_);
  child_prompt_ = prompt(child_prompt_, child_prompt_target_);
  if (phase_ != Phase::outer_entry) {
    wallet_opacity_ = std::min(1.F, wallet_opacity_ + kWalletRate * seconds);
  }
  if (category_target_ > category_strength_) {
    category_strength_ = std::min(category_target_,
                                  category_strength_ + kCategoryRate * seconds);
  } else {
    category_strength_ = std::max(category_target_,
                                  category_strength_ - kCategoryRate * seconds);
  }
  description_ = bw::math::damped_response(
      description_, {description_target_, kDamping}, seconds);
  if (phase_ == Phase::dialog_open) {
    dialog_progress_ = std::min(1.F, dialog_progress_ + kDialogRate * seconds);
  } else if (phase_ == Phase::dialog_close) {
    dialog_progress_ = std::max(0.F, dialog_progress_ - kDialogRate * seconds);
  }
}
void PowerUpFlow::update(MenuInput input, float elapsed_seconds) {
  if (!std::isfinite(elapsed_seconds) || elapsed_seconds < 0.F) {
    throw std::invalid_argument("Invalid power-up presentation time");
  }
  sounds_ = {};
  if (conceal_requested_) {
    advance_visuals(elapsed_seconds);
    return;
  }
  if (!accepts_input()) {
    switch (phase_) {
      case Phase::outer_entry:
        if (outer_seconds_ >= kOuterEntrySeconds) {
          description_target_ = 1.F;
          transition(Phase::category_reveal);
        }
        break;
      case Phase::category_reveal:
        outer_prompt_target_ = category_target_ = 1.F;
        transition(Phase::categories);
        break;
      case Phase::child_start:
        page_ = PowerUpPage::upgrades;
        focused_ = 0;
        child_seconds_ = child_pulse_seconds_ = 0.F;
        child_prompt_target_ = description_target_ = 0.F;
        transition(Phase::child_entry);
        break;
      case Phase::child_entry:
        if (child_seconds_ >= kChildEntrySeconds) show_upgrades();
        break;
      case Phase::child_reveal:
        child_prompt_target_ = 1.F;
        transition(Phase::upgrades);
        break;
      case Phase::quote_hide:
        child_prompt_target_ = 0.F;
        transition(Phase::quote_route);
        break;
      case Phase::quote_route:
        transition(Phase::quote);
        break;
      case Phase::quote:
        open_dialog();
        break;
      case Phase::dialog_open:
        if (dialog_progress_ >= 1.F - kDialogTolerance &&
            phase_seconds_ >= kDialogInputSeconds) {
          dialog_gate_open_ = true;
          transition(Phase::dialog);
        }
        break;
      case Phase::dialog_accept:
        transition(Phase::dialog_close);
        break;
      case Phase::dialog_close:
        if (dialog_progress_ <= kDialogTolerance) {
          transition(Phase::dialog_retire);
        }
        break;
      case Phase::dialog_retire:
        transition(Phase::dialog_finish);
        break;
      case Phase::dialog_finish:
        transition(Phase::purchase);
        break;
      case Phase::purchase:
        finish_dialog();
        break;
      case Phase::child_hide:
        child_prompt_target_ = 0.F;
        category_target_ = 1.F;
        transition(Phase::child_exit_start);
        break;
      case Phase::child_exit_start:
        child_seconds_ = 0.F;
        transition(Phase::child_exit);
        break;
      case Phase::child_exit:
        if (child_seconds_ >= kChildEntrySeconds) {
          page_ = PowerUpPage::categories;
          focused_ = static_cast<std::size_t>(category_);
          description_target_ = 1.F;
          transition(Phase::category_reveal);
        }
        break;
      case Phase::outer_hide:
        outer_prompt_target_ = 0.F;
        transition(Phase::outer_exit_start);
        break;
      case Phase::outer_exit_start:
        outer_seconds_ = 0.F;
        description_target_ = 0.F;
        transition(Phase::outer_exit);
        break;
      case Phase::outer_exit:
        if (outer_seconds_ >= kExitConcealSeconds) conceal_requested_ = true;
        break;
      case Phase::categories:
      case Phase::upgrades:
      case Phase::dialog:
        break;
    }
    advance_visuals(elapsed_seconds);
    return;
  }
  // All paths advance observations once, after policy consumes this frame.
  const auto apply = [&] {
    if ((input.pressed & kConfirm) != 0) {
      accept();
      return;
    }
    if (page_ == PowerUpPage::confirmation) {
      if (notice_ != PowerUpNotice::none) return;
      // Dialog applies horizontal navigation, then Back overrides the result.
      if ((input.repeated & kLeft) != 0) {
        navigate(-1);
      } else if ((input.repeated & kRight) != 0) {
        navigate(1);
      }
      if ((input.pressed & kCancel) != 0) cancel();
    } else if ((input.pressed & kCancel) != 0) {
      cancel();
    } else if ((input.repeated & kUp) != 0) {
      navigate(-1);
    } else if ((input.repeated & kDown) != 0) {
      navigate(1);
    }
  };
  apply();
  advance_visuals(elapsed_seconds);
}
}  // namespace bw::game
