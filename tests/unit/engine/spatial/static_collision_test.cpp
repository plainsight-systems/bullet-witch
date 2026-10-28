// Public static registration/query behavior, including front faces, category
// filters, coplanar contact and incidence clearance. Synthetic triangles remain
// borrowed from test-owned bytes; all registrations retire before their domain.
// Both MCP phases: R.2 (#rr-use-ptr), MEM.6
// (guidelines/memory/MEM.6-inject-allocators-behind-one-interface.md).
#include "bw/spatial/static_collision.hpp"

#include <cmath>
#include <limits>

#include "../../../fixtures/camera_fixture.hpp"
namespace {
using bw::scene::CollisionSourceMode;
using bw::scene::SegmentClearance;
constexpr std::uint32_t kCameraMask = 0x2000;
void queries(const std::shared_ptr<bw::memory::Domain>& domain) {
  using camera_fixture::collision;
  using camera_fixture::require;
  auto bytes = collision();
  bytes.at(202) = 0x5a;
  bytes.at(203) = 17;  // Authored high byte, independent of object flags.
  auto definition = bw::formats::CollisionDefinition::parse(bytes, domain);
  bw::scene::StaticCollision query(domain);
  query.add(definition, CollisionSourceMode::character);
  const auto front =
      query.clip_segment({{0, 0, 0}, {0, 0, 2}, .05F, kCameraMask});
  require(front.clipped && front.surface == 17 &&
              std::abs(front.position[2] - .95F) < .000001F,
          "Front face clips with clearance");
  require(
      !query.clip_segment({{0, 0, 2}, {0, 0, 0}, .05F, kCameraMask}).clipped,
      "Back face does not clip");
  require(
      !query.clip_segment({{0, 0, 0}, {0, 0, 0}, .05F, kCameraMask}).clipped,
      "Zero segment unchanged");
  const auto beyond =
      query.clip_segment({{0, 0, 0}, {0, 0, .98F}, .05F, kCameraMask});
  require(beyond.clipped && std::abs(beyond.position[2] - .95F) < .000001F,
          "Extended query catches clearance beyond requested endpoint");
  const auto coplanar =
      query.clip_segment({{-20, 0, 1}, {0, 0, 1}, .05F, kCameraMask});
  require(coplanar.clipped && std::abs(coplanar.position[0] + 5.05F) < .00001F,
          "Coplanar edge contact preserves earliest boundary");
  query.prepare_character_queries();
  const auto edge_face =
      query.capsule_face({-5.1F, 0, .9F}, {-5.1F, .02F, .9F}, .3F, 0);
  require(
      edge_face && std::abs((*edge_face)[0]) < .000001F &&
          std::abs((*edge_face)[1]) < .000001F && (*edge_face)[2] == -1,
      "Capsule edge contact publishes authored face, not separation normal");
  require(!query.capsule_face({0, 0, 0}, {0, .02F, 0}, .3F, 0),
          "Capsule miss has no invented face observation");
  require(
      query.clip_segment({{0, 0, 0}, {0, 0, 2}, .05F, kCameraMask}).position ==
              front.position &&
          query.clip_segment({{-20, 0, 1}, {0, 0, 1}, .05F, kCameraMask})
                  .position == coplanar.position,
      "Prepared camera hierarchy retains front-face and coplanar results");
  const auto prepared =
      query.clip_segment({{0, 0, 0}, {0, 0, 2}, .05F, kCameraMask});
  require(prepared.surface == front.surface,
          "Prepared hierarchy preserves source face metadata");
  const std::array generated{bw::scene::CollisionTriangle{
      .vertices = {{{-10, -10, .5F}, {0, 10, .5F}, {10, -10, .5F}}}}};
  constexpr bw::scene::CollisionGroup selected_group{1};
  query.add_triangles(generated, selected_group);
  query.prepare_character_queries();
  const auto prop =
      query.clip_segment({{0, 0, 0}, {0, 0, 2}, .05F, kCameraMask});
  require(prop.clipped && !prop.surface &&
              std::abs(prop.position[2] - .45F) < .00001F,
          "Closer generated shape does not inherit farther authored surface");
  const auto prepared_count = query.geometry_count();
  query.set_group_enabled(selected_group, false);
  const auto restored =
      query.clip_segment({{0, 0, 0}, {0, 0, 2}, .05F, kCameraMask});
  require(restored.position == front.position &&
              restored.surface == front.surface &&
              query.geometry_count() == prepared_count,
          "Disabling generated group preserves authored terrain and storage");
  query.set_group_enabled(selected_group, true);
  require(
      query.clip_segment({{0, 0, 0}, {0, 0, 2}, .05F, kCameraMask}).position ==
          prop.position,
      "Group reenabling restores queries without rebuilding");
  bool invalid_group = false;
  try {
    query.set_group_enabled(bw::scene::CollisionGroup{64}, false);
  } catch (const std::out_of_range&) {
    invalid_group = true;
  }
  require(invalid_group, "Invalid group must reject before changing mask");
  bool invalid = false;
  try {
    (void)query.clip_segment({{0, 0, 0},
                              {0, 0, 1},
                              std::numeric_limits<float>::quiet_NaN(),
                              kCameraMask});
  } catch (const std::invalid_argument&) {
    invalid = true;
  }
  require(invalid, "Nonfinite clearance rejects");
}
void filters(const std::shared_ptr<bw::memory::Domain>& domain) {
  using camera_fixture::collision;
  using camera_fixture::require;
  for (const auto name :
       {"pc_wall", "ne_wall", "nc_wall", "cc_wall", "NC_wall"}) {
    const auto bytes = collision(name);
    const auto definition =
        bw::formats::CollisionDefinition::parse(bytes, domain);
    bw::scene::StaticCollision query(domain);
    query.add(definition, CollisionSourceMode::character);
    require(query.part_count() == 1 &&
                !query.clip_segment({{0, 0, 0}, {0, 0, 2}, .05F, kCameraMask})
                     .clipped,
            "Camera exclusion is independent of registration");
  }
  for (const auto name : {"wa_water", "de_area", "pp_piece", "hp_piece"}) {
    const auto bytes = collision(name);
    const auto definition =
        bw::formats::CollisionDefinition::parse(bytes, domain);
    bw::scene::StaticCollision query(domain);
    query.add(definition, CollisionSourceMode::character);
    require(query.part_count() == 0,
            "Special categories are not ordinary static triangles");
  }
  const auto bytes = collision("special", 0x24);
  const auto definition =
      bw::formats::CollisionDefinition::parse(bytes, domain);
  bw::scene::StaticCollision query(domain);
  query.add(definition, CollisionSourceMode::environment);
  require(query.part_count() == 0, "Special collision category excluded");
}
// Original player dimensions exercise the shared resolver independently of
// input/animation: standing body is height 1.7, radius .4, center spacing .9.
void character_contacts(const std::shared_ptr<bw::memory::Domain>& domain) {
  using camera_fixture::require;
  const auto floor_bytes = camera_fixture::collision(
      "floor", 0x64, {{{-20, 0, -20}, {0, 0, 20}, {20, 0, -20}}});
  const auto wall_bytes = camera_fixture::collision();
  const auto side_bytes = camera_fixture::collision(
      "side", 0x64, {{{1, -10, -10}, {1, -10, 10}, {1, 10, 0}}});
  const auto ceiling_bytes = camera_fixture::collision(
      "ceiling", 0x64, {{{-20, 2, -20}, {20, 2, -20}, {0, 2, 20}}});
  const auto floor =
      bw::formats::CollisionDefinition::parse(floor_bytes, domain);
  const auto wall = bw::formats::CollisionDefinition::parse(wall_bytes, domain);
  const auto side = bw::formats::CollisionDefinition::parse(side_bytes, domain);
  const auto ceiling =
      bw::formats::CollisionDefinition::parse(ceiling_bytes, domain);
  bw::scene::StaticCollision query(domain);
  query.add(floor, CollisionSourceMode::character);
  query.add(wall, CollisionSourceMode::character);
  query.add(side, CollisionSourceMode::character);
  query.add(ceiling, CollisionSourceMode::character);
  bw::scene::CharacterCollisionRequest request{
      {0, .41F, 0}, {0, .41F, 0}, .4F, .9F, 1.0F / 60, 0, true};
  bool rejected = false;
  try {
    (void)query.move_character(request);
  } catch (const std::logic_error&) {
    rejected = true;
  }
  require(rejected, "Unprepared collision cannot silently allow movement");
  query.prepare_character_queries();
  const auto before = domain->snapshot();
  const auto standing = query.move_character(request);
  require(standing.grounded && std::abs(standing.position[1] - .41F) < .00001F,
          "Standing lower sphere retains floor clearance");
  request.proposed_position[1] += .00001F;
  request.vertical_velocity = .0006F;
  const auto root_rise = query.move_character(request);
  require(
      root_rise.grounded && std::abs(root_rise.position[1] - .41F) < .00001F,
      "Positive grounded root motion retains authored floor support");
  request.proposed_position = request.previous_position;
  request.vertical_velocity = 0;
  request.proposed_position[2] = .9F;
  const auto blocked = query.move_character(request);
  require(blocked.hit_wall && blocked.grounded &&
              std::abs(blocked.position[2] - .59F) < .00001F,
          "Upright body cannot pass through authored wall");
  request.proposed_position = {0, 1, 0};
  request.vertical_velocity = 3;
  request.mode = bw::scene::CharacterContactMode::airborne;
  const auto head_hit = query.move_character(request);
  require(!head_hit.grounded && head_hit.hit_ceiling &&
              std::abs(head_hit.position[1] - .69F) < .00001F,
          "Upper hemisphere stops at ceiling while airborne");
  request.proposed_position = {0, .55F, 0};
  const auto rising = query.move_character(request);
  require(!rising.grounded && rising.position == request.proposed_position,
          "Ascending jump is not pulled onto nearby floor");
  request.proposed_position = {.9F, .41F, .9F};
  request.vertical_velocity = 0;
  request.mode = bw::scene::CharacterContactMode::grounded;
  const auto corner = query.move_character(request);
  require(corner.hit_wall && corner.grounded && corner.position[0] < .61F &&
              corner.position[2] < .61F,
          "Two source corrections resolve both faces of a corner");
  request.previous_position = {0, .7F, 0};
  request.proposed_position = {0, .2F, 0};
  request.vertical_velocity = -3;
  request.mode = bw::scene::CharacterContactMode::airborne;
  request.previously_grounded = false;
  const auto landed = query.move_character(request);
  require(landed.grounded && std::abs(landed.position[1] - .41F) < .00001F,
          "Descending body lands at its radius above the floor");
  request.previous_position = {0, .41F, 0};
  request.proposed_position = {0, .41F, -30};
  request.vertical_velocity = 0;
  request.mode = bw::scene::CharacterContactMode::grounded;
  request.previously_grounded = true;
  require(!query.move_character(request).grounded,
          "Leaving stage geometry loses support without an artificial floor");
  require(domain->snapshot().live_bytes == before.live_bytes,
          "Character queries retain no frame allocations");
}
void character_slopes_and_steps(
    const std::shared_ptr<bw::memory::Domain>& domain) {
  using camera_fixture::require;
  const auto ramp_bytes = camera_fixture::collision(
      "ramp", 0x64, {{{-10, -5, -10}, {0, 0, 10}, {10, 5, -10}}});
  const auto ramp = bw::formats::CollisionDefinition::parse(ramp_bytes, domain);
  bw::scene::StaticCollision ramp_query(domain);
  ramp_query.add(ramp, CollisionSourceMode::character);
  ramp_query.prepare_character_queries();
  bw::scene::CharacterCollisionRequest request{
      {0, .46F, 0}, {.05F, .46F, 0}, .4F, .9F, 1.0F / 60, 0, true};
  auto resolved = ramp_query.move_character(request);
  require(resolved.grounded && resolved.ground_normal[1] > .89F &&
              resolved.position[1] > .46F,
          "Walkable slope supports the finite sphere above its plane");
  const auto floor_bytes = camera_fixture::collision(
      "floor", 0x64, {{{-20, 0, -20}, {0, 0, 20}, {20, 0, -20}}});
  const auto top_bytes = camera_fixture::collision(
      "step_top", 0x64, {{{0, .2F, -10}, {0, .2F, 10}, {10, .2F, 0}}});
  const auto face_bytes = camera_fixture::collision(
      "step_face", 0x64, {{{0, 0, -10}, {0, .2F, 10}, {0, .2F, -10}}});
  const auto floor =
      bw::formats::CollisionDefinition::parse(floor_bytes, domain);
  const auto top = bw::formats::CollisionDefinition::parse(top_bytes, domain);
  const auto face = bw::formats::CollisionDefinition::parse(face_bytes, domain);
  bw::scene::StaticCollision steps(domain);
  steps.add(floor, CollisionSourceMode::character);
  steps.add(top, CollisionSourceMode::character);
  steps.add(face, CollisionSourceMode::character);
  steps.prepare_character_queries();
  request.previous_position = {-1, .41F, 0};
  for (unsigned tick = 0; tick < 60; ++tick) {
    request.proposed_position = request.previous_position;
    request.proposed_position[0] += .04F;
    resolved = steps.move_character(request);
    request.previous_position = resolved.position;
    request.previously_grounded = resolved.grounded;
  }
  require(resolved.grounded && resolved.position[0] > .4F &&
              std::abs(resolved.position[1] - .61F) < .0001F,
          "Source lifted body and lower sphere climb an authored low step");
}
void placed_primitive_contacts(
    const std::shared_ptr<bw::memory::Domain>& domain) {
  using camera_fixture::require;
  bw::scene::StaticCollision query(domain);
  const std::array capsules{
      bw::scene::CollisionCapsule{{0, 0, 0}, {0, 2, 0}, .5F},
      bw::scene::CollisionCapsule{{5, 0, 0}, {5, 0, 0}, 1}};
  constexpr bw::scene::CollisionGroup selected_group{1};
  query.add_capsules(capsules, selected_group);
  query.prepare_character_queries();
  const auto wall = query.move_character(
      {{1, .4F, 0}, {.7F, .4F, 0}, .4F, .9F, 1.0F / 60, 0, false});
  require(wall.hit_wall && std::abs(wall.position[0] - .91F) < .00001F,
          "Placed capsule body uses exact axis distance");
  const auto sphere = query.move_character(
      {{5, 2, 0}, {5, 1.2F, 0}, .4F, .9F, 1.0F / 60, -1, false});
  require(sphere.grounded && std::abs(sphere.position[1] - 1.41F) < .00001F,
          "Placed sphere supplies curved support without tessellation");
  const auto camera = query.clip_segment({{2, 1, 0}, {0, 1, 0}, .05F, 0});
  require(camera.clipped && std::abs(camera.position[0] - .55F) < .00001F,
          "Camera includes analytic placed capsule with incidence clearance");
  const auto sphere_camera =
      query.clip_segment({{5, 3, 0}, {5, 0, 0}, .05F, 0});
  require(sphere_camera.clipped &&
              std::abs(sphere_camera.position[1] - 1.05F) < .00001F,
          "Sphere degeneracy has a finite analytic segment entry");
  const auto escape = query.clip_segment({{0, 1, 0}, {2, 1, 0}, 0, 0});
  require(!escape.clipped,
          "Initial primitive overlap permits outward penetration recovery");
  std::array<bw::scene::CollisionTriangle, 16> triangles{};
  for (std::size_t i = 0; i < triangles.size(); ++i) {
    const auto height = -static_cast<float>(i);
    triangles.at(i).vertices = {
        {{-20, height, -20}, {0, height, 20}, {20, height, -20}}};
  }
  query.add_triangles(triangles);
  query.prepare_character_queries();
  const auto floor = query.move_character(
      {{-2, .41F, 0}, {-2, .41F, 0}, .4F, .9F, 1.0F / 60, 0, true});
  require(floor.grounded && std::abs(floor.position[1] - .41F) < .00001F,
          "Prepared hierarchy chooses nearest authored surface across leaves");
  bw::scene::CharacterCollisionRequest following{
      {-2, 1.4F, 0}, {-2, 1.4F, 0}, .4F, .9F, 1.0F / 60, 0, true};
  require(!query.move_character(following).grounded,
          "Ordinary support reach does not snap across a large drop");
  following.ground_probe_extension = 1;
  const auto followed = query.move_character(following);
  require(followed.grounded && std::abs(followed.position[1] - .41F) < .00001F,
          "Original player policy extends grounded downward following");
  following.vertical_velocity = 1;
  following.mode = bw::scene::CharacterContactMode::airborne;
  require(!query.move_character(following).grounded,
          "Player floor extension does not capture an ascending jump");
  query.set_group_enabled(selected_group, false);
  const auto removed_wall = query.move_character(
      {{1, .4F, 0}, {.7F, .4F, 0}, .4F, .9F, 1.0F / 60, 0, false});
  require(!removed_wall.hit_wall &&
              !query.clip_segment({{2, 1, 0}, {0, 1, 0}, .05F, 0}).clipped,
          "Disabled group leaves neither capsule body nor camera contacts");
  const auto retained_floor = query.move_character(
      {{-2, .41F, 0}, {-2, .41F, 0}, .4F, .9F, 1.0F / 60, 0, true});
  require(retained_floor.grounded,
          "Disabling capsule group preserves default-group floor support");
  bw::scene::StaticCollision filtered(domain);
  auto excluded = capsules.front();
  excluded.object_flags |=
      bw::scene::CharacterCollisionRequest::kDefaultExcludedFlags;
  filtered.add_capsules(std::span(&excluded, 1));
  filtered.prepare_character_queries();
  const auto ignored = filtered.move_character(
      {{1, .4F, 0}, {.7F, .4F, 0}, .4F, .9F, 1.0F / 60, 0, false});
  require(!ignored.hit_wall && ignored.position[0] == .7F,
          "Prepared analytic primitives retain caller exclusion flags");
}
}  // namespace
void static_collision_tests() {
  const auto domain =
      bw::memory::Domain::create(std::make_shared<camera_fixture::Backing>());
  queries(domain);
  filters(domain);
  character_contacts(domain);
  character_slopes_and_steps(domain);
  placed_primitive_contacts(domain);
  {
    const auto bytes = camera_fixture::collision("wall", 0x60);
    const auto definition =
        bw::formats::CollisionDefinition::parse(bytes, domain);
    bw::scene::StaticCollision query(domain);
    for (const auto mode :
         {CollisionSourceMode::character, CollisionSourceMode::combined}) {
      bool failed = false;
      try {
        query.add(definition, mode);
      } catch (const bw::resources::Error&) {
        failed = true;
      }
      camera_fixture::require(failed && query.part_count() == 0,
                              "Unsupported layouts reject atomically");
    }
  }
  camera_fixture::require(domain->snapshot().live_bytes == 0,
                          "Static collision metadata retires");
}
