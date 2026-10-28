// Decode original selected straight-gun settings without mutable runtime
// fixups. I.1 keeps profile admission explicit; CDSA.32 prepares fixed fields
// once. Both implementation corpora queried/read; header owns the full
// contract.
#include "weapon_definition.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <numbers>
#include <stdexcept>

#include "bw/content/formats/binary_reader.hpp"
namespace bw::game {
namespace {
using resources::detail::Reader;
constexpr std::size_t kDispatch = 0x54, kRange = 0x64, kFalloffStart = 0x68,
                      kFalloffEnd = 0x6c, kSpread = 0x78, kSpeed = 0x88,
                      kRate = 0x90, kCapacity = 0x94, kRays = 0x98,
                      kFarMultiplier = 0x30, kDischargeAttachment = 0x14,
                      kOriginAttachment = 0x10, kSpreadMaximum = 0x7c,
                      kSpreadWiden = 0x80, kSpreadRecover = 0x84;
constexpr std::size_t kFlightSoundClass = 0x128, kPassSoundClass = 0x12c;
float nonnegative(const Reader& reader, std::size_t offset) {
  const auto value = std::bit_cast<float>(reader.u32(offset));
  if (!std::isfinite(value) || value < 0) {
    resources::detail::malformed("Invalid ordinary weapon scalar");
  }
  return value;
}
std::int32_t identity(const Reader& reader, std::size_t offset) {
  return std::bit_cast<std::int32_t>(reader.u32(offset));
}
}  // namespace
StraightWeaponDefinition StraightWeaponDefinition::parse(
    const formats::CustomizationDefinition& tree, std::string_view name) {
  const auto section = [&](std::string_view child) {
    const std::array path{std::string_view("witch"), std::string_view("weapon"),
                          name, child};
    return tree.lookup(path);
  };
  const Reader spec{section("00_spec")};
  if (spec.u32(kDispatch) != 0 || spec.u32(kRays) != 1) {
    resources::detail::unsupported(
        "Weapon does not use ordinary straight shot");
  }
  // Both selected guns have no traveling child cue. Other flight cue classes
  // require that retained receiver rather than silently losing their sound.
  constexpr std::uint32_t kNoFlightSound = 2;
  if (spec.u32(kFlightSoundClass) != kNoFlightSound ||
      spec.u32(kPassSoundClass) > 1) {
    resources::detail::unsupported(
        "Unsupported ordinary projectile sound class");
  }
  StraightWeaponDefinition result;
  constexpr std::uint32_t kFirstNearPassCue = 0x127;
  result.near_pass_cue = kFirstNearPassCue + spec.u32(kPassSoundClass);
  result.range = nonnegative(spec, kRange);
  result.speed = nonnegative(spec, kSpeed);
  result.spread_degrees = nonnegative(spec, kSpread);
  constexpr float kDegrees = 180.0F;
  result.spread = {
      result.spread_degrees * std::numbers::pi_v<float> / kDegrees,
      nonnegative(spec, kSpreadMaximum) * std::numbers::pi_v<float> / kDegrees,
      nonnegative(spec, kSpreadWiden), nonnegative(spec, kSpreadRecover)};
  if (result.spread.maximum_radians < result.spread.minimum_radians ||
      result.spread.maximum_radians > std::numbers::pi_v<float> ||
      result.spread.widen_seconds <= 0 || result.spread.recover_seconds <= 0) {
    resources::detail::malformed("Invalid ordinary weapon spread bounds");
  }
  result.rate_per_second = nonnegative(spec, kRate);
  result.magazine_capacity = spec.u32(kCapacity);
  result.damage.falloff_start = nonnegative(spec, kFalloffStart);
  result.damage.falloff_end = nonnegative(spec, kFalloffEnd);
  constexpr unsigned kMaximumNativeAmmunition = 511;
  if (result.range <= 0 || result.speed <= 0 || result.rate_per_second <= 0 ||
      !std::isfinite(1 / result.rate_per_second) ||
      result.magazine_capacity == 0 ||
      result.magazine_capacity > kMaximumNativeAmmunition ||
      result.damage.falloff_end <= result.damage.falloff_start) {
    resources::detail::malformed("Invalid ordinary weapon bounds");
  }
  for (std::size_t level = 0; level < result.damage.base.size(); ++level) {
    result.damage.base.at(level) = nonnegative(spec, level * sizeof(float));
    result.damage.far_multiplier.at(level) =
        nonnegative(spec, kFarMultiplier + level * sizeof(float));
  }
  const Reader visual{section("10_visual")};
  result.effects.retained_flash = identity(visual, 0);
  result.effects.projectile = identity(visual, sizeof(std::int32_t));
  result.effects.muzzle = identity(visual, 2 * sizeof(std::int32_t));
  result.effects.discharge_attachment = identity(visual, kDischargeAttachment);
  result.effects.projectile_origin_attachment =
      identity(visual, kOriginAttachment);
  const Reader impacts{section("normalImpact")};
  for (std::size_t i = 0; i < result.effects.surface_impact.size(); ++i) {
    result.effects.surface_impact.at(i) =
        identity(impacts, i * sizeof(std::int32_t));
  }
  return result;
}
float ProjectileDamage::at_distance(unsigned level, float distance) const {
  if (level > base.size() || !std::isfinite(distance) || distance < 0) {
    throw std::invalid_argument("Invalid projectile damage observation");
  }
  const auto lane = level > 0 ? level - 1 : 0;
  const auto fraction = std::clamp(
      (distance - falloff_start) / (falloff_end - falloff_start), 0.0F, 1.0F);
  return (fraction * far_multiplier.at(lane) + (1 - fraction)) * base.at(lane);
}
}  // namespace bw::game
