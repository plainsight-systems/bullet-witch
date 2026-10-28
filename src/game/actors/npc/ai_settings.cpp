// Decode original search/aim records into fixed copied values. Core E.4
// (#re-design-invariants) rejects malformed/nonfinite candidates before return;
// MEM.9 (guidelines/memory/MEM.9-embedded-allocate-at-init.md) performs all
// tree lookup at preparation and uses allocation-free constant-size queries.
// Both implementation corpora reread; contracts and source mapping are in the
// header.
#include "ai_settings.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <numbers>
#include <stdexcept>

#include "bw/content/formats/binary_reader.hpp"
namespace bw::game {
namespace {
constexpr float kHalfTurnDegrees = 180.F;
float scalar(const resources::detail::Reader& reader, std::size_t offset) {
  const float value = std::bit_cast<float>(reader.u32(offset));
  if (!std::isfinite(value) || value < 0) {
    resources::detail::malformed("Invalid original AI setting");
  }
  return value;
}
}  // namespace
NpcMovementSettings npc_movement_settings(std::int32_t posture) noexcept {
  constexpr float kOrdinaryRadius = .1F, kStandingRadius = .3F;
  constexpr std::array<std::int32_t, 13> kPostures{0x10, 0,    0x90, 0x20, 0x30,
                                                   0x40, 0x50, 0x60, 0x70, 0x80,
                                                   0xa0, 0xb0, 0xc0};
  const auto index =
      posture >= 0 && posture < static_cast<std::int32_t>(kPostures.size())
          ? static_cast<std::size_t>(posture)
          : 0;
  constexpr std::size_t kStandingVariant = 10, kLastVariant = 12;
  return {std::span{kPostures}[index],
          index == 0 || index == kStandingVariant || index == kLastVariant
              ? kStandingRadius
              : kOrdinaryRadius};
}
NpcAiSettings NpcAiSettings::parse(
    const formats::CustomizationDefinition& tree) {
  NpcAiSettings result;
  const std::array search_path{std::string_view("witch"),
                               std::string_view("aiparam"),
                               std::string_view("searchTarget_")};
  const resources::detail::Reader search{tree.lookup(search_path)};
  result.target_change_delay_ = scalar(search, 0);
  result.lost_target_seconds_ = scalar(search, sizeof(float));
  result.nearby_radius_ = scalar(search, 2 * sizeof(float));
  constexpr std::array names{std::string_view("id0"), std::string_view("id1"),
                             std::string_view("id2"), std::string_view("id3"),
                             std::string_view("id4")};
  for (std::size_t index = 0; index < names.size(); ++index) {
    const std::array path{std::string_view("witch"),
                          std::string_view("aiparam"),
                          std::string_view("shoot"), names.at(index)};
    const auto bytes = tree.lookup(path);
    const resources::detail::Reader reader{bytes};
    reader.range(0, 2 * sizeof(float) + 1);
    const std::array movement_path{std::string_view("witch"),
                                   std::string_view("aiparam"),
                                   std::string_view("boid"), names.at(index)};
    const resources::detail::Reader movement{tree.lookup(movement_path)};
    constexpr std::size_t kRetreat = 0x0c, kIdle = 0x14;
    constexpr float kIdleMultiplier = 1.5F;
    result.navigation_.at(index) = {scalar(movement, kRetreat),
                                    scalar(movement, kIdle) * kIdleMultiplier};
    const auto finite_field = [&](std::size_t field) {
      const auto value =
          std::bit_cast<float>(movement.u32(field * sizeof(float)));
      if (!std::isfinite(value)) {
        resources::detail::malformed("Nonfinite steering setting");
      }
      return value;
    };
    constexpr std::size_t kAggregateWeight = 5, kCohesionWeight = 6,
                          kAlignmentWeight = 7, kSeparationWeight = 8,
                          kAvoidanceWeight = 9;
    result.steering_.at(index) = {finite_field(0),
                                  finite_field(1),
                                  finite_field(2),
                                  finite_field(3),
                                  finite_field(4),
                                  finite_field(kAggregateWeight),
                                  finite_field(kCohesionWeight),
                                  finite_field(kAlignmentWeight),
                                  finite_field(kSeparationWeight),
                                  finite_field(kAvoidanceWeight)};
    result.aiming_.at(index) = {
        (scalar(reader, 0) * std::numbers::pi_v<float>) / kHalfTurnDegrees,
        scalar(reader, sizeof(float)), bytes[2 * sizeof(float)] != 0};
  }
  return result;
}
NpcAimSettings NpcAiSettings::aim(std::int32_t mode,
                                  bool player_target) const noexcept {
  const auto selected =
      mode >= 0 && mode < static_cast<std::int32_t>(aiming_.size())
          ? static_cast<std::size_t>(mode)
          : 0;
  // NPC-target spread always uses the fifth record. Prediction is separately
  // read from the actor's mode, even when its spread came from the NPC record.
  auto result =
      std::span{aiming_}[player_target ? selected : aiming_.size() - 1];
  result.predict_target = std::span{aiming_}[selected].predict_target;
  return result;
}
NpcNavigationSettings NpcAiSettings::navigation(std::int32_t mode,
                                                bool enabled) const noexcept {
  if (!enabled) return {};  // Original BSS fallback record.
  const auto selected =
      mode >= 0 && mode < static_cast<std::int32_t>(navigation_.size())
          ? static_cast<std::size_t>(mode)
          : 0;
  return std::span{navigation_}[selected];
}
NpcSteeringSettings NpcAiSettings::steering(std::int32_t mode) const noexcept {
  const auto selected =
      mode >= 0 && mode < static_cast<std::int32_t>(steering_.size())
          ? static_cast<std::size_t>(mode)
          : 0;
  return std::span{steering_}[selected];
}
PerceptionSettings NpcAiSettings::perception(
    const NpcActorAiSettings& actor) const {
  if (!std::isfinite(actor.view_degrees) ||
      !std::isfinite(actor.close_radius) || !std::isfinite(actor.far_radius) ||
      actor.close_radius < 0 || actor.far_radius < 0) {
    throw std::invalid_argument("Invalid actor perception settings");
  }
  // Source clamps only the angular field; radii retain authored values.
  return {(std::clamp(actor.view_degrees, 0.F, kHalfTurnDegrees) *
           std::numbers::pi_v<float>) /
              kHalfTurnDegrees,
          actor.close_radius,
          actor.far_radius,
          target_change_delay_,
          lost_target_seconds_,
          nearby_radius_};
}
}  // namespace bw::game
