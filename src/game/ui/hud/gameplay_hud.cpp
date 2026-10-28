// Native HUD command reconstruction; source map and original-x86 witnesses in
// gameplay-hud.md. Validate copied observations, update visual-only trails and
// transitions with supplied time, then publish a bounded command view. No game
// state, renderer, clock or resource authority is borrowed. Core F.16 (#rf-in)
// and implementation SL.con.3 (#rsl-bounds) preserve input constness/capacity;
// performance MEM.9 (guidelines/memory/MEM.9-embedded-allocate-at-init.md)
// fixes storage and performs no per-frame allocation. Both phases consulted.
#include "gameplay_hud.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>

#include "bw/math/scalar.hpp"

namespace bw::game {
namespace {
constexpr float kMeterWidth = 302, kMeterRight = 1173, kMeterHeight = 16;
constexpr float kTrailDuration = .65F, kSettledTolerance = .01F,
                kGainTolerance = .0001F, kGainDuration = .3F;
constexpr float kNoticeHiddenX = 400, kHealthY = 60, kMagicY = 89;
constexpr unsigned kGaugeImage = 9, kMarkerImage = 14, kBackgroundImage = 6,
                   kWeaponBackgroundImage = 17, kFirstWeaponImage = 18,
                   kRingOuterImage = 133, kRingInnerImage = 134,
                   kRingCenterImage = 101, kFlameImage = 121,
                   kEmptyFlameImage = 435, kSelectionFlameImage = 436,
                   kMagicReticleImage = 41, kDefaultReticleImage = 39;
constexpr render::Rect kBackgroundPosition{858, 30, 0, 0},
    kOuterRingPosition{507, 227, 0, 0}, kInnerRingPosition{535, 255, 0, 0},
    kRingCenterPosition{473, 193, 0, 0}, kFlamePosition{472, 137, 0, 0},
    kReticlePosition{640, 360, 104, 104};
constexpr render::Color kWarningColor{1, .4F, .4F, 1};
constexpr render::Color kHealth{178 / 255.0F, 127 / 255.0F, 173 / 255.0F, 1};
constexpr render::Color kMagic{107 / 255.0F, 146 / 255.0F, 145 / 255.0F, 1};
constexpr render::Color kTrail{153 / 255.0F, 201 / 255.0F, 204 / 255.0F, 1};
constexpr std::array<unsigned, 4> kAimedReticle{40, 42, 37, 38};
constexpr float kHalf = .5F, kColorByteMaximum = 255;
float packed_channel(float value) {
  return static_cast<float>(static_cast<unsigned>(
             std::clamp(value, 0.0F, 1.0F) * kColorByteMaximum)) /
         kColorByteMaximum;
}
void validate(const HudResourceMeter& meter) {
  if (!std::isfinite(meter.current) || !std::isfinite(meter.recoverable) ||
      !std::isfinite(meter.maximum) || meter.maximum <= 0 ||
      meter.current < 0 || meter.current > meter.recoverable ||
      meter.recoverable > meter.maximum) {
    throw std::invalid_argument("Invalid HUD resource observation");
  }
}
}  // namespace
void GameplayHud::reset() noexcept {
  count_ = 0;
  ammunition_.reset();
  damage_.reset();
  goal_.reset();
  health_ = {};
  magic_ = {};
  initialized_ = false;
  weapon_ = 0;
  notice_phase_ = NoticePhase::begin;
  notice_visible_ = false;
  notice_elapsed_ = 0;
  notice_x_ = kNoticeHiddenX;
  notice_y_ = 0;
  notice_velocity_ = 0;
  notice_y_velocity_ = 0;
  warning_elapsed_ = 0;
  warning_strength_ = 0;
  warning_active_ = false;
  reticle_hit_remaining_ = 0;
  notice_x_target_ = notice_y_target_ = 0;
}
void GameplayHud::append(HudSprite sprite) {
  if (count_ == commands_.size()) {
    throw std::length_error("HUD command capacity exceeded");
  }
  commands_.at(count_++) = sprite;
}
// Recoverable resource is the source marker; the trailing fill independently
// follows current-resource depletion with the native damped transition.
void GameplayHud::meter(const HudResourceMeter& observed, Gauge& gauge, float y,
                        render::Color color, float seconds) {
  const float current = observed.current / observed.maximum;
  const float recoverable = observed.recoverable / observed.maximum;
  if (!initialized_) gauge = {current, current, 0, false, false, 0, 0, 0};
  if (current < gauge.previous) gauge.draining = true;
  if (current > gauge.trail) gauge.trail = current;
  if (gauge.draining) {
    if (std::abs(gauge.trail - current) < kSettledTolerance) {
      gauge.draining = false;
    }
    const auto next = math::damped_response(
        math::DampedResponseState{gauge.trail, gauge.velocity},
        math::DampedResponseTarget{current, kTrailDuration}, seconds);
    gauge.trail = next.value;
    gauge.velocity = next.velocity;
  }
  if (!gauge.gaining && current > gauge.previous &&
      recoverable <= current + kGainTolerance) {
    gauge.gaining = true;
    gauge.gain_amount = current;
    gauge.gain_progress = gauge.gain_velocity = 0;
  }
  if (gauge.gaining) {
    if (1 - gauge.gain_progress < kSettledTolerance) gauge.gaining = false;
    const auto next = math::damped_response(
        math::DampedResponseState{gauge.gain_progress, gauge.gain_velocity},
        math::DampedResponseTarget{1.0F, kGainDuration}, seconds);
    gauge.gain_progress = next.value;
    gauge.gain_velocity = next.velocity;
  }
  gauge.previous = current;
  const auto fill = [&](float amount, render::Color tint) {
    if (amount <= 0) return;
    const float width = kMeterWidth * amount;
    append({kGaugeImage,
            {kMeterRight - width, y, width, kMeterHeight},
            {0, 0, 1, 1},
            tint});
  };
  fill(gauge.trail, kTrail);
  fill(current, color);
  if (gauge.gaining) {
    const float progress = std::clamp(gauge.gain_progress, 0.0F, 1.0F);
    const float width = kMeterWidth * gauge.gain_amount;
    const float scaled_width =
        width * math::linear_interpolate(1.0F, 1.4F, progress);
    const float scaled_height =
        kMeterHeight * math::linear_interpolate(1.0F, 1.8F, progress);
    auto flash = color;
    flash.alpha = packed_channel(1 - progress);
    append({kGaugeImage,
            {kMeterRight - width * kHalf - scaled_width * kHalf,
             y + kMeterHeight * kHalf - scaled_height * kHalf, scaled_width,
             scaled_height},
            {0, 0, 1, 1},
            flash,
            0,
            false,
            1,
            false,
            true});
  }
  constexpr float kMarkerX = 1170;
  const float marker_y = y == 60 ? 52 : 80;
  append(
      {kMarkerImage, {kMarkerX - recoverable * kMeterWidth, marker_y, 0, 0}});
}
// Weapon change owns a transient notice. Its two damped coordinates and
// arrival/bounce/hold/depart phases are separate from the persistent ammo row.
void GameplayHud::weapon_notice(const GameplayHudSnapshot& observed,
                                float seconds) {
  const auto weapon = observed.weapon;
  constexpr float kSlideTime = .13F, kBounceTime = .1F;
  constexpr float kHiddenX = 400, kBounceY = 12, kArrivalTolerance = 2;
  constexpr float kSettleTolerance = .01F, kHoldSeconds = 1;
  if (!initialized_ || weapon != weapon_) {
    weapon_ = weapon;
    notice_phase_ = NoticePhase::begin;
    notice_elapsed_ = 0;
  }

  if (seconds > 0 || !initialized_) {
    switch (notice_phase_) {
      case NoticePhase::begin:
        notice_visible_ = true;
        notice_x_ = kHiddenX;
        notice_y_ = 0;
        notice_x_target_ = notice_y_target_ = 0;
        notice_velocity_ = notice_y_velocity_ = 0;
        notice_phase_ = NoticePhase::arriving;
        break;
      case NoticePhase::arriving:
        if (std::abs(notice_x_) < kArrivalTolerance) {
          notice_y_target_ = kBounceY;
          notice_phase_ = NoticePhase::bounce;
        }
        break;
      case NoticePhase::bounce:
        if (std::abs(notice_y_ - notice_y_target_) < kSettleTolerance) {
          notice_phase_ = NoticePhase::hold;
          notice_elapsed_ = 0;
        }
        break;
      case NoticePhase::hold:
        if (notice_elapsed_ > kHoldSeconds) {
          notice_phase_ = NoticePhase::hold_end;
        }
        break;
      case NoticePhase::hold_end:
        notice_phase_ = NoticePhase::settle;
        break;
      case NoticePhase::settle:
        notice_y_target_ = 0;
        notice_phase_ = NoticePhase::settling;
        break;
      case NoticePhase::settling:
        if (std::abs(notice_y_) < kArrivalTolerance) {
          notice_x_target_ = kHiddenX;
          notice_phase_ = NoticePhase::departing;
        }
        break;
      case NoticePhase::departing:
        if (std::abs(notice_x_ - notice_x_target_) < kSettleTolerance) {
          notice_phase_ = NoticePhase::finish;
        }
        break;
      case NoticePhase::finish:
        notice_phase_ = NoticePhase::hide;
        break;
      case NoticePhase::hide:
        notice_visible_ = false;
        break;
    }
  }
  const auto next_x = math::damped_response(
      math::DampedResponseState{notice_x_, notice_velocity_},
      math::DampedResponseTarget{notice_x_target_, kSlideTime}, seconds);
  notice_x_ = next_x.value;
  notice_velocity_ = next_x.velocity;
  const auto next_y = math::damped_response(
      math::DampedResponseState{notice_y_, notice_y_velocity_},
      math::DampedResponseTarget{notice_y_target_, kBounceTime}, seconds);
  notice_y_ = next_y.value;
  notice_y_velocity_ = next_y.velocity;
  notice_elapsed_ += seconds;
  if (notice_visible_) {
    constexpr float kBackgroundX = 990, kBackgroundY = 168;
    constexpr float kWeaponX = 1007, kWeaponY = 189;
    append({kWeaponBackgroundImage,
            {kBackgroundX + notice_x_, kBackgroundY + notice_y_, 0, 0}});
    append({kFirstWeaponImage + weapon,
            {kWeaponX + notice_x_, kWeaponY + notice_y_, 0, 0}});
  }
}
void GameplayHud::ring(const SpellRingPresentation& observed) {
  if (!observed.visible || observed.opacity <= 0) return;
  constexpr std::array<std::array<unsigned, 4>, 3> kArt{
      {{120, 114, 102, 113}, {120, 118, 107, 112}, {120, 119, 106, 111}}};
  constexpr std::array<std::array<unsigned, 4>, 3> kNames{
      {{132, 129, 123, 128}, {132, 130, 125, 127}, {132, 131, 124, 126}}};
  constexpr std::array<render::Rect, 4> kArtPositions{
      {{445, 110, 0, 0}, {791, 165, 0, 0}, {445, 511, 0, 0}, {319, 165, 0, 0}}};
  constexpr std::array<render::Rect, 4> kNamePositions{
      {{563, 159, 0, 0}, {773, 271, 0, 0}, {537, 496, 0, 0}, {436, 263, 0, 0}}};
  const render::Color tint{1, 1, 1, packed_channel(observed.opacity)};
  append({kRingOuterImage,
          kOuterRingPosition,
          {0, 0, 1, 1},
          tint,
          observed.rotation_radians,
          false,
          observed.ring_scale});
  // Original draw retains the preceding ring scale for the second ring.
  append({kRingInnerImage,
          kInnerRingPosition,
          {0, 0, 1, 1},
          tint,
          -observed.rotation_radians,
          false,
          observed.ring_scale});
  for (std::size_t choice = 0; choice < observed.choices.size(); ++choice) {
    if (!observed.choices.at(choice).learned) continue;
    const float value = observed.choices.at(choice).permitted ? 1 : kHalf;
    append({kArt.at(observed.page - 1).at(choice),
            kArtPositions.at(choice),
            {0, 0, 1, 1},
            {value, value, value, packed_channel(observed.art_opacity)},
            observed.art_rotation,
            false,
            observed.art_scale,
            true});
  }
  append({kRingCenterImage,
          kRingCenterPosition,
          {0, 0, 1, 1},
          tint,
          0,
          false,
          observed.center_scale});
  constexpr std::array<float, 4> kFlameAngles{0, std::numbers::pi_v<float> / 2,
                                              std::numbers::pi_v<float>,
                                              -std::numbers::pi_v<float> / 2};
  for (std::size_t choice = 0; choice < observed.choices.size(); ++choice) {
    const auto availability = observed.choices.at(choice);
    const bool disabled = availability.learned && !availability.permitted;
    const float value = disabled ? kHalf : 1;
    append({availability.learned ? kFlameImage : kEmptyFlameImage,
            kFlamePosition,
            {0, 0, 1, 1},
            {value, value, value,
             packed_channel(observed.name_opacity * packed_channel(value))},
            kFlameAngles.at(choice),
            false,
            observed.flame_scale,
            true});
  }
  for (std::size_t choice = 0; choice < observed.choices.size(); ++choice) {
    if (!observed.choices.at(choice).learned) continue;
    const float value = observed.choices.at(choice).permitted ? 1 : kHalf;
    append({kNames.at(observed.page - 1).at(choice),
            kNamePositions.at(choice),
            {0, 0, 1, 1},
            {value, value, value, packed_channel(observed.name_opacity)},
            0,
            false,
            observed.flame_scale,
            true});
  }
  if (observed.selected_choice != 0 && observed.selection_progress > 0) {
    constexpr std::array<render::Color, 4> kChoiceColors{
        {{160 / 255.0F, 170 / 255.0F, 99 / 255.0F, 1},
         {165 / 255.0F, 31 / 255.0F, 59 / 255.0F, 1},
         {92 / 255.0F, 183 / 255.0F, 94 / 255.0F, 1},
         {50 / 255.0F, 82 / 255.0F, 192 / 255.0F, 1}}};
    const auto choice = static_cast<std::size_t>(observed.selected_choice - 1);
    auto color = kChoiceColors.at(choice);
    const float pulse = std::clamp(
        std::sin(observed.selection_progress * std::numbers::pi_v<float>), 0.0F,
        1.0F);
    // Original UI vertex shader adds its uniform light before the ps_2_0
    // color-input saturation. This quad's four vertex colors are identical.
    const float light = pulse * pulse * pulse * kHalf;
    color.red = std::min(1.0F, color.red + light);
    color.green = std::min(1.0F, color.green + light);
    color.blue = std::min(1.0F, color.blue + light);
    color.alpha = packed_channel(observed.name_opacity * pulse);
    append({kSelectionFlameImage,
            kFlamePosition,
            {0, 0, 1, 1},
            color,
            kFlameAngles.at(choice),
            false,
            observed.flame_scale,
            true,
            true});
  }
}
void GameplayHud::advance(const GameplayHudSnapshot& observed, float seconds,
                          SourceRandom& random) {
  validate(observed.health);
  validate(observed.magic);
  const auto& ring_state = observed.magic_ring;
  const std::array<float, 11> visual_values{
      ring_state.opacity,           ring_state.elapsed_seconds,
      ring_state.rotation_radians,  ring_state.center_scale,
      ring_state.ring_scale,        ring_state.name_opacity,
      ring_state.flame_scale,       ring_state.art_opacity,
      ring_state.art_rotation,      ring_state.art_scale,
      ring_state.selection_progress};
  if (!std::ranges::all_of(visual_values,
                           [](float value) { return std::isfinite(value); }) ||
      ring_state.selected_choice > 4 || ring_state.center_scale <= 0 ||
      ring_state.ring_scale <= 0 || ring_state.flame_scale <= 0 ||
      ring_state.art_scale <= 0 || ring_state.name_opacity < 0 ||
      ring_state.name_opacity > 1 || ring_state.art_opacity < 0 ||
      ring_state.art_opacity > 1 || ring_state.selection_progress < 0 ||
      ring_state.selection_progress > 1 ||
      static_cast<unsigned>(observed.target) >
          static_cast<unsigned>(HudTargetKind::hit)) {
    throw std::invalid_argument("Invalid HUD visual observation");
  }
  if (!std::isfinite(seconds) || seconds < 0 ||
      observed.weapon >= kAimedReticle.size() ||
      observed.ammunition > observed.ammunition_capacity ||
      observed.ammunition > ammunition_hud_maximum ||
      observed.magic_ring.page == 0 || observed.magic_ring.page > 3 ||
      !std::isfinite(observed.magic_ring.opacity) ||
      observed.magic_ring.opacity < 0 || observed.magic_ring.opacity > 1 ||
      !std::isfinite(observed.magic_ring.elapsed_seconds)) {
    throw std::invalid_argument("Invalid gameplay HUD observation");
  }
  count_ = 0;
  if (!observed.visible) return;
  if (observed.goal_observation) {
    goal_.advance(*observed.goal_observation, seconds);
    for (const auto& command : goal_.commands()) append(command);
  }
  damage_.advance(seconds);
  for (const auto& command : damage_.commands()) append(command);
  append({kBackgroundImage, kBackgroundPosition});
  auto health_color = kHealth;
  constexpr float kWarningThreshold = .2F, kWarningPeriod = 1.5F;
  const bool low_health =
      observed.health.current / observed.health.maximum <= kWarningThreshold;
  if (low_health && !warning_active_) {
    warning_active_ = true;
    warning_elapsed_ = 0;
  }
  if (warning_active_) {
    warning_strength_ = low_health
                            ? std::min(1.0F, warning_strength_ + 3 * seconds)
                            : std::max(0.0F, warning_strength_ - 3 * seconds);
    const float phase =
        (std::sin(warning_elapsed_ *
                  (2 * std::numbers::pi_v<float> / kWarningPeriod)) +
         1) *
        .5F;
    const float strength = phase * warning_strength_;
    const float blend = strength * strength;
    health_color.red = math::linear_interpolate(kHealth.red, 1.0F, blend);
    health_color.green =
        math::linear_interpolate(kHealth.green, kWarningColor.green, blend);
    health_color.blue =
        math::linear_interpolate(kHealth.blue, kWarningColor.blue, blend);
    warning_elapsed_ = std::fmod(warning_elapsed_ + seconds, kWarningPeriod);
    if (!low_health && warning_strength_ < kSettledTolerance) {
      warning_active_ = false;
    }
  }
  health_color.red = packed_channel(health_color.red);
  health_color.green = packed_channel(health_color.green);
  health_color.blue = packed_channel(health_color.blue);
  meter(observed.health, health_, kHealthY, health_color, seconds);
  meter(observed.magic, magic_, kMagicY, kMagic, seconds);
  weapon_notice(observed, seconds);
  ammunition_.advance({observed.weapon, observed.ammunition, observed.recharged,
                       observed.enchanted},
                      seconds, random);
  for (const auto& command : ammunition_.commands()) append(command);
  constexpr float kHitReticleSeconds = .35F;
  if (observed.target == HudTargetKind::hit) {
    reticle_hit_remaining_ = kHitReticleSeconds;
  }
  const auto target =
      reticle_hit_remaining_ > 0 ? HudTargetKind::hit : observed.target;
  reticle_hit_remaining_ = std::max(0.0F, reticle_hit_remaining_ - seconds);
  if (!observed.reticle_hidden) {
    constexpr std::array<render::Color, 3> kTargetColors{
        {{173 / 255.0F, 173 / 255.0F, 173 / 255.0F, 1},
         {0, 159 / 255.0F, 230 / 255.0F, 1},
         {200 / 255.0F, 0, 0, 1}}};
    const auto image =
        observed.magic_aim
            ? kMagicReticleImage
            : (observed.aiming ? kAimedReticle.at(observed.weapon)
                               : kDefaultReticleImage);
    append({image,
            kReticlePosition,
            {0, 0, 1, 1},
            kTargetColors.at(static_cast<std::size_t>(target)),
            0,
            true});
  }
  ring(observed.magic_ring);
  initialized_ = true;
}
std::span<const HudSprite> GameplayHud::commands() const noexcept {
  return std::span(commands_).first(count_);
}
}  // namespace bw::game
