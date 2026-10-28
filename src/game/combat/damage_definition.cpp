// Copy the source interleaved region policy into its hot hit consumer layout.
// Core I.11/CDSA.32 were retrieved again for this preparation phase; actors
// retain their own health and random stream and borrow only immutable values.
#include "damage_definition.hpp"

#include <bit>
#include <cmath>
#include <stdexcept>

#include "bw/content/formats/binary_reader.hpp"
namespace bw::game {
ActorDamageDefinition ActorDamageDefinition::parse(
    const formats::CustomizationDefinition& tree, std::string_view actor) {
  const std::array path{std::string_view("witch"), std::string_view("actor"),
                        actor, std::string_view("body")};
  const resources::detail::Reader reader{tree.lookup(path)};
  const auto number = [&](std::size_t offset) {
    const auto value = std::bit_cast<float>(reader.u32(offset));
    if (!std::isfinite(value) || value < 0) {
      resources::detail::malformed("Invalid source damage policy scalar");
    }
    return value;
  };
  constexpr std::size_t kMeleeHeavy = 0x6c;
  constexpr std::size_t kMeleeDeathFirst = 0x148;
  constexpr std::size_t kMeleeDeathSecond = 0x14c;
  constexpr std::size_t kLowHealth = 0x58;
  constexpr std::size_t kHeavyDamage = 0x60;
  constexpr std::size_t kPhysicalDamage = 0x64;
  constexpr std::size_t kSustainedHealth = 0x150;
  constexpr std::size_t kSustainedProbability = 0x154;
  constexpr std::size_t kSustainedSeconds = 0x158;
  constexpr std::size_t kDeathNonpositive = 0x124;
  constexpr std::size_t kDeathPositive = 0x13c;
  ActorDamageDefinition result;
  for (std::size_t i = 0; i < result.ordinary.size(); ++i) {
    constexpr std::size_t kFirstRegion = 0x74, kRegionStride = 8;
    const auto offset = kFirstRegion + i * kRegionStride;
    result.ordinary.at(i) = {number(offset), number(offset + sizeof(float))};
    // The original compares its unit draw directly with this threshold.
    // Purchased PW0 region eight contains 21: it means unconditional success.
  }
  result.melee_heavy_threshold = number(kMeleeHeavy);
  result.melee_death_weights = {number(kMeleeDeathFirst),
                                number(kMeleeDeathSecond)};
  result.low_health_ratio = number(kLowHealth);
  result.heavy_damage_threshold = number(kHeavyDamage);
  result.physical_damage_threshold = number(kPhysicalDamage);
  result.sustained_health_ratio = number(kSustainedHealth);
  result.sustained_probability = number(kSustainedProbability);
  result.sustained_recovery_seconds = number(kSustainedSeconds);
  if (result.low_health_ratio > 1 || result.sustained_health_ratio > 1 ||
      result.sustained_probability > 1) {
    resources::detail::malformed("Invalid source damage policy ratio");
  }
  for (std::size_t i = 0; i < result.nonpositive_death_weights.size(); ++i) {
    result.nonpositive_death_weights.at(i) =
        number(kDeathNonpositive + i * sizeof(float));
  }
  for (std::size_t i = 0; i < result.positive_death_weights.size(); ++i) {
    result.positive_death_weights.at(i) =
        number(kDeathPositive + i * sizeof(float));
  }
  return result;
}
float ActorDamageDefinition::magnitude(std::uint8_t body,
                                       float incoming) const {
  if (!std::isfinite(incoming) || incoming < 0 || body >= ordinary.size()) {
    throw std::invalid_argument("Invalid ordinary hit region or magnitude");
  }
  const auto result = incoming * ordinary.at(body).damage_multiplier;
  if (!std::isfinite(result)) {
    throw std::overflow_error("Ordinary hit magnitude overflow");
  }
  return result;
}
}  // namespace bw::game
