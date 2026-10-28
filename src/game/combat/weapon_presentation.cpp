// Recovered accepted-shot presentation and attachment updates. E.4
// (#re-design-invariants) validates copied descriptors/transforms before live
// publication; MEM.9 (guidelines/memory/MEM.9-embedded-allocate-at-init.md)
// retains fixed weapon state and prepared effect/light pools. Both corpora
// reread for implementation; source/reference detail is in
// first-combat-effects.
#include "weapon_presentation.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <stdexcept>

#include "bw/content/formats/binary_reader.hpp"
namespace bw::game {
namespace {
void validate_color(const math::Vector3<float>& color) {
  if (!math::all_finite(color) ||
      std::ranges::any_of(color, [](float v) { return v < 0; })) {
    throw std::invalid_argument("Invalid weapon light color");
  }
}
void validate_transform(const math::Affine3<float>& transform) {
  if (!std::ranges::all_of(
          transform, [](const auto& row) { return math::all_finite(row); })) {
    throw std::invalid_argument("Invalid weapon muzzle transform");
  }
}
void validate_definition(const WeaponPresentationDefinition& value) {
  validate_color(value.diffuse);
  validate_color(value.specular);
  validate_color(value.attenuation);
  if (!std::isfinite(value.duration) ||
      (value.attenuation[1] == 0 && value.attenuation[2] == 0) ||
      (value.duration > 0 && !std::isfinite(1.F / value.duration))) {
    throw std::invalid_argument("Invalid weapon muzzle light descriptor");
  }
}
}  // namespace
WeaponPresentationDefinition WeaponPresentationDefinition::parse(
    const formats::CustomizationDefinition& tree, std::string_view name) {
  const auto section = [&](std::string_view child) {
    const std::array path{std::string_view("witch"), std::string_view("weapon"),
                          name, child};
    return tree.lookup(path);
  };
  const resources::detail::Reader visual{section("10_visual")};
  const resources::detail::Reader light{section("20_muzzleLight")};
  WeaponPresentationDefinition result;
  constexpr std::size_t kAlternateFlash = 24, kDuration = 40,
                        kAttenuationLane = 6;
  constexpr std::size_t kDischargeEffect = 8;
  result.discharge_effect =
      std::bit_cast<std::int32_t>(visual.u32(kDischargeEffect));
  result.retained_flash = {
      std::bit_cast<std::int32_t>(visual.u32(0)),
      std::bit_cast<std::int32_t>(visual.u32(kAlternateFlash))};
  for (std::size_t lane = 0; lane < 3; ++lane) {
    result.diffuse.at(lane) =
        std::bit_cast<float>(light.u32(lane * sizeof(float)));
    result.specular.at(lane) =
        std::bit_cast<float>(light.u32((lane + 3) * sizeof(float)));
    result.attenuation.at(lane) = std::bit_cast<float>(
        light.u32((lane + kAttenuationLane) * sizeof(float)));
  }
  result.duration = std::bit_cast<float>(light.u32(kDuration));
  validate_definition(result);
  return result;
}
WeaponPresentation::WeaponPresentation(const world::WorldClock& clock,
                                       WeaponPresentationDefinition definition,
                                       CombatEffectEmitter effects,
                                       scene::LocalLightEmitter&& light)
    : clock_(clock),
      definition_(definition),
      effects_(effects),
      light_(std::move(light)) {
  validate_definition(definition_);
  if (!light_.valid()) {
    throw std::invalid_argument("Weapon requires live light registration");
  }
}
WeaponPresentation::~WeaponPresentation() { retire(); }
// Detached discharge instances keep the authored placement captured at each
// projectile, independently of the retained barrel child's subsequent motion.
// Both-phase I.4/MEM.9 keep the copied frame and prepared pool boundary
// explicit.
void WeaponPresentation::discharge(const math::Affine3<float>& transform) {
  validate_transform(transform);
  if (definition_.discharge_effect >= 0) {
    (void)effects_.spawn(
        static_cast<std::uint32_t>(definition_.discharge_effect),
        effects::SpritePlacement{.transform = transform});
  }
}
void WeaponPresentation::accepted_shot(const math::Affine3<float>& transform,
                                       const math::Vector3<float>& color,
                                       bool primary) {
  validate_transform(transform);
  validate_color(color);
  scene::LocalLight candidate{transform[3], {}, {}, definition_.attenuation};
  for (std::size_t lane = 0; lane < 3; ++lane) {
    candidate.diffuse.at(lane) = definition_.diffuse.at(lane) * color.at(lane);
    candidate.specular.at(lane) =
        definition_.specular.at(lane) * color.at(lane);
  }
  validate_color(candidate.diffuse);
  validate_color(candidate.specular);
  // Original replaces the retained child before constructing the next one.
  retire();
  placement_.transform = transform;
  light_value_ = candidate;
  const auto effect = definition_.retained_flash.at(primary ? 0U : 1U);
  if (effect >= 0) {
    flash_ = effects_.spawn(static_cast<std::uint32_t>(effect), placement_);
  }
  constexpr std::uint32_t kInitialDecayingIntensity = 0x3f7ffffe;
  intensity_ = definition_.duration > 0
                   ? std::bit_cast<float>(kInitialDecayingIntensity)
                   : 1.F;
  decay_ = definition_.duration > 0 ? 1.F / definition_.duration : 0.F;
  light_active_ = true;
  publish_light();
}
void WeaponPresentation::move(const math::Affine3<float>& transform) {
  validate_transform(transform);
  placement_.transform = transform;
  if (flash_ && !effects_.move(*flash_, placement_)) {
    flash_.reset();
  }
  light_value_.position = transform[3];
  if (light_active_) {
    publish_light();
  }
}
void WeaponPresentation::publish_light() {
  auto value = light_value_;
  value.diffuse = math::scale(value.diffuse, intensity_);
  // Original consumer fades diffuse only; captured specular remains unchanged.
  light_.update(value);
}
void WeaponPresentation::advance(const world::WorldFrame& frame) {
  if (!frame.belongs_to(clock_) || frame.sequence() <= sequence_) {
    throw std::invalid_argument("Invalid weapon presentation frame");
  }
  sequence_ = frame.sequence();
  if (!frame.admission().simulation) {
    return;
  }
  if (flash_ && !effects_.alive(*flash_)) {
    flash_.reset();
  }
  if (light_active_ && intensity_ < 1.F) {
    intensity_ -= decay_ * frame.delta_seconds();
    if (intensity_ <= 0) {
      intensity_ = 0;
      light_active_ = false;
      light_.clear();
    } else {
      publish_light();
    }
  }
}
void WeaponPresentation::retire() {
  if (flash_) {
    effects_.stop(*flash_);
    flash_.reset();
  }
  if (light_active_) {
    light_.clear();
    light_active_ = false;
  }
  intensity_ = 0;
}
}  // namespace bw::game
