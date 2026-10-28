// Copied geometry contracts: separate reference/launch locations, source
// movement spread, enemy bone direction, deterministic shared RNG ordering.
#include "game/combat/shot_geometry.hpp"

#include <cmath>
#include <limits>

#include "../../../fixtures/camera_fixture.hpp"
#include "game/session/compatibility/source_random.hpp"

namespace {
using camera_fixture::require;
void near(float actual, float expected) {
  require(std::abs(actual - expected) < 0.00001F,
          "Shot geometry numeric contract mismatch");
}
template <class Function>
void rejects(Function operation) {
  bool rejected = false;
  try {
    operation();
  } catch (const std::invalid_argument&) {
    rejected = true;
  }
  require(rejected, "Invalid shot geometry admitted");
}
}  // namespace
void shot_geometry_tests() {
  namespace game = bw::game;
  namespace math = bw::math;
  const auto segment =
      game::player_shot_segment({{0, 2, -5}, {0, 0, 1}, {0, 0, 0}, 100});
  near(segment.start[2], 0.1F);
  near(segment.start[1], 2);
  near(segment.end[2], 95);

  const auto bone =
      math::Affine3<float>{{{0, 0, -1}, {0, 1, 0}, {1, 0, 0}, {10, 3, 20}}};
  const auto enemy = game::weapon_forward_shot_geometry(bone);
  require(enemy.origin == bone[3] && enemy.scenery_reference == bone[3] &&
              enemy.target == math::Vector3<float>{11, 3, 20},
          "Source Shooter fallback adds the returned locator forward axis");

  game::PlayerShotPlacement placement{{2, 2, -1}, {0, 2, 100}, {0, 0, 0},
                                      {0, 0, 1},  1,           false};
  const auto shot = game::player_shot_geometry(placement);
  near(shot.origin[0], .9F);
  near(shot.origin[1], .9F);
  near(shot.origin[2], .5F);
  require(shot.scenery_reference == placement.muzzle,
          "Launch correction incorrectly changed scenery reference");
  placement.aim_hit = true;
  require(game::player_shot_geometry(placement).scenery_reference ==
              placement.target,
          "Camera hit did not become scenery reference");
  placement = {{0, 1, 2}, {0, 1, 20}, {0, 0, 0}, {0, 0, 1}, 1, false};
  near(game::player_shot_geometry(placement).origin[2], 0);

  // Bone SRT may carry authored scale; source forward-depth placement uses
  // the raw axis twice and must neither reject it nor normalize it.
  placement.weapon_forward = {0, 0, .5F};
  near(game::player_shot_geometry(placement).origin[2], 1.5F);

  const game::WeaponSpread spread{.1F, .5F, 2, 4};
  near(game::advance_weapon_spread(spread, .1F, 1, true), .3F);
  near(game::advance_weapon_spread(spread, .5F, 1, false), .4F);
  near(game::advance_weapon_spread(spread, .5F, 100, false), .1F);
  near(game::advance_weapon_spread(spread, .1F, 100, true), .5F);
  near(game::advance_weapon_spread(spread, .3F, 0, true), .3F);
  rejects([&] { game::advance_weapon_spread(spread, 0, 1, true); });

  game::SourceRandom random(1), reference(1);
  const game::ShotGeometry forward{{}, {0, 0, 10}, {}};
  const auto straight = game::sample_shot_direction(forward, 0, random);
  near(straight[0], 0);
  near(straight[1], 0);
  near(straight[2], 1);
  reference.unit();
  reference.unit();
  require(random.draws() == 2 && random.state() == reference.state(),
          "Zero-spread shot changed session RNG order");
  const auto before = random.state();
  rejects([&] { game::sample_shot_direction({{}, {}, {}}, .1F, random); });
  require(random.state() == before && random.draws() == 2,
          "Rejected geometry consumed RNG");
  for (unsigned index = 0; index < 16; ++index) {
    const auto direction = game::sample_shot_direction(forward, .2F, random);
    near(math::length(direction), 1);
    require(direction[2] >= std::cos(.2F), "Shot escaped authored cone");
  }
  const auto transform = game::shot_forward_transform({1, 2, 3}, {0, 0, 1});
  require(math::translation(transform) == math::Vector3<float>{1, 2, 3} &&
              transform[2] == math::Vector<float, 4>{0, 0, 1, 0},
          "Projectile forward basis or position changed");
  const auto upward = game::shot_forward_transform({}, {0, 1, 0});
  const auto downward = game::shot_forward_transform({}, {0, -1, 0});
  require(upward[0] == math::Vector<float, 4>{1, 0, 0, 0} &&
              upward[1] == math::Vector<float, 4>{0, 0, -1, 0} &&
              downward[1] == math::Vector<float, 4>{0, 0, 1, 0},
          "Vertical impact basis lost original fallback");
  const auto down =
      game::sample_shot_direction({{}, {0, -1, 0}, {}}, 0, random);
  near(down[1], -1);
  rejects([&] { game::shot_forward_transform({}, {}); });
}
