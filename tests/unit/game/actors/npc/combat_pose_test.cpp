// Source completed-pose contracts: weighted attachment and fallback are
// distinct, and aim height comes from model geometry rather than a collider.
#include "game/actors/animation/combat_pose.hpp"

#include "../../../../fixtures/camera_fixture.hpp"
void combat_pose_tests() {
  using camera_fixture::require;
  using camera_fixture::scalar;
  using camera_fixture::word;
  const auto domain =
      bw::memory::Domain::create(std::make_shared<camera_fixture::Backing>());
  // Slot7 is deliberately beyond the two-bone skeleton: attachment identities
  // must not accidentally become raw bone indices again.
  bw::resources::Bytes bytes(104);
  word(bytes, 0, 0x4250435f);
  word(bytes, 4, 1);
  word(bytes, 8, 8);
  word(bytes, 12 + 7 * 4, 44);
  word(bytes, 44, 3);
  word(bytes, 48, 2);
  scalar(bytes, 52, 2);
  scalar(bytes, 64, 2);
  scalar(bytes, 72, 1);
  scalar(bytes, 76, 2);
  scalar(bytes, 80, 1);
  word(bytes, 88, 0);
  word(bytes, 92, 1);
  scalar(bytes, 96, .25F);
  scalar(bytes, 100, .75F);
  auto controls = bw::formats::ControlPointDefinition::parse(bytes, 2, domain);
  const bw::math::Matrix4<float> first{
      {{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {4, 5, 6, 1}}};
  auto second = first;
  second[3][0] = 8;
  const std::array skin{first, second};
  const bw::math::Affine3<float> model{
      {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}, {21, 22, 23}}};
  const auto actual =
      bw::game::original_weapon_attachment_pose(&controls, skin, model, 7);
  require(actual[0] == bw::math::Vector3<float>{-1, 0, 0} &&
              actual[1] == bw::math::Vector3<float>{0, 1, 0} &&
              actual[2] == bw::math::Vector3<float>{0, 0, -1} &&
              actual[3] == bw::math::Vector3<float>{9, 5, 6},
          "Weapon locator blends bind-space points through completed world "
          "skin once");
  require(bw::game::original_weapon_attachment_pose(&controls, skin, model,
                                                    -1) == model &&
              bw::game::original_weapon_attachment_pose(&controls, skin, model,
                                                        0) == model &&
              bw::game::original_weapon_attachment_pose(&controls, skin, model,
                                                        8) == model &&
              bw::game::original_weapon_attachment_pose(nullptr, skin, model,
                                                        7) == model,
          "Only unavailable attachment returns unmodified model frame");
  // The format allows nonunit weight sums. Local point accumulation must not
  // scale model translation or discard inherited model scale.
  scalar(bytes, 96, .5F);
  scalar(bytes, 100, 1.F);
  const auto scaled_controls =
      bw::formats::ControlPointDefinition::parse(bytes, 2, domain);
  const bw::math::Affine3<float> scaled_model{
      {{2, 0, 0}, {0, 3, 0}, {0, 0, 4}, {21, 22, 23}}};
  const auto world_skin = bw::math::homogeneous(scaled_model);
  const std::array scaled_skin{world_skin, world_skin};
  const auto scaled = bw::game::original_weapon_attachment_pose(
      &scaled_controls, scaled_skin, scaled_model, 7);
  require(scaled[3] == bw::math::Vector3<float>{27, 22, 23} &&
              scaled[0] == bw::math::Vector3<float>{-2, 0, 0} &&
              scaled[1] == bw::math::Vector3<float>{0, 3, 0} &&
              scaled[2] == bw::math::Vector3<float>{0, 0, -4},
          "Local attachment weights preserve model scale and do not multiply "
          "model translation");
  word(bytes, 48, 0);
  auto unweighted =
      bw::formats::ControlPointDefinition::parse(bytes, 2, domain);
  require(bw::game::original_weapon_attachment_pose(&unweighted, skin, model,
                                                    7)[3] ==
              bw::math::Vector3<float>{23, 22, 23},
          "Unweighted attachment remains in model space");
  require(bw::game::original_actor_aim_point({1, 2, 3}, 4, 10)[1] == 3,
          "Valid completed eye midpoint overrides model radius");
  require(bw::game::original_actor_aim_point({1, 2, 3}, 2, 10)[1] == 7,
          "Low eye frame uses original model-resource radius");
  require(
      bw::game::original_actor_aim_point({1, 2, 3}, 2, std::nullopt)[1] == 2,
      "Missing model preserves original base position");
  using bw::game::original_npc_movement_category;
  require(original_npc_movement_category({2, 4, 2}) == 0 &&
              original_npc_movement_category({2, 3, 2}) == 1 &&
              original_npc_movement_category({2, 2.5F, 2}) == 2,
          "NPC eye height uses inclusive half/quarter model-radius thresholds");
  require(original_npc_movement_category({2, 4, 2, true}) == 1 &&
              original_npc_movement_category({2, 4, 2, false, 757}) == 1 &&
              original_npc_movement_category({2, 2, 2, false, 756}) == 0,
          "Explicit source squat and damage states override geometry");
}
