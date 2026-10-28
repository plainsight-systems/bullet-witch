// Original direction sectors, independent retained channels and draw witness
// values. Public observations only; no game files or presentation devices.
// Core I.1 and MEM.9 exercise copied input and bounded command storage.
#include "game/ui/hud/damage_direction_hud.hpp"

#include <cmath>
#include <stdexcept>
namespace {
void require(bool value, const char* message) {
  if (!value) throw std::runtime_error(message);
}
}  // namespace
void damage_direction_hud_tests() {
  using bw::game::classify_damage_direction;
  using bw::game::DamageDirection;
  using bw::game::DamageDirectionHud;
  constexpr bw::math::Vector3<float> kForward{0, 0, 1};
  require(
      classify_damage_direction({0, 0, -1}, kForward) == DamageDirection::front,
      "Front sector changed");
  require(
      classify_damage_direction({-1, 0, 0}, kForward) == DamageDirection::right,
      "Right sector changed");
  require(
      classify_damage_direction({0, 0, 1}, kForward) == DamageDirection::rear,
      "Rear sector changed");
  require(
      classify_damage_direction({1, 0, 0}, kForward) == DamageDirection::left,
      "Left sector changed");
  require(
      classify_damage_direction({0, 1, 0}, kForward) == DamageDirection::left,
      "Vertical source branch changed");
  require(classify_damage_direction({-1, 0, 0}, {1, 1, 0}) ==
              DamageDirection::front,
          "Camera yaw/pitch feedback changed");
  DamageDirectionHud hud;
  require(hud.request(DamageDirection::front), "Fresh channel rejected");
  hud.advance(.25F);
  hud.advance(.25F);
  require(hud.commands().empty(), "Source entry phases skipped");
  hud.advance(.25F);
  require(hud.commands().size() == 3, "Source channel must emit three sprites");
  require(!hud.request(DamageDirection::front), "Visible channel retriggered");
  hud.advance(0);
  const auto draws = hud.commands();
  require(draws[0].image == 23 && draws[1].image == 22 && draws[2].image == 22,
          "Original draw order changed");
  require(std::abs(draws[1].destination.x - 393.69036865234375F) < .001F,
          "Native quarter-age bar travel differs");
  require(draws[1].color.alpha == 123.F / 255, "Native packed opacity differs");
  const auto transform = draws[1].transform;
  if (!transform) throw std::runtime_error("Native bar transform missing");
  require(std::abs(transform->scale_y - 1.714231014251709F) < .0001F,
          "Native bar stretch differs");
  require(hud.request(DamageDirection::right), "Other direction blocked");
  hud.advance(.25F);
  hud.advance(.25F);
  hud.advance(.25F);
  require(hud.commands().size() == 6, "Independent directions lost");
  for (int i = 0; i < 8; ++i) hud.advance(.25F);
  require(hud.commands().empty() && hud.request(DamageDirection::front),
          "Channel did not retire/rearm");
  hud.reset();
  require(hud.commands().empty(), "Reset retained draws");
  bool rejected = false;
  try {
    hud.request(static_cast<DamageDirection>(4));
  } catch (const std::invalid_argument&) {
    rejected = true;
  }
  require(rejected, "Invalid channel accepted");
}
