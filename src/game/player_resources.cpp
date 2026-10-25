// Recovered resource receivers and child updates, independent of animation and
// effect submission. Selection/levels remain borrowed mission authority.
// Core E.4 validates initialization/frame identity before publication; MEM.9
// fixes all state inline. Evidence and original owner offsets are in
// research/engine/subsystems/player-combat-resources.md.
#include "game/player_resources.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <stdexcept>

#include "formats/reader.hpp"

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
    : settings_(settings), mission_(mission), clock_(clock) {
  validate(settings_);
  health_ = health_ceiling_ = settings_.maximum_health;
  magic_ = magic_ceiling_ = magic_floor_ =
      settings_.weapons.front().recharge_cost;
  for (std::size_t i = 0; i < weapons_.size(); ++i) {
    weapons_.at(i) = {settings_.weapons.at(i).capacity,
                      settings_.weapons.at(i).capacity, 0};
  }
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
  return {
      health_,           health_ceiling_, settings_.maximum_health,   magic_,
      magic_ceiling_,    kMaximumMagic,   mission_.selected_weapon(), weapons_,
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
  return frame_open_ && simulation_admitted_;
}
void PlayerResources::finish_frame() {
  if (!frame_open_) throw std::logic_error("No player resource frame is open");
  if (simulation_admitted_) {
    // Child weapon updates keep one negative overshoot but discard idle debt.
    for (auto& weapon : weapons_) {
      weapon.cooldown_seconds = weapon.cooldown_seconds > 0
                                    ? weapon.cooldown_seconds - frame_delta_
                                    : 0;
    }
    const auto magic_level = mission_.levels().stats.at(1);
    magic_ = recover(
        magic_, magic_ceiling_,
        kMaximumMagic / settings_.magic_recovery_seconds.at(magic_level),
        frame_delta_);
    // Constructor installs health stat zero before progression application.
    // Later level publication does not reinstall this source owner policy.
    const auto& health = settings_.health_recovery.front();
    if (settings_.health_recovery_enabled) {
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
  auto& weapon = weapons_.at(mission_.selected_weapon());
  if (weapon.cooldown_seconds > 0) return {ShotStatus::cooling_down, 0};
  const float interval =
      1 / settings_.weapons.at(mission_.selected_weapon()).rate_per_second;
  ShotResourceResult result{ShotStatus::accepted, 0};
  // Native acceptance may consume several rounds before returning empty.
  // Return both observations; animation admission must follow source status.
  while (weapon.cooldown_seconds <= 0) {
    if (weapon.ammunition == 0) {
      result.status = ShotStatus::empty;
      break;
    }
    --weapon.ammunition;
    ++result.shot_count;
    weapon.cooldown_seconds += interval;
  }
  return result;
}
ResourceActionResult PlayerResources::reload() {
  if (!actions_admitted()) return ResourceActionResult::inactive;
  auto& weapon = weapons_.at(mission_.selected_weapon());
  if (weapon.ammunition == weapon.capacity) {
    return ResourceActionResult::magazine_full;
  }
  const float missing = 1 - static_cast<float>(weapon.ammunition) /
                                static_cast<float>(weapon.capacity);
  const float cost =
      missing * settings_.weapons.at(mission_.selected_weapon()).recharge_cost;
  if (magic_ < cost) return ResourceActionResult::insufficient_magic;
  // Recharge receivers debit current magic and refill before body animation.
  magic_ = std::max(0.0F, magic_ - cost);
  weapon.ammunition = weapon.capacity;
  weapon.cooldown_seconds = 0;
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
ShotResourceResult PlayerActionResources::try_fire() { return owner_->fire(); }
ResourceActionResult PlayerActionResources::try_reload() {
  return owner_->reload();
}
ResourceActionResult PlayerActionResources::commit_spell(PlayerSpell spell) {
  return owner_->spend_spell(spell);
}
}  // namespace bw::game
