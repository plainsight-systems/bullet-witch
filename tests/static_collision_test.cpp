// Public static registration/query behavior, including front faces, category
// filters, coplanar contact and incidence clearance. Synthetic triangles remain
// borrowed from test-owned bytes; all registrations retire before their domain.
// Both MCP phases: R.2 (#rr-use-ptr), MEM.6
// (guidelines/memory/MEM.6-inject-allocators-behind-one-interface.md).
#include "bw/scene/spatial/static_collision.hpp"

#include <cmath>
#include <limits>

#include "camera_fixture.hpp"
namespace {
using bw::scene::CollisionSourceMode;
using bw::scene::SegmentClearance;
constexpr std::uint32_t kCameraMask = 0x2000;
void queries(const std::shared_ptr<bw::memory::Domain>& domain) {
  using camera_fixture::collision;
  using camera_fixture::require;
  auto bytes = collision();
  auto definition = bw::formats::CollisionDefinition::parse(bytes, domain);
  bw::scene::StaticCollision query(domain);
  query.add(definition, CollisionSourceMode::character);
  const auto front =
      query.clip_segment({{0, 0, 0}, {0, 0, 2}, .05F, kCameraMask});
  require(front.clipped && std::abs(front.position[2] - .95F) < .000001F,
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
}  // namespace
void static_collision_tests() {
  const auto domain =
      bw::memory::Domain::create(std::make_shared<camera_fixture::Backing>());
  queries(domain);
  filters(domain);
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
