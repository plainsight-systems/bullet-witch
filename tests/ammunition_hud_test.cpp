// Bounded native cockpit observations: compare authored draw geometry and one
// ejection against literal-x86 witnesses, then exercise bank reuse, explicit
// recharge receipts and independent outgoing/current weapon children.
// No game assets or private state mutation. Core E.4 and both-phase MEM.9
// preserve rejection and fixed-array publication contracts; see
// ammunition-hud.md.
#include "game/ammunition_hud.hpp"

#include <cmath>
#include <limits>
#include <stdexcept>

#include "bw/math/scalar.hpp"

namespace {
using bw::game::AmmunitionHud;
using bw::game::SourceRandom;
void require(bool value, const char* message) {
  if (!value) throw std::runtime_error(message);
}
bool near(float left, float right) { return std::abs(left - right) < .0002F; }
void native_layout_and_ejection() {
  AmmunitionHud hud;
  SourceRandom random;
  hud.advance({0, 80}, .05F, random);
  require(hud.commands().size() == 42 &&
              near(hud.commands().front().destination.x, 1145.5544F) &&
              near(hud.commands().front().color.alpha, 84 / 255.0F) &&
              hud.commands().back().color.alpha == 1,
          "native arrival offsets cartridge row while digits remain visible");
  hud.advance({0, 80}, .15F, random);
  require(hud.commands()[19].destination.y == 119 && random.draws() == 0,
          "last cartridge drops five authored pixels without drawing RNG");
  hud.advance({0, 79}, 1.0F / 60, random);
  require(random.draws() == 3 && hud.commands()[19].destination.x == 964,
          "shot observation enters ejection phase before moving cartridge");
  hud.advance({0, 79}, 1.0F / 60, random);
  const auto& flying = hud.commands()[19];
  require(near(flying.destination.x, 966.026367F) &&
              near(flying.destination.y, 105.004166F) &&
              near(flying.rotation_radians, -.106589325F) &&
              flying.top_left_pivot,
          "source seeded ejection direction gravity and rotation match native");
}
void recharge_and_switch_ownership() {
  AmmunitionHud hud;
  SourceRandom random;
  hud.advance({0, 80}, .2F, random);
  hud.advance({0, 80, true}, 0, random);
  require(hud.commands().size() == 62 && random.draws() == 60,
          "explicit same-count recharge ejects old row and uses next bank");
  hud.advance({1, 16}, .05F, random);
  bool old_weapon = false, new_weapon = false;
  for (const auto& command : hud.commands()) {
    old_weapon |= command.image == 429;
    new_weapon |= command.image == 430;
  }
  require(old_weapon && new_weapon, "weapon change retains outgoing child");
  for (int i = 0; i < 30; ++i) hud.advance({1, 16}, .05F, random);
  for (const auto& command : hud.commands()) {
    require(command.image != 429,
            "retired outgoing child publishes no sprites");
  }
  require(hud.commands().size() == 18,
          "shotgun owns eight plus eight reserve and two digits");
}
void flash_and_validation() {
  AmmunitionHud hud;
  SourceRandom random;
  hud.advance({0, 80, false, true}, .1F, random);
  require(hud.commands().size() == 45 && hud.commands().back().image == 434 &&
              near(hud.commands().back().scale, .568F) &&
              near(hud.commands().back().color.alpha, .87F),
          "native enchant entry produces independent centered burst");
  const auto old_count = hud.commands().size();
  bool rejected = false;
  try {
    hud.advance({4, 80}, std::numeric_limits<float>::quiet_NaN(), random);
  } catch (const std::invalid_argument&) {
    rejected = true;
  }
  require(rejected && hud.commands().size() == old_count && random.draws() == 0,
          "invalid observations preserve command storage and shared RNG");
  hud.reset();
  require(hud.commands().empty(), "reset clears retained publication");
  require(bw::math::eased_tangent_interpolate<float>({15.0F, 0.0F, 0.0F, 7.0F},
                                                     0.0F) == 15 &&
              bw::math::eased_tangent_interpolate<float>(
                  {15.0F, 0.0F, 0.0F, 7.0F}, 1.0F) == 0,
          "shared source easing retains exact endpoints");
}
}  // namespace
void ammunition_hud_contracts() {
  native_layout_and_ejection();
  recharge_and_switch_ownership();
  flash_and_validation();
}
