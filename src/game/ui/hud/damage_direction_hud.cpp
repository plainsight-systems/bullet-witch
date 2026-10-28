// Source damage indicator task and original art layout. Four independent
// channels preserve native task entry/exit phases and reject visible
// retriggers. Commands follow the original x86 draw witness, including the
// quick edge flash and two outward-moving bars. I.1 and MEM.9 retain copied
// policy and bounded inline state; generic easing/vector arithmetic remains in
// bw::math.
#include "damage_direction_hud.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>

#include "bw/math/scalar.hpp"
namespace bw::game {
namespace {
struct Bar {
  float x{}, y{}, rotation{}, travel{}, pivot_y{};
};
struct Layout {
  float origin_x{}, origin_y{}, distance{};
  float flash_x{}, flash_y{}, flash_rotation{}, flash_scale_x{},
      flash_scale_y{};
  float flash_direction_x{}, flash_direction_y{};
  std::array<Bar, 2> bars;
};
constexpr std::array<Layout, 4> kLayouts{
    {{625,
      33,
      500,
      640,
      0,
      180,
      4,
      4,
      0,
      1,
      {{{-58, 16, -90, -90, 0}, {70, 14, 90, 90, 20}}}},
     {1179,
      345,
      300,
      1280,
      360,
      270,
      5,
      8,
      -1,
      0,
      {{{13, -34, 0, 180, 0}, {17, 66, 180, 0, 100}}}},
     {625,
      657,
      500,
      640,
      720,
      0,
      4,
      4,
      0,
      -1,
      {{{-58, 16, -90, -90, 0}, {70, 14, 90, 90, 20}}}},
     {71,
      345,
      300,
      0,
      360,
      90,
      5,
      8,
      1,
      0,
      {{{13, -34, 0, 180, 0}, {17, 66, 180, 0, 100}}}}}};
constexpr unsigned kFlashImage = 23, kBarImage = 22;
constexpr float kDegrees = 180, kQuarterTurn = 90, kFlashHold = .025F,
                kFlashFade = .183F, kFlashEnd = .208F, kOpacityEnd = .48F,
                kStretchEnd = .36F, kTravelEnd = .6F, kStretch = 5,
                kFlashHalfPixels = 20, kColorMaximum = 255;
float radians(float degrees) {
  return degrees * std::numbers::pi_v<float> / kDegrees;
}
float packed(float value) {
  return std::floor(std::clamp(value, 0.F, 1.F) * kColorMaximum) /
         kColorMaximum;
}
}  // namespace
bool DamageDirectionHud::request(DamageDirection direction) {
  const auto index = static_cast<std::size_t>(direction);
  if (index >= channels_.size()) {
    throw std::invalid_argument("Invalid damage feedback channel");
  }
  auto& channel = channels_.at(index);
  if (channel.visible) return false;
  channel.requested = true;
  return true;
}
void DamageDirectionHud::reset() noexcept { *this = DamageDirectionHud{}; }
std::span<const HudSprite> DamageDirectionHud::commands() const noexcept {
  return std::span(commands_).first(count_);
}
void DamageDirectionHud::advance(float seconds) {
  if (!std::isfinite(seconds) || seconds < 0) {
    throw std::invalid_argument("Invalid damage HUD interval");
  }
  count_ = 0;
  for (std::size_t i = 0; i < channels_.size(); ++i) {
    auto& c = channels_.at(i);
    switch (c.phase) {
      case Phase::idle:
        break;
      case Phase::requested:
        c.requested = false;
        c.phase = Phase::begin;
        break;
      case Phase::begin:
        c.visible = true;
        c.elapsed = 0;
        c.progress = 0;
        c.phase = Phase::live;
        break;
      case Phase::live:
        c.progress = std::clamp(c.elapsed, 0.F, 1.F);
        if (c.elapsed > 1) c.phase = Phase::finish;
        break;
      case Phase::finish:
        c.phase = Phase::retire;
        break;
      case Phase::retire:
        c.visible = false;
        c.phase = Phase::idle;
        break;
    }
    if (c.requested) c.phase = Phase::requested;
    c.elapsed += seconds;
    if (c.visible) draw(i, c.progress);
  }
}
void DamageDirectionHud::draw(std::size_t direction, float progress) {
  const auto& layout = kLayouts.at(direction);
  const float sine =
      std::sin((progress * kDegrees) * std::numbers::pi_v<float> / kDegrees);
  const float opacity =
      sine * std::cos(radians(
                 kQuarterTurn *
                 (1 - (kOpacityEnd - std::clamp(progress, 0.F, kOpacityEnd)) /
                          kOpacityEnd)));
  const float stretch =
      sine *
      std::cos(
          radians(kQuarterTurn *
                  (1 - (kStretchEnd - std::clamp(progress, 0.F, kStretchEnd)) /
                           kStretchEnd))) *
      (1 - (1 - progress) * (1 - progress));
  const float travel =
      layout.distance *
      math::quintic_ease(1 -
                         (kTravelEnd - std::clamp(progress, 0.F, kTravelEnd)) /
                             kTravelEnd);
  const float flash =
      progress <= kFlashHold
          ? 1
          : 1 - (std::clamp(progress, kFlashHold, kFlashEnd) - kFlashHold) /
                    kFlashFade;
  const std::array<float, 2> flash_center{layout.flash_x, layout.flash_y};
  commands_.at(count_++) = {
      .image = kFlashImage,
      .destination = {layout.flash_x, layout.flash_y},
      .color = {1, 1, 1, packed(flash)},
      .rotation_radians = radians(layout.flash_rotation),
      .centered = true,
      .transform = HudSpriteTransform{
          layout.flash_scale_x,
          layout.flash_scale_y,
          flash_center,
          flash_center,
          {layout.flash_scale_x * kFlashHalfPixels * layout.flash_direction_x,
           layout.flash_scale_y * kFlashHalfPixels *
               layout.flash_direction_y}}};
  for (const auto& bar : layout.bars) {
    const float x = std::sin(radians(bar.travel)) * travel;
    const float y = std::cos(radians(bar.travel)) * travel;
    commands_.at(count_++) = {
        .image = kBarImage,
        .destination = {layout.origin_x + bar.x + x,
                        layout.origin_y + bar.y + y},
        .color = {1, 1, 1, packed(opacity)},
        .rotation_radians = radians(bar.rotation),
        .centered = true,
        .transform = HudSpriteTransform{
            1,
            1 + kStretch * stretch,
            std::array<float, 2>{layout.origin_x + x,
                                 layout.origin_y + bar.pivot_y + y},
            {},
            {}}};
  }
}
}  // namespace bw::game
