// In-process integration of game input routing, resource publication and
// cockpit output. Physical observations traverse real policies; no platform,
// files, world fixture or fabricated spell producer is involved. I.1/E.4 and
// MEM.9 in both phases: bounded real owners and output-call observations test
// consumption/lifetimes without adding production mutation capabilities.
#include <algorithm>
#include <stdexcept>

#include "game/player_cockpit.hpp"
#include "game/player_input_routing.hpp"

namespace {
using bw::game::CockpitActivation;
using bw::game::ConceptProfile;
using bw::game::GameplayHud;
using bw::game::GameplayHudOutput;
using bw::game::GameplayInput;
using bw::game::MissionControl;
using bw::game::MissionPlayerState;
using bw::game::PlayerCockpit;
using bw::game::PlayerInputRouting;
using bw::game::PlayerResources;
using bw::game::PlayerResourceSettings;
using bw::game::PlayerSpell;
using bw::game::ResourceActionResult;
using bw::game::ShotStatus;
using bw::game::SourceRandom;
using bw::game::SpellRequestStatus;
void require(bool condition, const char* why) {
  if (!condition) throw std::runtime_error(why);
}
struct Output final : GameplayHudOutput {
  unsigned shows{}, hides{};
  std::size_t visible_commands{};
  void show(const GameplayHud& hud, std::string_view) override {
    ++shows;
    visible_commands = hud.commands().size();
  }
  void hide() noexcept override {
    ++hides;
    visible_commands = 0;
  }
};
PlayerResourceSettings settings() {
  PlayerResourceSettings result;
  result.maximum_health = 5000;
  result.magic_recovery_seconds.fill(30);
  result.weapons = {{{10, 80, 20}, {1, 16, 40}, {1.8F, 24, 40}, {60, 400, 40}}};
  for (auto& spell : result.spells) spell.fill({10, 0});
  return result;
}
void routing_and_publication() {
  bw::world::WorldClock clock;
  MissionControl control;
  MissionPlayerState mission(
      {.concept_mission = true,
       .concept_profile = ConceptProfile::development_completed});
  PlayerResources resources(settings(), mission, clock);
  PlayerInputRouting routing;
  SourceRandom random;
  Output output;
  PlayerCockpit cockpit(output, random, CockpitActivation::inspection);
  constexpr float dt = 1.0F / 60;
  const auto update = [&](GameplayInput sampled, bool ui = false) {
    const auto frame = clock.publish(dt, control.admission(ui));
    resources.begin_frame(frame);
    auto routed =
        routing.route(frame, control.route(sampled, ui), mission, resources);
    if (routed.actions.fire_trigger > 0) (void)resources.actions().try_fire();
    resources.finish_frame();
    cockpit.advance(frame, resources.snapshot(), false, routing.ring());
    return routed;
  };
  auto routed = update({.movement = {.5F, 0},
                        .fire_trigger = 1,
                        .reload_pressed = true,
                        .attack_pressed = true,
                        .weapon_cycle_pressed = true,
                        .shoulder_left_pressed = true,
                        .shoulder_left_held = true});
  require(!routed.actions.reload_pressed && !routed.actions.attack_pressed &&
              !routed.actions.weapon_cycle_pressed &&
              routed.actions.fire_trigger == 1 &&
              routed.actions.movement[0] == .5F,
          "game ring consumes face commands but preserves fire and movement");
  require(resources.snapshot().weapons[0].ammunition == 79,
          "routed fire reaches authoritative resource publication");
  cockpit.show();
  require(output.shows == 1 && output.visible_commands > 0,
          "cockpit publishes real HUD commands from live resources");
  for (unsigned i = 0; i < 60; ++i) (void)update({});
  const auto elapsed = routing.ring().elapsed_seconds;
  const auto draws = random.draws();
  const auto magic = resources.snapshot().magic;
  (void)control.set_user_paused(true);
  routed = update({.fire_trigger = 1, .face_east_pressed = true});
  require(routed.actions.fire_trigger == 0 &&
              routing.ring().elapsed_seconds == elapsed &&
              random.draws() == draws && resources.snapshot().magic == magic,
          "pause withholds ring phases, HUD randomness and resource actions");
  (void)control.set_user_paused(false);
  // A consumed resume frame has no simulation, but must replace retained menu
  // output with the previous cockpit rather than leave stale pause sprites.
  output.visible_commands = 0;
  routed = update({.reload_pressed = true}, true);
  cockpit.show();
  require(!routed.actions.reload_pressed && output.shows == 2 &&
              output.visible_commands > 0 &&
              routing.ring().elapsed_seconds == elapsed,
          "resume redraws retained cockpit without replaying input or stepping "
          "phases");
  const auto page = routing.ring().page;
  (void)update({.shoulder_right_pressed = true,
                .shoulder_left_held = true,
                .shoulder_right_held = true});
  require(routing.ring().page == page,
          "game-owned shoulder chord expires without page navigation");
  const auto draws_before_hidden = random.draws();
  PlayerCockpit hidden(output, random, CockpitActivation::hidden);
  hidden.advance(clock.publish(dt, {}), resources.snapshot(), true,
                 routing.ring());
  hidden.show();
  require(output.hides == 1 && output.visible_commands == 0 &&
              random.draws() == draws_before_hidden,
          "hidden activation clears retained commands without advancing HUD");
}
void resource_snapshot_mapping() {
  bw::world::WorldClock clock;
  MissionPlayerState mission(
      {.concept_mission = true,
       .concept_profile = ConceptProfile::development_completed});
  PlayerResources resources(settings(), mission, clock);
  SourceRandom random;
  Output output;
  PlayerCockpit cockpit(output, random, CockpitActivation::inspection);
  const auto has_image = [&](unsigned image) {
    return std::ranges::any_of(
        cockpit.presentation().commands(),
        [image](const auto& command) { return command.image == image; });
  };
  auto frame = clock.publish(.2F, {});
  resources.begin_frame(frame);
  resources.finish_frame();
  cockpit.advance(frame, resources.snapshot(), true, {});
  require(has_image(40),
          "rifle aimed reticle comes from targeting and selected weapon");
  frame = clock.publish(1.0F / 60, {});
  resources.begin_frame(frame);
  require(resources.actions().try_fire().status == ShotStatus::accepted,
          "mapping fixture admits original resource fire");
  resources.finish_frame();
  cockpit.advance(frame, resources.snapshot(), true, {});
  frame = clock.publish(1.0F / 60, {});
  resources.begin_frame(frame);
  require(resources.actions().try_reload() == ResourceActionResult::committed,
          "mapping fixture admits real recharge transaction");
  resources.finish_frame();
  const auto recharged = resources.snapshot();
  const auto before = random.draws();
  cockpit.advance(frame, recharged, true, {});
  require(recharged.recharged_weapons == 1 && random.draws() > before,
          "authoritative recharge receipt drives native cartridge ejection");
  const auto after = random.draws();
  frame = clock.publish(1.0F / 60, {false, false});
  resources.begin_frame(frame);
  resources.finish_frame();
  cockpit.advance(frame, recharged, false, {});
  require(
      random.draws() == after && has_image(40),
      "paused publication cannot consume a receipt or change targeting art");
  frame = clock.publish(1.0F / 60, {});
  resources.begin_frame(frame);
  resources.finish_frame();
  cockpit.advance(frame, resources.snapshot(), true, {});
  require(random.draws() == after,
          "next-frame receipt reset does not duplicate recharge ejection");
  mission.select_weapon(1);
  frame = clock.publish(1.0F / 60, {});
  resources.begin_frame(frame);
  resources.finish_frame();
  cockpit.advance(frame, resources.snapshot(), true, {});
  require(has_image(42),
          "shotgun aimed reticle follows authoritative weapon selection");
}
void choice_priority_and_missing_owner() {
  bw::world::WorldClock clock;
  MissionPlayerState mission(
      {.concept_mission = true,
       .concept_profile = ConceptProfile::development_completed});
  PlayerResources resources(settings(), mission, clock);
  PlayerInputRouting routing;
  constexpr float dt = 1.0F / 60;
  const auto update = [&](GameplayInput input) {
    const auto frame = clock.publish(dt, {});
    resources.begin_frame(frame);
    const auto result = routing.route(frame, input, mission, resources);
    resources.finish_frame();
    return result;
  };
  (void)update({.shoulder_left_pressed = true});
  for (unsigned i = 0; i < 60; ++i) (void)update({});
  (void)update({.face_east_pressed = true});
  bool missing = false;
  for (unsigned i = 0; i < 120; ++i) {
    const auto result = update({});
    missing |= result.spell_status == SpellRequestStatus::missing_world_owner &&
               result.requested_spell == PlayerSpell::ancient_wall;
  }
  require(
      missing && resources.snapshot().magic == 20,
      "ring publishes typed request without fabricating a cast or magic debit");
  (void)update({.shoulder_left_pressed = true});
  for (unsigned i = 0; i < 60; ++i) (void)update({});
  (void)update({.face_north_pressed = true, .face_east_pressed = true});
  for (unsigned i = 0; i < 120; ++i) {
    require(
        !update({}).requested_spell,
        "north cancellation takes priority over simultaneous east selection");
  }
}
}  // namespace
void gameplay_integration_contracts() {
  routing_and_publication();
  choice_priority_and_missing_owner();
  resource_snapshot_mapping();
}
