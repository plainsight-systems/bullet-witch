// Behavioral resource contracts: persistent weapon owners, original debit
// lanes and update order, checkpoint initialization and frame authority.
// Fixtures supply authored-shaped settings through the production constructor;
// no private mutation or test-only setters. Fixed bounded state, no allocation
// on successful updates. Design I.1/E.5, implementation E.4 and both-phase
// MEM.9 match the owner's contracts; independent source evidence is recorded
// in player-combat-resources.md rather than inferred from these fixtures.
#include "game/actors/alicia/player_resources.hpp"

#include <cmath>
#include <limits>
#include <stdexcept>
#include <type_traits>

namespace {
using bw::game::CheckpointPlayer;
using bw::game::MissionPlayerEntry;
using bw::game::MissionPlayerState;
using bw::game::PlayerActionResources;
using bw::game::PlayerResources;
using bw::game::PlayerResourceSettings;
using bw::game::PlayerSpell;
using bw::game::ResourceActionResult;
using bw::game::ShotStatus;
using bw::world::WorldClock;
static_assert(!std::is_copy_constructible_v<PlayerResources> &&
              !std::is_move_constructible_v<PlayerResources>);
template <class T>
concept HasFrameControl = requires(T& value) { value.finish_frame(); };
static_assert(!HasFrameControl<PlayerActionResources>);
void require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}
bool near(float left, float right) { return std::abs(left - right) < 0.0001F; }
template <class Operation>
void rejected(Operation operation) {
  bool did_reject = false;
  try {
    operation();
  } catch (const std::exception&) {
    did_reject = true;
  }
  require(did_reject, "invalid resource operation must reject");
}
PlayerResourceSettings settings() {
  PlayerResourceSettings result;
  result.maximum_health = 5000;
  result.health_recovery_enabled = true;
  result.health_recovery.fill({20, 2, 180, 6});
  result.magic_recovery_seconds = {60, 50, 40, 30};
  result.weapons = {{{10, 80, 20}, {1, 16, 40}, {1.8F, 24, 40}, {60, 400, 40}}};
  constexpr std::array<float, 9> costs{40, 20, 35, 30, 10, 40, 80, 90, 100};
  constexpr std::array<float, 9> debits{10, 0, 15, 10, 0, 20, 60, 70, 80};
  for (std::size_t i = 0; i < result.spells.size(); ++i) {
    result.spells.at(i).fill({costs.at(i), debits.at(i)});
  }
  result.spells.at(1).at(1).current = 15;
  result.spells.at(1).at(2).current = 10;
  return result;
}
MissionPlayerEntry all_learned() {
  MissionPlayerEntry result;
  result.progression.levels.weapons.fill(1);
  result.progression.levels.spells.fill(1);
  return result;
}
void initial_and_reload_lanes() {
  const MissionPlayerState mission(all_learned());
  WorldClock clock;
  PlayerResources resources(settings(), mission, clock);
  auto actions = resources.actions();
  const auto initial = resources.snapshot();
  require(initial.health == 5000 && initial.magic == 20 &&
              initial.recoverable_magic == 20 && initial.maximum_magic == 100,
          "completed learning must not imply full magic");
  require(actions.try_fire().status == ShotStatus::inactive,
          "commands require coordinator admission");
  resources.begin_frame(clock.publish(0, {}));
  require(actions.try_reload() == ResourceActionResult::magazine_full,
          "full magazine cannot consume magic");
  require(resources.snapshot().recharged_weapons == 0,
          "rejected recharge publishes no receipt");
  require(actions.try_fire().shot_count == 1 &&
              actions.try_fire().status == ShotStatus::cooling_down,
          "first shot debits one round then cadence rejects");
  require(actions.try_reload() == ResourceActionResult::committed,
          "partial recharge can commit before animation completes");
  const auto reloaded = resources.snapshot();
  require(near(reloaded.magic, 19.75F) && reloaded.recoverable_magic == 20 &&
              reloaded.weapons.front().ammunition == 80 &&
              reloaded.weapons.front().cooldown_seconds == 0,
          "recharge affects current magic only and refills immediately");
  resources.finish_frame();
  require(resources.snapshot().recharged_weapons == 1,
          "committed recharge receipt survives finish for HUD observation");
  resources.begin_frame(clock.publish(0.1F, {false, false}));
  require(resources.snapshot().recharged_weapons == 0 &&
              near(resources.snapshot().magic, 19.75F),
          "next paused frame clears receipts without changing resources");
  resources.finish_frame();
  resources.begin_frame(clock.publish(0.1F, {}));
  resources.finish_frame();
  require(near(resources.snapshot().magic, 19.95F),
          "magic recovery uses effective stat-one table entry");
}
void persistent_magazines_and_source_overshoot() {
  MissionPlayerState mission(all_learned());
  WorldClock clock;
  auto config = settings();
  config.weapons.front().capacity = 2;
  PlayerResources resources(config, mission, clock);
  auto actions = resources.actions();
  resources.begin_frame(clock.publish(0.5F, {}));
  require(actions.try_fire().shot_count == 1, "first catch-up seed shot");
  resources.finish_frame();
  mission.select_weapon(1);
  resources.begin_frame(clock.publish(0, {}));
  require(actions.try_fire().shot_count == 1, "second weapon independent shot");
  mission.select_weapon(0);
  const auto exhausted = actions.try_fire();
  require(exhausted.shot_count == 1 && exhausted.status == ShotStatus::empty,
          "source can return empty after earlier accepted catch-up shot");
  require(resources.snapshot().weapons.at(1).ammunition == 15,
          "weapon selection preserves every magazine");
  resources.finish_frame();
  require(resources.snapshot().weapons.front().cooldown_seconds == 0,
          "old negative overshoot is discarded by child update");
}
void spells_checkpoint_and_recovery() {
  auto entry = all_learned();
  entry.checkpoint = CheckpointPlayer{entry.progression.levels, 0, 2500, 100};
  MissionPlayerState mission(entry);
  WorldClock clock;
  PlayerResources resources(settings(), mission, clock, entry.checkpoint);
  auto actions = resources.actions();
  resources.begin_frame(clock.publish(0, {}));
  require(actions.commit_spell(PlayerSpell::elemental_shot) ==
                  ResourceActionResult::committed &&
              resources.snapshot().magic == 60 &&
              resources.snapshot().recoverable_magic == 90,
          "cast commits distinct current and recoverable debits");
  require(actions.commit_spell(PlayerSpell::meteor) ==
              ResourceActionResult::insufficient_magic,
          "affordability rejects without reducing either pool");
  resources.finish_frame();
  resources.begin_frame(clock.publish(1, {}));
  resources.finish_frame();
  require(near(resources.snapshot().magic, 62) &&
              near(resources.snapshot().recoverable_health, 2527.7778F) &&
              near(resources.snapshot().health, 2527.7778F),
          "ordinary recovery advances ceiling before current health");
  mission.set_script_disabled_spells(1);
  require(!resources.can_afford_spell(PlayerSpell::elemental_shot),
          "affordability observes mission-owned mask immediately");
  resources.begin_frame(clock.publish(0, {}));
  require(actions.commit_spell(PlayerSpell::elemental_shot) ==
              ResourceActionResult::unavailable,
          "masked cast cannot debit resources");
  resources.finish_frame();
  auto concept_entry = entry;
  concept_entry.concept_mission = true;
  MissionPlayerState concept_mission(concept_entry);
  PlayerResources concept_resources(settings(), concept_mission, clock,
                                    entry.checkpoint);
  require(concept_resources.snapshot().magic == 20,
          "concept entry cannot inherit campaign checkpoint resource pool");
}
void pause_and_frame_authority() {
  MissionPlayerState mission(all_learned());
  WorldClock clock, foreign;
  PlayerResources resources(settings(), mission, clock);
  auto actions = resources.actions();
  rejected([&] { resources.finish_frame(); });
  rejected([&] { resources.begin_frame(foreign.publish(1, {})); });
  const auto frame = clock.publish(1, {false, false});
  resources.begin_frame(frame);
  rejected([&] { resources.begin_frame(clock.publish(1, {})); });
  require(actions.try_fire().status == ShotStatus::inactive &&
              actions.try_reload() == ResourceActionResult::inactive &&
              actions.commit_spell(PlayerSpell::ancient_wall) ==
                  ResourceActionResult::inactive &&
              resources.can_afford_spell(PlayerSpell::ancient_wall),
          "paused owner allows read-only affordance but no actions");
  resources.finish_frame();
  rejected([&] { resources.begin_frame(frame); });
  require(resources.snapshot().magic == 20 &&
              resources.snapshot().weapons.front().ammunition == 80,
          "foreign stale reentrant and paused requests preserve resources");
  resources.begin_frame(clock.publish(0, {}));
  require(actions.commit_spell(PlayerSpell::ancient_wall) ==
              ResourceActionResult::committed,
          "source zero-delta admitted frame still accepts actions");
  resources.finish_frame();
}
void malformed_settings_reject() {
  MissionPlayerState mission(all_learned());
  WorldClock clock;
  auto config = settings();
  config.weapons.front().capacity = 512;
  rejected([&] { PlayerResources invalid(config, mission, clock); });
  config = settings();
  config.magic_recovery_seconds.front() =
      std::numeric_limits<float>::quiet_NaN();
  rejected([&] { PlayerResources invalid(config, mission, clock); });
}
void exhausted_pool_and_ceiling_floor() {
  auto entry = all_learned();
  entry.checkpoint = CheckpointPlayer{entry.progression.levels, 1, 0, 100};
  MissionPlayerState mission(entry);
  WorldClock clock;
  auto config = settings();
  config.weapons.at(1).capacity = 1;
  PlayerResources resources(config, mission, clock, entry.checkpoint);
  auto actions = resources.actions();
  resources.begin_frame(clock.publish(0, {}));
  require(actions.try_fire().shot_count == 1, "single round magazine empties");
  require(actions.commit_spell(PlayerSpell::meteor) ==
                  ResourceActionResult::committed &&
              resources.snapshot().magic == 0 &&
              resources.snapshot().recoverable_magic == 20,
          "largest debit retains constructor minimum recoverable pool");
  require(actions.try_reload() == ResourceActionResult::insufficient_magic &&
              resources.snapshot().weapons.at(1).ammunition == 0,
          "unaffordable recharge leaves ammunition empty");
  resources.finish_frame();
  resources.begin_frame(clock.publish(60, {}));
  resources.finish_frame();
  require(resources.snapshot().magic == 20 &&
              resources.snapshot().recoverable_magic == 20,
          "time cannot regenerate the spent ceiling without a pickup");
}
}  // namespace
void player_resource_contracts() {
  initial_and_reload_lanes();
  persistent_magazines_and_source_overshoot();
  spells_checkpoint_and_recovery();
  pause_and_frame_authority();
  malformed_settings_reject();
  exhausted_pool_and_ceiling_floor();
}
