// Original cartridge banks, ejection and weapon transition presentation.
// All numeric policy below is authored table/receiver evidence, not gameplay
// resource authority. Core E.4 validates observations before mutation; MEM.9
// keeps both children and their publication buffer bounded and allocation-free.
#include "ammunition_hud.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>

#include "bw/math/scalar.hpp"
#include "bw/math/vector.hpp"

namespace bw::game {
namespace {
struct Layout {
  unsigned ejection{}, image{}, row{};
  float x{}, y{}, spacing{}, burst_x{}, burst_y{};
  render::Color enchant;
};
constexpr std::array<Layout, 4> kLayouts{{
    {0,
     429,
     20,
     1135,
     114,
     9,
     1169,
     136,
     {224 / 255.0F, 102 / 255.0F, 91 / 255.0F, 1}},
    {1,
     430,
     8,
     1131,
     110,
     18,
     1169,
     136,
     {154 / 255.0F, 223 / 255.0F, 167 / 255.0F, 1}},
    {0,
     427,
     12,
     1133,
     112,
     16,
     1169,
     136,
     {230 / 255.0F, 218 / 255.0F, 114 / 255.0F, 1}},
    {2,
     428,
     20,
     1123,
     115,
     9,
     1160,
     136,
     {157 / 255.0F, 140 / 255.0F, 251 / 255.0F, 1}},
}};
struct Ejection {
  float fade_begin{}, fade_end{}, x_bias{}, x_random{}, y_random{};
  float speed{}, rotation{}, rotation_random{};
};
constexpr std::array<Ejection, 3> kEjection{{
    {.2F, .7F, 0, .8F, .6F, 15, -4.5F, 4},
    {.4F, .7F, 0, .5F, .4F, 15, -8, 4.8F},
    {0, .1F, .1F, .1F, .1F, 17, -8.5F, 4},
}};
constexpr float kLastCartridgeDrop = 5, kSourceFramesPerSecond = 60;
constexpr float kGravity = 52, kArrivalSeconds = .15F, kDepartureSeconds = .25F;
constexpr float kFlashSeconds = 1.5F, kTintSeconds = .22F;
constexpr float kDigitWidth = 32, kDigitHeight = 42, kDigitAdvance = 15;
constexpr float kDigitRight = 1175, kDigitY = 115, kDigitAtlasWidth = 320;
constexpr float kPackedColorMaximum = 255;
constexpr unsigned kDigitImage = 425, kDigitFlashImage = 426, kBurstImage = 434;
constexpr unsigned kDecimalBase = 10;
constexpr float kBurstInitialScale = .4F;
constexpr render::Color kReserveColor{.5F, .5F, .5F, .7F};
unsigned row_count(unsigned ammunition, unsigned capacity) {
  const auto remainder = ammunition % capacity;
  return remainder == 0 && ammunition >= capacity ? capacity : remainder;
}
float progress(float value, float duration) {
  return std::clamp(value / duration, 0.0F, 1.0F);
}
float packed_alpha(float value) {
  return std::floor(std::clamp(value, 0.0F, 1.0F) * kPackedColorMaximum) /
         kPackedColorMaximum;
}
}  // namespace

void AmmunitionHud::reset() noexcept {
  children_ = {};
  initialized_ = false;
  selected_ = 0;
  count_ = 0;
  // Source damping is a function-static initialized at first flying update;
  // resetting UI children does not reseed it or the shared random stream.
}
std::span<const HudSprite> AmmunitionHud::commands() const noexcept {
  return std::span(commands_).first(count_);
}
void AmmunitionHud::append(HudSprite sprite) {
  commands_.at(count_++) = sprite;
}
void AmmunitionHud::initialize(Child& child, AmmunitionObservation observed) {
  const auto weapon = observed.weapon;
  const auto ammunition = observed.ammunition;
  child = {};
  child.active = true;
  child.weapon = weapon;
  child.ammunition = ammunition;
  const auto& layout = kLayouts.at(weapon);
  child.row = row_count(ammunition, layout.row);
  auto& bank = child.banks.front();
  for (unsigned i = 0; i < layout.row; ++i) {
    bank.at(i).x = layout.x - static_cast<float>(i) * layout.spacing;
    bank.at(i).y = layout.y;
    bank.at(i).active = i < child.row;
  }
  if (child.row != 0) bank.at(child.row - 1).y += kLastCartridgeDrop;
}
void AmmunitionHud::eject(Cartridge& cartridge, unsigned weapon,
                          SourceRandom& random) {
  if (cartridge.ejected) return;
  const auto& policy = kEjection.at(kLayouts.at(weapon).ejection);
  const math::Vector3<float> direction{
      1 + policy.x_bias + random.unit() * policy.x_random - policy.x_random,
      -1 + random.unit() * policy.y_random - policy.y_random, 0};
  const auto normalized = math::divide(direction, math::length(direction));
  cartridge.direction_x = normalized[0];
  cartridge.direction_y = normalized[1];
  cartridge.speed = policy.speed;
  cartridge.rotation_speed = policy.rotation +
                             random.unit() * policy.rotation_random -
                             policy.rotation_random;
  cartridge.rotation = cartridge.gravity = 0;
  cartridge.ejected = true;
}
void AmmunitionHud::set_count(Child& child, AmmunitionObservation observed,
                              SourceRandom& random) {
  const auto& layout = kLayouts.at(child.weapon);
  const auto previous = child.row;
  child.row = row_count(observed.ammunition, layout.row);
  child.ammunition = observed.ammunition;
  child.enchanted = observed.enchanted;
  if (!observed.recharged && child.row <= previous) {
    if (child.row == previous) return;
    for (unsigned i = child.row; i < previous; ++i) {
      eject(child.banks.at(child.bank).at(i), child.weapon, random);
    }
  } else {
    for (unsigned i = 0; i < previous; ++i) {
      eject(child.banks.at(child.bank).at(i), child.weapon, random);
    }
    child.bank = (child.bank + 1) % child.banks.size();
    auto& next = child.banks.at(child.bank);
    for (unsigned i = 0; i < child.row; ++i) {
      next.at(i) = {};
      next.at(i).active = true;
      next.at(i).x = layout.x - static_cast<float>(i) * layout.spacing;
      next.at(i).y = layout.y;
    }
  }
  if (child.row != 0) {
    child.banks.at(child.bank).at(child.row - 1).y += kLastCartridgeDrop;
  }
}
void AmmunitionHud::update(Child& child, float seconds) {
  if (!child.active) return;
  switch (child.phase) {
    case ChildPhase::begin:
      child.phase = ChildPhase::visible;
      break;
    case ChildPhase::visible:
      if (child.depart_requested) {
        child.phase = ChildPhase::depart;
        child.elapsed = 0;
      }
      break;
    case ChildPhase::depart:
      child.phase = ChildPhase::departing;
      child.elapsed = 0;
      break;
    case ChildPhase::departing:
      if (child.elapsed >= 1) child.phase = ChildPhase::finish;
      break;
    case ChildPhase::finish:
      child.active = false;
      break;
  }
  const auto& ejection = kEjection.at(kLayouts.at(child.weapon).ejection);
  for (auto& bank : child.banks) {
    for (unsigned i = 0; i < kLayouts.at(child.weapon).row; ++i) {
      auto& cartridge = bank.at(i);
      if (cartridge.phase == CartridgePhase::idle && cartridge.ejected) {
        cartridge.phase = CartridgePhase::ejecting;
        cartridge.elapsed = 0;
      } else if (cartridge.phase == CartridgePhase::ejecting) {
        cartridge.alpha = 1 - progress(cartridge.elapsed - ejection.fade_begin,
                                       ejection.fade_end - ejection.fade_begin);
        const float step = seconds * kSourceFramesPerSecond;
        cartridge.x += cartridge.direction_x * cartridge.speed * step;
        cartridge.y += cartridge.direction_y * cartridge.speed * step;
        if (!damping_initialized_) {
          constexpr double kSourceDamping = 0.9800000190734863;
          damping_ = static_cast<float>(std::pow(kSourceDamping, step));
          damping_initialized_ = true;
        }
        cartridge.speed *= damping_;
        cartridge.gravity += kGravity * seconds;
        cartridge.y += cartridge.gravity * step;
        cartridge.rotation += cartridge.rotation_speed * seconds;
        if (cartridge.elapsed > ejection.fade_end) {
          cartridge.phase = CartridgePhase::retiring;
          cartridge.elapsed = 0;
        }
      } else if (cartridge.phase == CartridgePhase::retiring) {
        cartridge.active = false;
        cartridge.phase = CartridgePhase::hidden;
        cartridge.elapsed = 0;
      }
      cartridge.elapsed += seconds;
    }
  }
  // The enchant indication is an explicit source observation, not a guess
  // based on selected spell or ammunition. Its one-shot burst is independent.
  const auto start_flash = [&] {
    child.flash_elapsed = child.flash_value = 0;
    child.flash_finished = false;
    if (!child.flash) {
      child.burst = true;
      child.burst_alpha = 1;
      child.burst_scale = kBurstInitialScale;
    }
    child.flash = true;
  };
  if (!child.flash) {
    if (child.enchanted) start_flash();
  } else {
    if (child.flash_finished) {
      if (child.enchanted) {
        start_flash();
      } else {
        child.flash = false;
      }
    }
    if (!child.flash_finished) {
      child.flash_value =
          std::sin(progress(child.flash_elapsed, kFlashSeconds) *
                   std::numbers::pi_v<float>);
      child.flash_elapsed += seconds;
      if (child.flash_elapsed >= kFlashSeconds) {
        child.flash_elapsed = child.flash_value = 0;
        child.flash_finished = true;
      }
    }
  }
  if (child.burst) {
    constexpr float kAlphaSpeed = 1.3F, kScaleTarget = 1.6F, kScaleRate = 1.4F;
    constexpr float kSettled = .01F;
    if (std::abs(child.burst_alpha) < kSettled &&
        std::abs(kScaleTarget - child.burst_scale) < kSettled) {
      child.burst = false;
    }
    child.burst_alpha =
        std::max(0.0F, child.burst_alpha - kAlphaSpeed * seconds);
    child.burst_scale = math::linear_interpolate(
        child.burst_scale, kScaleTarget, std::min(1.0F, kScaleRate * seconds));
  }
  const auto tint = math::damped_response(
      math::DampedResponseState<float>{child.tint, child.tint_velocity},
      math::DampedResponseTarget<float>{child.flash ? 1.0F : 0.0F,
                                        kTintSeconds},
      seconds);
  child.tint = tint.value;
  child.tint_velocity = tint.velocity;
  child.arrival = std::min(1.0F, child.arrival + seconds);
  if (child.phase == ChildPhase::departing ||
      child.phase == ChildPhase::finish) {
    child.departure = std::min(1.0F, child.departure + seconds);
  }
  child.elapsed += seconds;
}
void AmmunitionHud::draw(const Child& child) {
  if (!child.active) return;
  const auto& layout = kLayouts.at(child.weapon);
  constexpr float kArrivalOffset = 15, kDepartureOffset = -20,
                  kArrivalShape = 7;
  const float arrival = progress(child.arrival, kArrivalSeconds);
  const float departure = progress(child.departure, kDepartureSeconds);
  const float offset =
      math::eased_tangent_interpolate<float>(
          {kArrivalOffset, 0.0F, 0.0F, kArrivalShape}, arrival) +
      math::eased_tangent_interpolate<float>(
          {0.0F, kDepartureOffset, 0.0F, 0.0F}, departure);
  const float alpha = arrival * (1 - departure);
  for (const auto& bank : child.banks) {
    for (unsigned i = 0; i < layout.row; ++i) {
      const auto& cartridge = bank.at(i);
      if (!cartridge.active) continue;
      HudSprite sprite{layout.image, {cartridge.x + offset, cartridge.y, 0, 0}};
      sprite.color.alpha = packed_alpha(packed_alpha(cartridge.alpha) * alpha);
      sprite.rotation_radians = cartridge.rotation;
      sprite.top_left_pivot = true;
      append(sprite);
    }
  }
  if (child.ammunition > layout.row) {
    constexpr float kReserveX = 7, kReserveY = -5;
    for (unsigned i = 0; i < layout.row; ++i) {
      append({layout.image,
              {layout.x + offset + kReserveX -
                   static_cast<float>(i) * layout.spacing,
               layout.y + kReserveY, 0, 0},
              {0, 0, 1, 1},
              {kReserveColor.red, kReserveColor.green, kReserveColor.blue,
               packed_alpha(kReserveColor.alpha * alpha)}});
    }
  }
  const auto digits = [&](unsigned image, render::Color color) {
    unsigned value = child.ammunition, position = 0;
    for (;;) {
      const auto digit = value % kDecimalBase;
      append({image,
              {kDigitRight - static_cast<float>(position + 1) * kDigitAdvance,
               kDigitY, kDigitWidth, kDigitHeight},
              {static_cast<float>(digit) * kDigitWidth / kDigitAtlasWidth, 0,
               kDigitWidth / kDigitAtlasWidth, 1},
              color});
      ++position;
      value /= kDecimalBase;
      if (value == 0) break;
    }
  };
  const float number_alpha = child.departure > 0 ? 0.0F : 1.0F;
  render::Color number{
      math::linear_interpolate(1.0F, layout.enchant.red, child.tint),
      math::linear_interpolate(1.0F, layout.enchant.green, child.tint),
      math::linear_interpolate(1.0F, layout.enchant.blue, child.tint),
      number_alpha};
  digits(kDigitImage, number);
  if (child.flash) {
    auto flash_color = layout.enchant;
    flash_color.alpha *= child.flash_value * child.flash_value * number_alpha;
    digits(kDigitFlashImage, flash_color);
  }
  if (child.burst) {
    HudSprite burst{kBurstImage, {layout.burst_x, layout.burst_y, 0, 0}};
    burst.color = layout.enchant;
    burst.color.alpha *= child.burst_alpha * number_alpha;
    burst.centered = true;
    burst.scale = child.burst_scale;
    append(burst);
  }
}
void AmmunitionHud::advance(AmmunitionObservation observed, float seconds,
                            SourceRandom& random) {
  if (observed.weapon >= kLayouts.size() ||
      observed.ammunition > ammunition_hud_maximum || !std::isfinite(seconds) ||
      seconds < 0 || !std::isfinite(seconds * kSourceFramesPerSecond)) {
    throw std::invalid_argument("Invalid ammunition HUD observation");
  }
  if (!initialized_) {
    initialize(children_.at(selected_), observed);
    initialized_ = true;
  } else if (children_.at(selected_).weapon != observed.weapon) {
    children_.at(selected_).depart_requested = true;
    selected_ ^= 1U;
    initialize(children_.at(selected_), observed);
  }
  set_count(children_.at(selected_), observed, random);
  // The native owner updates and draws the outgoing child before the current.
  update(children_.at(selected_ ^ 1U), seconds);
  update(children_.at(selected_), seconds);
  count_ = 0;
  draw(children_.at(selected_ ^ 1U));
  draw(children_.at(selected_));
}
}  // namespace bw::game
