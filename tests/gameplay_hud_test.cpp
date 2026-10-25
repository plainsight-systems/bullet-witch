// Native cockpit and ring public-contract checks. Expected art/positions come
// from the literal original draw witness, not a second implementation. Fixed
// snapshots exercise resource separation, pause and command lifetime; no files,
// device or synthetic renderer. Core F.16/SL.con.3 and performance MEM.9,
// consulted during both phases, bound all observations and command storage.
#include "game/gameplay_hud.hpp"

#include <cmath>
#include <limits>
#include <stdexcept>
#include <type_traits>

#include "game/source_random.hpp"

namespace {
void require(bool value, const char* reason) {
  if (!value) throw std::runtime_error(reason);
}
bool near(float first, float second) {
  return std::abs(first - second) < .0001F;
}
static_assert(!std::is_copy_constructible_v<bw::game::GameplayHud>);
static_assert(!std::is_move_constructible_v<bw::game::GameplayHud>);
const bw::game::HudSprite& image(const bw::game::GameplayHud& hud, unsigned id,
                                 std::size_t occurrence = 0) {
  for (const auto& command : hud.commands()) {
    if (command.image == id && occurrence-- == 0) return command;
  }
  throw std::runtime_error("Expected original HUD sprite absent");
}
bw::game::GameplayHudSnapshot observation() {
  bw::game::GameplayHudSnapshot result;
  result.health = {100, 100, 100};
  result.magic = {20, 80, 100};
  result.ammunition = result.ammunition_capacity = 60;
  return result;
}
void source_layout_and_resources() {
  bw::game::GameplayHud hud;
  bw::game::SourceRandom random;
  auto observed = observation();
  hud.advance(observed, 1.0F / 60, random);
  require(
      image(hud, 6).destination.x == 858 && image(hud, 6).destination.y == 30,
      "native cockpit background witness");
  require(near(image(hud, 9, 1).destination.width, 302),
          "full health right-anchored fill witness");
  require(near(image(hud, 9, 3).destination.width, 60.4F),
          "magic current is distinct from recoverable ceiling");
  require(near(image(hud, 14, 1).destination.x, 928.4F) &&
              image(hud, 14, 1).destination.y == 80,
          "recoverable magic marker witness");
  observed.health.current = 25;
  hud.advance(observed, 1.0F / 60, random);
  require(
      image(hud, 9).destination.width > image(hud, 9, 1).destination.width,
      "depletion trail is presentation state, separate from current health");
  const auto before = image(hud, 9).destination.width;
  hud.advance(observed, 0, random);
  require(image(hud, 9).destination.width == before,
          "zero world time freezes gauge interpolation");
  observed.health.current = std::numeric_limits<float>::quiet_NaN();
  bool rejected = false;
  try {
    hud.advance(observed, 1.0F / 60, random);
  } catch (const std::invalid_argument&) {
    rejected = true;
  }
  require(rejected && image(hud, 9).destination.width == before,
          "bad snapshots reject before invalidating prior commands");
  observed.health.current = 25;
  observed.ammunition = observed.ammunition_capacity =
      bw::game::ammunition_hud_maximum + 1;
  const auto draws_before = random.draws();
  rejected = false;
  try {
    hud.advance(observed, 1.0F / 60, random);
  } catch (const std::invalid_argument&) {
    rejected = true;
  }
  require(rejected && image(hud, 9).destination.width == before &&
              random.draws() == draws_before,
          "native ammunition bound rejects before meter updates or RNG "
          "consumption");
}
void ring_admission_and_lifetime() {
  using bw::game::PlayerSpell;
  using bw::game::SpellAvailabilitySet;
  using bw::game::SpellSelectionFlow;
  using bw::game::SpellSelectionInput;
  SpellSelectionFlow flow;
  SpellAvailabilitySet availability{};
  availability.at(static_cast<std::size_t>(PlayerSpell::elemental_shot)) = {
      true, true};
  SpellSelectionInput previous;
  previous.previous_page = true;
  (void)flow.advance(previous, availability, 1.0F / 60);
  require(flow.active() && flow.presentation().page == 1,
          "first shoulder opens source page one without cycling");
  for (unsigned index = 0; index < 3; ++index) {
    (void)flow.advance({}, availability, 1.0F / 60);
  }
  SpellSelectionInput choose;
  choose.choice = 2;
  const auto rejected = flow.advance(choose, availability, 1.0F / 60);
  require(rejected.rejected && !rejected.requested && flow.active(),
          "unlearned spell cannot emit gameplay request");
  choose.choice = 3;
  require(!flow.advance(choose, availability, 1.0F / 60).requested,
          "native selection phase precedes request publication");
  const auto request = flow.advance({}, availability, 1.0F / 60);
  require(request.requested == PlayerSpell::elemental_shot,
          "source lower face slot publishes elemental shot");
  require(!flow.advance({}, availability, 1.0F / 60).requested,
          "held selection publishes exactly once");
  const float elapsed = flow.presentation().elapsed_seconds;
  (void)flow.advance({}, availability, 0);
  require(flow.presentation().elapsed_seconds == elapsed,
          "paused observation cannot advance ring phases");
  for (unsigned index = 0; index < 60; ++index) {
    (void)flow.advance({}, availability, 1.0F / 60);
  }
  require(!flow.active() && !flow.presentation().visible,
          "selected ring finishes native highlight then fade");
  flow.reset();
  require(!flow.active() && !flow.presentation().visible,
          "mission teardown clears retained UI state");
}
void ring_expiry_keeps_visual_lifecycle() {
  bw::game::SpellSelectionFlow flow;
  bw::game::SpellAvailabilitySet available;
  available.fill({true, true});
  constexpr float kFrame = 1.F / 60.F;
  const auto close = flow.advance({.expire = true}, available, kFrame);
  require(close.cancelled && !flow.active() && flow.presentation().visible,
          "inactive shoulder chord expires input while native art opens and "
          "retires");
  for (unsigned frame = 0; frame < 60; ++frame) {
    require(!flow.advance({.choice = 2}, available, kFrame).requested,
            "expired opening cannot admit a spell");
  }
  require(!flow.presentation().visible, "expired opening retires");
  (void)flow.advance({.open = true}, available, kFrame);
  for (unsigned frame = 0; frame < 10; ++frame) {
    (void)flow.advance({}, available, kFrame);
  }
  (void)flow.advance({.next_page = true}, available, kFrame);
  const auto page = flow.presentation().page;
  (void)flow.advance({.expire = true}, available, kFrame);
  require(!flow.active() && flow.presentation().visible &&
              flow.presentation().page == page,
          "shoulder chord expires page transition without another page change");
  for (unsigned frame = 0; frame < 60; ++frame) {
    (void)flow.advance({}, available, kFrame);
  }
  require(!flow.presentation().visible, "expired page transition retires");
  (void)flow.advance({.open = true}, available, kFrame);
  for (unsigned frame = 0; frame < 200 && flow.active(); ++frame) {
    (void)flow.advance({}, available, kFrame);
  }
  require(
      !flow.active() && flow.presentation().visible,
      "inactivity first revokes input without deleting visible native state");
  for (unsigned frame = 0; frame < 60; ++frame) {
    (void)flow.advance({}, available, kFrame);
  }
  require(!flow.presentation().visible,
          "inactivity follows native close phases");
}
void ring_original_art() {
  bw::game::GameplayHud hud;
  bw::game::SourceRandom random;
  auto observed = observation();
  auto& ring = observed.magic_ring;
  ring.visible = true;
  ring.opacity = ring.name_opacity = ring.art_opacity = 1;
  ring.choices.fill({true, true});
  ring.selected_choice = 3;
  ring.selection_progress = .5F;
  hud.advance(observed, 1.0F / 60, random);
  require(image(hud, 120).destination.x == 445 &&
              image(hud, 114).destination.x == 791 &&
              image(hud, 102).destination.y == 511,
          "original ring quadrant art witness");
  require(image(hud, 121, 2).canvas_pivot &&
              near(image(hud, 121, 2).rotation_radians, 3.1415927F),
          "flames rotate around authored canvas center");
  require(image(hud, 436).additive && image(hud, 436).color.alpha == 1,
          "native selected flame uses additive blending");
}
}  // namespace
void gameplay_hud_contracts() {
  source_layout_and_resources();
  ring_admission_and_lifetime();
  ring_original_art();
  ring_expiry_keeps_visual_lifecycle();
}
