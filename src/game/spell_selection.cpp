// Magic-ring policy implementation; gameplay-hud.md records source functions.
// Inputs are const value observations; outputs request gameplay admission and
// cannot deduct magic or create effects. Validate first, then update bounded
// state. Fixed work and no allocation. Core F.16 (#rf-in), SL.con.3
// (#rsl-bounds), performance MEM.9
// (guidelines/memory/MEM.9-embedded-allocate-at-init.md) consulted in design
// and implementation preserve narrow authority and bounded command processing.
#include "spell_selection.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>

#include "bw/math/scalar.hpp"

namespace bw::game {
namespace {
constexpr std::uint8_t kPageCount = 3, kChoiceCount = 4;
constexpr float kInactiveLimit = 3, kCenterStart = .1F, kCenterDuration = .1F,
                kRingStart = 1.7F, kRingDuration = .2F, kFlameStart = .6F,
                kFlameDuration = .09F, kArtRotationRate = 13,
                kCloseCenter = .5F, kCloseRing = 1.2F, kCloseFlame = 1.3F,
                kCloseArt = 1.4F, kArtDelay = .13F, kPageFlame = 1.06F,
                kPageAngle = -.4F, kSelectionHold = .11F,
                kConcealedTolerance = .02F, kRotationPeriod = 4;
constexpr std::array<std::array<PlayerSpell, 3>, kPageCount> kPages{
    {{PlayerSpell::ancient_wall, PlayerSpell::elemental_shot,
      PlayerSpell::rose_bomb},
     {PlayerSpell::sacrifice, PlayerSpell::will_power,
      PlayerSpell::raven_panic},
     {PlayerSpell::tornado, PlayerSpell::thunder, PlayerSpell::meteor}}};
}  // namespace
std::optional<PlayerSpell> SpellSelectionFlow::spell(std::uint8_t page,
                                                     std::uint8_t choice) {
  if (page == 0 || page > kPageCount || choice == 0 || choice > kChoiceCount) {
    throw std::invalid_argument("Invalid magic-ring page or choice");
  }
  if (choice == 1) return std::nullopt;
  return kPages.at(page - 1).at(choice - 2);
}
// Each native interpolator retains only its value/velocity. Time comes from
// the caller; response modes are shared arithmetic plus native parameter data.
void SpellSelectionFlow::Scalar::update(float seconds) noexcept {
  switch (response) {
    case Response::linear:
      value = value < target ? std::min(target, value + parameter * seconds)
                             : std::max(target, value - parameter * seconds);
      break;
    case Response::exponential:
      value = math::linear_interpolate(value, target,
                                       std::min(1.0F, parameter * seconds));
      break;
    case Response::damped: {
      const auto next = math::damped_response(
          math::DampedResponseState{value, velocity},
          math::DampedResponseTarget{target, parameter}, seconds);
      value = next.value;
      velocity = next.velocity;
      break;
    }
  }
}
void SpellSelectionFlow::begin() noexcept {
  active_ = true;
  closing_ = false;
  prior_keepalive_ = false;
  phase_ = Phase::opening;
  phase_step_ = 0;
  page_ = 1;
  selected_ = 0;
  phase_elapsed_ = elapsed_ = rotation_elapsed_ = inactive_ = 0;
  opacity_ = {0, 1, 3};
  center_scale_ = {kCenterStart, 1, kCenterDuration, 0, Response::damped};
  ring_scale_ = {kRingStart, 1, kRingDuration, 0, Response::damped};
  name_opacity_ = {0, 1, 4};
  flame_scale_ = {kFlameStart, 1, kFlameDuration, 0, Response::damped};
  art_opacity_ = {0, 0, 4};
  constexpr float kInitialAngle = -20 * std::numbers::pi_v<float> / 180;
  art_rotation_ = {kInitialAngle, kInitialAngle, kArtRotationRate, 0,
                   Response::exponential};
  art_scale_ = {1, 1, kFlameDuration, 0, Response::damped};
  selection_progress_ = {0, 0, 4};
}
void SpellSelectionFlow::close() noexcept {
  closing_ = true;
  phase_ = Phase::closing;
  opacity_.target = 0;
  center_scale_.target = kCloseCenter;
  ring_scale_.target = kCloseRing;
  name_opacity_.target = 0;
  flame_scale_.target = kCloseFlame;
  art_opacity_.target = 0;
  art_scale_.target = kCloseArt;
}
// Native opening/page phases consume their own updates before admitting face
// input. A choice publishes on the following update and remains highlighted
// for .11 seconds before the input-active latch expires and art fades.
SpellSelectionResult SpellSelectionFlow::advance(
    const SpellSelectionInput& input, const SpellAvailabilitySet& availability,
    float seconds) {
  if (!std::isfinite(seconds) || seconds < 0 || input.choice > kChoiceCount) {
    throw std::invalid_argument("Invalid magic-ring observation");
  }
  SpellSelectionResult result;
  if (seconds == 0) return result;
  availability_ = availability;
  const bool opening_input =
      input.open || input.previous_page || input.next_page || input.expire;
  const bool was_admitted = active();
  const bool newly_opened = opening_input && !was_admitted && !prior_keepalive_;
  if (newly_opened) {
    begin();
    inactive_ = seconds;
    if (input.expire) {
      closing_ = true;
      result.cancelled = true;
    }
    prior_keepalive_ = true;
    return result;
  }
  if (!active_) return result;
  // The original action owner supplies keepalive using the previous input
  // latch. Expiry revokes input this frame; visual idle observes the missing
  // keepalive on the following frame, after any opening/page transition.
  const bool keepalive = !input.cancel && (was_admitted || opening_input);
  if (opening_input) {
    inactive_ = seconds;
    closing_ = false;
  } else if (was_admitted) {
    inactive_ += seconds;
  }
  if (input.expire || inactive_ >= kInactiveLimit) {
    if (!closing_) result.cancelled = input.expire;
    inactive_ = 0;
    closing_ = true;
  }
  if (input.cancel) {
    if (!closing_) result.cancelled = true;
    // RS retires the native magic action task, withdrawing keepalive. It does
    // not force the visual state into its closing interpolators.
    closing_ = true;
  }
  switch (phase_) {
    case Phase::opening:
      if (++phase_step_ >= 3) {
        phase_ = Phase::idle;
        phase_elapsed_ = -seconds;
      }
      break;
    case Phase::idle:
      if (!keepalive) {
        phase_ = Phase::close_begin;
        break;
      }
      if (closing_) break;
      if (phase_elapsed_ >= kArtDelay) {
        art_opacity_.target = 1;
        art_rotation_.target = 0;
      }
      if (input.previous_page || input.next_page) {
        if (input.previous_page) {
          page_ = page_ == 1 ? kPageCount : page_ - 1;
        } else {
          page_ = page_ == kPageCount ? 1 : page_ + 1;
        }
        flame_scale_.value = kPageFlame;
        art_rotation_.value = kPageAngle;
        art_rotation_.target = 0;
        art_opacity_.value = kFlameStart;
        art_opacity_.target = 1;
        phase_ = Phase::page_change;
        phase_step_ = 0;
      } else if (input.choice != 0) {
        const auto requested = spell(page_, input.choice);
        const bool permitted =
            !requested ||
            (availability_.at(static_cast<std::size_t>(*requested)).learned &&
             availability_.at(static_cast<std::size_t>(*requested)).permitted);
        selected_ = input.choice;
        if (permitted) {
          phase_ = Phase::publish;
        } else {
          result.rejected = true;
        }
      }
      break;
    case Phase::page_change:
      if (++phase_step_ >= 3) phase_ = Phase::idle;
      break;
    case Phase::publish:
      if (!closing_) {
        result.requested = spell(page_, selected_);
        result.cancelled = !result.requested;
      }
      selection_progress_.target = 1;
      phase_ = Phase::selected;
      phase_elapsed_ = 0;
      break;
    case Phase::selected:
      if (phase_elapsed_ >= kSelectionHold) phase_ = Phase::expire;
      break;
    case Phase::expire:
      closing_ = true;
      phase_ = Phase::release;
      break;
    case Phase::release:
      phase_ = Phase::close_begin;
      break;
    case Phase::close_begin:
      phase_ = Phase::close_prepare;
      break;
    case Phase::close_prepare:
      close();
      break;
    case Phase::closing:
      if (std::abs(opacity_.value) < kConcealedTolerance &&
          std::abs(name_opacity_.value) < kConcealedTolerance) {
        active_ = false;
      }
      break;
  }
  opacity_.update(seconds);
  center_scale_.update(seconds);
  ring_scale_.update(seconds);
  name_opacity_.update(seconds);
  flame_scale_.update(seconds);
  art_opacity_.update(seconds);
  art_rotation_.update(seconds);
  art_scale_.update(seconds);
  selection_progress_.update(seconds);
  rotation_elapsed_ = elapsed_;
  elapsed_ += seconds;
  phase_elapsed_ += seconds;
  prior_keepalive_ = keepalive;
  return result;
}
SpellRingPresentation SpellSelectionFlow::presentation() const {
  SpellRingPresentation result;
  if (!active_) return result;
  result.visible = true;
  result.page = page_;
  result.selected_choice = selected_;
  result.opacity = opacity_.value;
  result.rotation_radians = std::fmod(rotation_elapsed_, kRotationPeriod) *
                            (2 * std::numbers::pi_v<float> / 4);
  result.center_scale = center_scale_.value;
  result.ring_scale = ring_scale_.value;
  result.name_opacity = name_opacity_.value;
  result.flame_scale = flame_scale_.value;
  result.art_opacity = art_opacity_.value;
  result.art_rotation = art_rotation_.value;
  result.art_scale = art_scale_.value;
  result.selection_progress = selection_progress_.value;
  result.elapsed_seconds = elapsed_;
  result.choices.at(0) = {true, true};
  const auto& page = kPages.at(page_ - 1);
  for (std::size_t index = 0; index < page.size(); ++index) {
    result.choices.at(index + 1) =
        availability_.at(static_cast<std::size_t>(page.at(index)));
  }
  return result;
}
void SpellSelectionFlow::reset() noexcept { *this = {}; }
}  // namespace bw::game
