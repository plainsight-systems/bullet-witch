// Recovered resource receivers and child updates, independent of animation and
// effect submission. Selection/levels remain borrowed mission authority.
// Core E.4 validates initialization/frame identity before publication; MEM.9
// fixes all state inline. Evidence and original owner offsets are in
// research/engine/subsystems/player-combat-resources.md.
#include "player_resources.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <stdexcept>

#include "bw/content/formats/binary_reader.hpp"

namespace bw::game {
namespace {
constexpr float kMaximumMagic = 100;
constexpr unsigned kMaximumNativeAmmunition = 511;
constexpr std::size_t kFirstStorySpell = 6;
constexpr std::uint64_t kInitialHealth = 0xac;
constexpr std::uint64_t kHealthRecoveryEnabled = 0x15c;
constexpr std::uint64_t kHealthCurrentSeconds = 0x160;
constexpr std::uint64_t kHealthCurrentDelay = 0x170;
constexpr std::uint64_t kHealthCeilingSeconds = 0x180;
constexpr std::uint64_t kHealthCeilingDelay = 0x190;
constexpr std::uint64_t kMagicRecoverySeconds = 0x1a0;
constexpr std::uint64_t kWeaponCadence = 0x90;
constexpr std::uint64_t kWeaponCapacity = 0x94;
constexpr std::uint64_t kRechargeCost = 0x60;
using Reader = resources::detail::Reader;
float scalar(const Reader& reader, std::uint64_t offset) {
  return std::bit_cast<float>(reader.u32(offset));
}
void finite_nonnegative(float value) {
  if (!std::isfinite(value) || value < 0) {
    throw std::invalid_argument("Invalid player resource setting");
  }
}
void positive_interval(float value) {
  if (!std::isfinite(value) || value <= 0 || !std::isfinite(1.0F / value)) {
    throw std::invalid_argument("Invalid player resource interval");
  }
}
void validate(const PlayerResourceSettings& settings) {
  positive_interval(settings.maximum_health);
  for (const auto& health : settings.health_recovery) {
    for (float seconds : {health.current_seconds, health.ceiling_seconds}) {
      if (!std::isfinite(seconds)) {
        throw std::invalid_argument("Invalid health recovery interval");
      }
      if (seconds > 0) positive_interval(seconds);
    }
    finite_nonnegative(health.current_delay);
    finite_nonnegative(health.ceiling_delay);
  }
  for (float seconds : settings.magic_recovery_seconds) {
    positive_interval(seconds);
  }
  for (const auto& weapon : settings.weapons) {
    positive_interval(weapon.rate_per_second);
    if (weapon.capacity == 0 || weapon.capacity > kMaximumNativeAmmunition) {
      throw std::invalid_argument("Unsupported native weapon capacity");
    }
    finite_nonnegative(weapon.recharge_cost);
  }
  if (settings.weapons.front().recharge_cost > kMaximumMagic) {
    throw std::invalid_argument("Initial magic exceeds native maximum");
  }
  for (const auto& levels : settings.spells) {
    for (const auto& cost : levels) {
      finite_nonnegative(cost.current);
      finite_nonnegative(cost.recoverable);
    }
  }
}
// Accumulate in wider arithmetic before applying the source ceiling, avoiding
// an overflowed intermediate under an otherwise valid scaled world interval.
float recover(float current, float ceiling, double rate, float delta) {
  return static_cast<float>(
      std::min(static_cast<double>(ceiling), current + rate * delta));
}
}  // namespace

PlayerResourceSettings PlayerResourceSettings::parse(
    const formats::CustomizationDefinition& tree) {
  PlayerResourceSettings result;
  constexpr std::array setup_path{std::string_view("witch"),
                                  std::string_view("general"),
                                  std::string_view("setup")};
  constexpr std::array body_path{
      std::string_view("witch"), std::string_view("actor"),
      std::string_view("pw0"), std::string_view("body")};
  const Reader setup{tree.lookup(setup_path)};
  const Reader body{tree.lookup(body_path)};
  result.maximum_health = scalar(setup, kInitialHealth);
  result.health_recovery_enabled = body.u8(kHealthRecoveryEnabled) != 0;
  for (std::size_t i = 0; i < result.health_recovery.size(); ++i) {
    const auto offset = i * sizeof(float);
    result.health_recovery.at(i) = {
        scalar(body, kHealthCurrentSeconds + offset),
        scalar(body, kHealthCurrentDelay + offset),
        scalar(body, kHealthCeilingSeconds + offset),
        scalar(body, kHealthCeilingDelay + offset)};
    result.magic_recovery_seconds.at(i) =
        scalar(body, kMagicRecoverySeconds + offset);
  }
  constexpr std::array weapon_names{
      std::string_view("wa0"), std::string_view("wa1"), std::string_view("wa2"),
      std::string_view("wa3")};
  for (std::size_t i = 0; i < weapon_names.size(); ++i) {
    const std::array path{std::string_view("witch"), std::string_view("weapon"),
                          weapon_names.at(i), std::string_view("00_spec")};
    const Reader reader{tree.lookup(path)};
    result.weapons.at(i) = {scalar(reader, kWeaponCadence),
                            reader.u32(kWeaponCapacity),
                            scalar(reader, kRechargeCost)};
  }
  // Compatibility spell names are kept at the format boundary only.
  constexpr std::array spell_names{
      std::string_view("elemental_shot"), std::string_view("ancient_wall"),
      std::string_view("rose_bomb"),      std::string_view("will_power"),
      std::string_view("sacrifice"),      std::string_view("raven_panice"),
      std::string_view("thunder"),        std::string_view("tornade"),
      std::string_view("meteor")};
  constexpr std::array levels{std::string_view("01"), std::string_view("02"),
                              std::string_view("03")};
  for (std::size_t spell = 0; spell < spell_names.size(); ++spell) {
    for (std::size_t level = 0; level < levels.size(); ++level) {
      const std::array path{std::string_view("witch"),
                            std::string_view("magic"), spell_names.at(spell),
                            levels.at(spell >= kFirstStorySpell ? 0 : level)};
      const Reader reader{tree.lookup(path)};
      result.spells.at(spell).at(level) = {scalar(reader, 0),
                                           scalar(reader, 4)};
    }
  }
  validate(result);
  return result;
}

PlayerResources::PlayerResources(const PlayerResourceSettings& settings,
                                 const MissionPlayerState& mission,
                                 const world::WorldClock& clock,
                                 std::optional<CheckpointPlayer> checkpoint)
    : settings_(settings),
      mission_(mission),
      clock_(clock),
      weapons_{WeaponResources(settings.weapons.at(0).capacity,
                               settings.weapons.at(0).rate_per_second),
               WeaponResources(settings.weapons.at(1).capacity,
                               settings.weapons.at(1).rate_per_second),
               WeaponResources(settings.weapons.at(2).capacity,
                               settings.weapons.at(2).rate_per_second),
               WeaponResources(settings.weapons.at(3).capacity,
                               settings.weapons.at(3).rate_per_second)} {
  validate(settings_);
  health_ = health_ceiling_ = settings_.maximum_health;
  magic_ = magic_ceiling_ = magic_floor_ =
      settings_.weapons.front().recharge_cost;
  // Concept entry discarded checkpoint authority already. Positive restored
  // lanes preserve the constructor maximum/floor; zero means no restore.
  if (mission_.origin() == ProgressionOrigin::checkpoint) {
    if (!checkpoint) {
      throw std::invalid_argument("Checkpoint resource values are required");
    }
    finite_nonnegative(checkpoint->health);
    finite_nonnegative(checkpoint->magic);
    if (checkpoint->health > settings_.maximum_health ||
        checkpoint->magic > kMaximumMagic) {
      throw std::invalid_argument("Checkpoint resources exceed maximum");
    }
    if (checkpoint->health > 0) health_ = health_ceiling_ = checkpoint->health;
    if (checkpoint->magic > 0) magic_ = magic_ceiling_ = checkpoint->magic;
  }
}
PlayerResourceSnapshot PlayerResources::snapshot() const {
  std::array<WeaponResourceSnapshot, player_weapon_count> weapons{};
  for (std::size_t i = 0; i < weapons.size(); ++i) {
    weapons.at(i) = weapons_.at(i).snapshot();
  }
  return {
      health_,           health_ceiling_, settings_.maximum_health,   magic_,
      magic_ceiling_,    kMaximumMagic,   mission_.selected_weapon(), weapons,
      recharged_weapons_};
}
SpellResourceCost PlayerResources::spell_cost(PlayerSpell spell) const {
  const auto index = static_cast<unsigned>(spell);
  const auto learned = mission_.levels().spells.at(index);
  const auto level = learned == 0 ? 0U : static_cast<unsigned>(learned - 1U);
  return settings_.spells.at(index).at(level);
}
bool PlayerResources::can_afford_spell(PlayerSpell spell) const {
  return mission_.spell_available(static_cast<unsigned>(spell)) &&
         magic_ >= spell_cost(spell).current;
}
void PlayerResources::begin_frame(const world::WorldFrame& frame) {
  if (frame_open_ || !frame.belongs_to(clock_) ||
      frame.sequence() <= last_sequence_) {
    throw std::invalid_argument("Invalid player resource frame");
  }
  last_sequence_ = frame.sequence();
  frame_delta_ = frame.delta_seconds();
  simulation_admitted_ = frame.admission().simulation;
  frame_open_ = true;
  recharged_weapons_ = 0;
}
bool PlayerResources::actions_admitted() const noexcept {
  return frame_open_ && simulation_admitted_ && health_ > 0;
}
void PlayerResources::finish_frame() {
  if (!frame_open_) throw std::logic_error("No player resource frame is open");
  if (simulation_admitted_) {
    for (auto& weapon : weapons_) weapon.advance(frame_delta_);
    const auto magic_level = mission_.levels().stats.at(1);
    magic_ = recover(
        magic_, magic_ceiling_,
        kMaximumMagic / settings_.magic_recovery_seconds.at(magic_level),
        frame_delta_);
    // Constructor installs health stat zero before progression application.
    // Later level publication does not reinstall this source owner policy.
    const auto& health = settings_.health_recovery.front();
    // Death replaces the original Damager timed update; ordinary recovery is
    // no longer dispatched once its health transaction enters death.
    if (health_recovery_active_ && settings_.health_recovery_enabled) {
      if (health.ceiling_seconds > 0 &&
          health_ceiling_ < settings_.maximum_health) {
        health_ceiling_delay_ -= frame_delta_;
        if (health_ceiling_delay_ <= 0) {
          health_ceiling_ = recover(
              health_ceiling_, settings_.maximum_health,
              settings_.maximum_health / health.ceiling_seconds, frame_delta_);
        }
      }
      if (health.current_seconds > 0 && health_ < health_ceiling_) {
        health_delay_ -= frame_delta_;
        if (health_delay_ <= 0) {
          health_ = recover(health_, health_ceiling_,
                            settings_.maximum_health / health.current_seconds,
                            frame_delta_);
        }
      }
    }
  }
  frame_open_ = false;
}
ShotResourceResult PlayerResources::fire() {
  if (!actions_admitted()) return {};
  const auto selected = mission_.selected_weapon();
  const auto receipt = weapons_.at(selected).try_fire();
  return {receipt.status,
          receipt.shot_count,
          selected,
          receipt.first_shot_age_seconds,
          receipt.interval_seconds,
          mission_.levels().weapons.at(selected)};
}
ResourceActionResult PlayerResources::reload() {
  if (!actions_admitted()) return ResourceActionResult::inactive;
  auto& weapon = weapons_.at(mission_.selected_weapon());
  const auto state = weapon.snapshot();
  if (state.ammunition == state.capacity) {
    return ResourceActionResult::magazine_full;
  }
  const float missing = 1 - static_cast<float>(state.ammunition) /
                                static_cast<float>(state.capacity);
  const float cost =
      missing * settings_.weapons.at(mission_.selected_weapon()).recharge_cost;
  if (magic_ < cost) return ResourceActionResult::insufficient_magic;
  // Recharge receivers debit current magic and refill before body animation.
  magic_ = std::max(0.0F, magic_ - cost);
  weapon.recharge();
  recharged_weapons_ |=
      static_cast<std::uint8_t>(1U << mission_.selected_weapon());
  return ResourceActionResult::committed;
}
ResourceActionResult PlayerResources::spend_spell(PlayerSpell spell) {
  if (!actions_admitted()) return ResourceActionResult::inactive;
  if (!mission_.spell_available(static_cast<unsigned>(spell))) {
    return ResourceActionResult::unavailable;
  }
  const auto cost = spell_cost(spell);
  if (magic_ < cost.current) return ResourceActionResult::insufficient_magic;
  magic_ = std::max(0.0F, magic_ - cost.current);
  magic_ceiling_ = std::max(magic_floor_, magic_ceiling_ - cost.recoverable);
  return ResourceActionResult::committed;
}
HealthDamageResult PlayerResources::receive_damage(float magnitude) {
  finite_nonnegative(magnitude);
  if (!actions_admitted()) {
    return {health_, health_ceiling_, false, health_ > 0};
  }
  const auto result =
      apply_health_damage({health_, health_ceiling_, settings_.maximum_health,
                           magnitude, 1, HealthDamageMode::player_camera});
  if (result.accepted) {
    health_ = result.current;
    if (!result.alive) health_recovery_active_ = false;
    health_ceiling_ = result.recoverable;
    const auto& recovery = settings_.health_recovery.front();
    health_delay_ = recovery.current_delay;
    health_ceiling_delay_ = recovery.ceiling_delay;
  }
  return result;
}
// Source death messages restore only the recoverable ceiling. The constructor
// multiplier is one; no admitted producer changes it. I.1/E.4 and MEM.9 were
// reread: validate before writes, accept only the coordinator's live frame.
ResourceActionResult PlayerResources::receive_defeat(float authored_amount) {
  finite_nonnegative(authored_amount);
  if (!frame_open_ || !simulation_admitted_) {
    return ResourceActionResult::inactive;
  }
  magic_ceiling_ = std::min(kMaximumMagic, magic_ceiling_ + authored_amount);
  return ResourceActionResult::committed;
}
ResourceActionResult PlayerDefeatResources::receive(float authored_amount) {
  return owner_->receive_defeat(authored_amount);
}
HealthDamageResult PlayerDamageResources::receive(float magnitude) {
  return owner_->receive_damage(magnitude);
}
PlayerResourceSnapshot PlayerDamageResources::snapshot() const {
  return owner_->snapshot();
}
ShotResourceResult PlayerActionResources::try_fire() { return owner_->fire(); }
ResourceActionResult PlayerActionResources::try_reload() {
  return owner_->reload();
}
ResourceActionResult PlayerActionResources::commit_spell(PlayerSpell spell) {
  return owner_->spend_spell(spell);
}
}  // namespace bw::game
