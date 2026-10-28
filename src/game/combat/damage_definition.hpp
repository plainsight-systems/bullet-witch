// Immutable original ordinary-hit policy, copied once from an actor's body
// customization. Hit region selects damage and reaction threshold separately;
// receiver state and source RNG decide the eventual action. Definitions own no
// health, object identity, action, physics body or clock. Missing/invalid
// source values reject at preparation. O(body regions), fixed storage, no hot
// parsing. Core I.11 (#ri-raw) and performance CDSA.32
// (transform-static-data-into-the- consumer-layout), retrieved for design,
// separate resident policy from actor state. Source6867b0/687d30/687ff0;
// first-combat-exchange.md.
#pragma once
#include "bw/content/formats/customization.hpp"
namespace bw::game {
struct BodyHitRegion {
  float damage_multiplier{}, reaction_threshold{};
};
struct ActorDamageDefinition {
  static constexpr std::size_t kBodyRegions = 11;
  std::array<BodyHitRegion, kBodyRegions> ordinary{};
  float melee_heavy_threshold{};
  std::array<float, 2> melee_death_weights{};
  float low_health_ratio{}, heavy_damage_threshold{},
      physical_damage_threshold{};
  float sustained_health_ratio{}, sustained_probability{},
      sustained_recovery_seconds{};
  static constexpr std::size_t kNonpositiveDeathChoices = 6;
  std::array<float, kNonpositiveDeathChoices> nonpositive_death_weights{};
  std::array<float, 3> positive_death_weights{};
  static ActorDamageDefinition parse(const formats::CustomizationDefinition&,
                                     std::string_view canonical_actor);
  float magnitude(std::uint8_t body, float incoming) const;
};
}  // namespace bw::game
