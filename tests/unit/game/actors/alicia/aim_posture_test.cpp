// In-process source target/lifetime witnesses, not hardware acceptance.
// E.4 exercises rejection and MEM.9 fixed-state update without asset loading.
#include "game/actors/animation/aim_posture.hpp"

#include <cmath>
#include <stdexcept>

#include "game/actors/alicia/actions/raised_weapon_state.hpp"
namespace {
void require(bool value, const char* message) {
  if (!value) throw std::runtime_error(message);
}
}  // namespace
void aim_posture_tests() {
  bw::game::RaisedWeaponLifetime raised;
  require(raised.advance(0, false, true, 0) && raised.remaining_seconds() == 6,
          "Ordinary accepted aiming refreshes six-second source lifetime");
  require(raised.advance(10, true, false, 0) && raised.remaining_seconds() == 6,
          "An active upper clip holds the raised lifetime");
  require(!raised.advance(6, false, false, 0),
          "Absent upper permits lifetime decay");
  require(
      raised.advance(0, false, true, 3) && raised.remaining_seconds() == .75F,
      "Heavy weapon uses its shorter source raised lifetime");
  bw::game::AimPostureBlend fade;
  require(fade.advance(.125F, true) == .5F && fade.advance(.125F, true) == 1 &&
              fade.advance(.125F, false) == .5F &&
              fade.advance(0, false) == .5F,
          "Posture quarter-second fade preserves a zero-time preview");
  constexpr bw::math::Matrix4<float> identity{
      {{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1}}};
  for (const float side : {-1.F, 1.F}) {
    const auto step =
        bw::game::advance_shooter_anchor(identity, {side, 0, 0}, 1.F / 60);
    require(
        !step.completed && std::abs(step.turn_rate - 2) < 1e-6F &&
            std::abs(step.anchor[2][0] - side * .0523359589278698F) < 1e-6F &&
            std::abs(step.anchor[2][2] - .9986295700073242F) < 1e-6F,
        "Original Shooter witness turns opposite headings but emits positive "
        "rate for both");
  }
  const auto back =
      bw::game::advance_shooter_anchor(identity, {0, 0, -1}, 1.F / 60);
  require(back.anchor[2][0] < 0 && std::abs(back.turn_rate - 2) < 1e-6F,
          "Original antipodal Shooter witness selects negative Y rotation");
  const auto up = bw::game::advance_shooter_anchor(
      identity, {0, std::sin(.4F), std::cos(.4F)}, 1.F / 60);
  require(std::abs(up.anchor[2][1] - .0523359589278698F) < 1e-6F &&
              up.turn_rate == 0,
          "Shooter pitch has independent bounded movement and no yaw rate");
  const auto compound_anchor = bw::math::orthonormal_basis(
      bw::math::Vector3<float>{std::sin(.5F) * std::cos(.2F), std::sin(.2F),
                               std::cos(.5F) * std::cos(.2F)},
      bw::math::Vector3<float>{0, 1, 0});
  const auto compound =
      bw::game::advance_shooter_anchor(compound_anchor, {1, 0, 0}, 1.F / 60);
  require(std::abs(compound.anchor[2][0] - .5189894437789917F) < 1e-6F &&
              std::abs(compound.anchor[2][1] - .14710433781147003F) < 1e-6F &&
              std::abs(compound.anchor[2][2] - .8420275449752808F) < 1e-6F,
          "Original compound yaw/pitch witness preserves quaternion order");
  const auto settled =
      bw::game::advance_shooter_anchor(identity, {0, 0, 1}, 1.F / 60);
  require(
      settled.completed && settled.turn_rate == 0 && settled.anchor == identity,
      "Settled Shooter request completes without perturbing orientation");
  bw::game::AimPostureTarget target{
      identity, identity, {0, 0, 1}, {65, -90, 90, -45}};
  auto direction = bw::game::aim_posture_target(target);
  require(direction == bw::math::Vector3<float>{0, 0, 1},
          "Identity source target retains forward");
  const float pitch = 80.0F * 3.1415927410125732F / 180;
  target.anchor = bw::math::orthonormal_basis(
      bw::math::Vector3<float>{0, std::sin(pitch), std::cos(pitch)},
      bw::math::Vector3<float>{0, 1, 0});
  direction = bw::game::aim_posture_target(target);
  require(std::abs(direction[1] + std::sin(65.0F * 3.1415927410125732F / 180)) <
              1e-6F,
          "Standing upward limit clamps before modifier Y convention");
  target.crouched = true;
  direction = bw::game::aim_posture_target(target);
  require(std::abs(direction[1] + std::sin(pitch)) < 1e-6F,
          "Crouched ninety-degree upper setting bypasses clamp branch");
  require(bw::game::raised_turn_input(.5F, 25, false) == .5F &&
              bw::game::raised_turn_input(.5F, 25, true) == -.5F,
          "Turn clip rate uses configured look input rather than camera delta");
  const auto facing = bw::game::raised_facing_step(
      {{0, 0, 1}, {1, 0, 0}, {0, 0, 1}, identity, .01F, false});
  require(facing[0] > 0 && facing[0] < 1 &&
              std::abs(bw::math::length(facing) - 1) < 1e-6F,
          "Facing advances bounded source quaternion rotation");
  // Actual installed shoot_turn_ri_l after raised right-stick input. The
  // preceding root interval leaves the model forward slightly below unit.
  auto reference = identity;
  reference[0][0] = -1;
  reference[2][2] = -1;
  const auto authored_facing =
      bw::game::raised_facing_step({{.920855343F, 5.47249499e-7F, .388461918F},
                                    {.337423891F, 0, .941352784F},
                                    {.337423891F, 0, .941352784F},
                                    reference,
                                    1.0F / 60,
                                    true});
  const bw::math::Vector3<float> authored_current{.920855343F, 5.47249499e-7F,
                                                  .388461918F};
  const float turn_cosine = bw::math::dot(authored_current, authored_facing) /
                            bw::math::length(authored_current);
  require(std::abs(turn_cosine - .965925826F) < 1e-6F &&
              std::abs(bw::math::length(authored_facing) - 1) < 1e-6F,
          "Authored root scale survives turn decisions before final basis "
          "normalization");
  // Normalizing either raw operand before the dot test would choose the
  // bounded-turn branch instead of the source snap branch in these cases.
  const bw::math::Vector3<float> near_forward{.1F, 0, .994987437F};
  const auto scaled_current = bw::game::raised_facing_step(
      {{0, 0, 1.1F}, near_forward, {0, 0, 1}, identity, .001F, false});
  reference = identity;
  reference[0][0] = reference[1][1] = reference[2][2] = 1.1F;
  const auto scaled_target = bw::game::raised_facing_step(
      {{0, 0, 1}, near_forward, {0, 0, 1}, reference, .001F, false});
  require(std::abs(scaled_current[0] - .1F) < 1e-6F &&
              std::abs(scaled_target[0] - .1F) < 1e-6F,
          "Source snap comparison preserves current and reference scale");
  bool rejected = false;
  try {
    (void)bw::game::raised_facing_step(
        {{0, 0, 0}, near_forward, {0, 0, 1}, identity, .001F, false});
  } catch (const std::invalid_argument&) {
    rejected = true;
  }
  require(rejected, "A zero current direction has no final facing basis");
}
