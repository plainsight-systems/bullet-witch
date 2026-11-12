// Unit test: public contracts of bw/rendering/scene/instance_set.hpp and
// instance.hpp's world_box. The set is plain memory; no device.
#include "bw/rendering/scene/instance_set.hpp"

#include "bw/math/matrix/matrix4.hpp"
#include "bw/math/vector/vector3.hpp"
#include "bw/rendering/scene/instance.hpp"
#include "bw/rendering/scene/view.hpp"
#include "bw/rendering/shaders/parameter_block.hpp"
#include "bw/testing/harness.hpp"

#include <array>
#include <cstdint>
#include <vector>

namespace {

namespace s = bw::scene;
using bw::math::Matrix4f;
using bw::math::Vector3f;
using bw::testing::require;

bool same(const bw::shaders::Float4x4& a, const bw::shaders::Float4x4& b) {
  for (std::size_t r = 0; r < 4U; ++r) {
    if (a.rows[r].x != b.rows[r].x || a.rows[r].y != b.rows[r].y || a.rows[r].z != b.rows[r].z ||
        a.rows[r].w != b.rows[r].w) {
      return false;
    }
  }
  return true;
}
bool same(const bw::shaders::Float3& a, float x, float y, float z) {
  return a.x == x && a.y == y && a.z == z;
}
std::vector<std::uint32_t> changed(const s::InstanceSet& set) {
  return {set.changed().begin(), set.changed().end()};
}

// (instance_set.hpp, contracts 2 to 6; instance.hpp, world_box): entry
// gives the box of the eight transformed corners grown by the pad, and
// previous equal to current; an empty box stays empty; a full set refuses;
// a removed id is refused while its index serves a new id; a move leaves
// previous alone until begin_frame copies it; only changed records are
// listed, ascending, each once.
void ids_and_previous_transform() {
  // A quarter turn about z, then a move by (10, 20, 30): (x, y, z) goes to
  // (10 - y, 20 + x, 30 + z).
  const Matrix4f turned(Matrix4f::Rows{{{0.0F, 1.0F, 0.0F, 0.0F},
                                        {-1.0F, 0.0F, 0.0F, 0.0F},
                                        {0.0F, 0.0F, 1.0F, 0.0F},
                                        {10.0F, 20.0F, 30.0F, 1.0F}}});
  const Matrix4f moved = Matrix4f::translation(Vector3f{1.0F, 2.0F, 3.0F});
  s::InstanceDescription description{};
  description.geometry.bounds = {Vector3f{-1.0F, -2.0F, 0.0F}, Vector3f{1.0F, 2.0F, 3.0F}};
  description.max_displacement = 0.5F;
  description.transform = turned;
  description.flags = s::kInstanceCastsShadow;

  const s::Box empty =
      s::world_box({Vector3f{1.0F, 0.0F, 0.0F}, Vector3f{0.0F, 0.0F, 0.0F}}, turned, 1.0F);
  require(empty.min.x > empty.max.x || empty.min.y > empty.max.y || empty.min.z > empty.max.z,
          "an empty local box gives an empty box");

  s::InstanceSet set(2);
  std::array<s::InstanceId, 3> ids{};
  require(set.enter(description, ids[0]) == s::InstanceError::None &&
              set.enter(description, ids[1]) == s::InstanceError::None && ids[0] != ids[1],
          "two entries, two ids");
  require(set.enter(description, ids[2]) == s::InstanceError::SetFull, "a full set refuses");
  const s::InstanceRecord& entered = set.records()[ids[0].index];
  require(same(entered.bounds_center, 10.0F, 20.0F, 31.5F) &&
              same(entered.bounds_extent, 2.5F, 1.5F, 2.0F),
          "the world box is the turned corners' box, padded");
  require(same(entered.previous_object_to_world, entered.object_to_world) &&
              same(entered.previous_bounds_center, 10.0F, 20.0F, 31.5F) &&
              same(entered.previous_bounds_extent, 2.5F, 1.5F, 2.0F),
          "an entering instance's previous is its current");
  require(changed(set) == std::vector<std::uint32_t>{0, 1}, "both entries changed");

  set.clear_changed();
  require(set.remove(ids[0]) == s::InstanceError::None &&
              set.enter(description, ids[2]) == s::InstanceError::None &&
              ids[2].index == ids[0].index,
          "a removed index serves the next entry");
  require(changed(set) == std::vector<std::uint32_t>{ids[0].index},
          "a removed and re-entered index is listed once");
  require(set.move(ids[0], moved) == s::InstanceError::IdStale &&
              set.set_flags(ids[0], 0) == s::InstanceError::IdStale &&
              set.remove(ids[0]) == s::InstanceError::IdStale &&
              set.move(s::InstanceId{}, moved) == s::InstanceError::IdStale,
          "the old id and the invalid id are stale");

  set.clear_changed();
  require(set.move(ids[1], moved) == s::InstanceError::None, "a live id moves");
  const s::InstanceRecord& record = set.records()[ids[1].index];
  require(same(record.object_to_world, s::shader_matrix(moved)) &&
              same(record.previous_object_to_world, s::shader_matrix(turned)),
          "a move leaves previous as it was");
  const s::Box moved_box = s::world_box(description.geometry.bounds, moved, 0.5F);
  require(same(record.bounds_center, (moved_box.min.x + moved_box.max.x) * 0.5F,
               (moved_box.min.y + moved_box.max.y) * 0.5F,
               (moved_box.min.z + moved_box.max.z) * 0.5F) &&
              same(record.bounds_extent, (moved_box.max.x - moved_box.min.x) * 0.5F,
                   (moved_box.max.y - moved_box.min.y) * 0.5F,
                   (moved_box.max.z - moved_box.min.z) * 0.5F),
          "a move gives the entered bounds' box under the new transform");
  require(changed(set) == std::vector<std::uint32_t>{ids[1].index}, "only the moved record");
  set.clear_changed();
  set.begin_frame();
  require(same(record.previous_object_to_world, s::shader_matrix(moved)),
          "frame start copies current to previous");
  require(changed(set) == std::vector<std::uint32_t>{ids[1].index},
          "frame start lists the rolled record and no other");
  set.clear_changed();
  set.begin_frame();
  require(set.changed().empty(), "a frame with no move lists nothing");
}

using Scenario = bw::testing::Scenario<void>;
constexpr std::array kScenarios{
    Scenario{"ids_and_previous_transform", &ids_and_previous_transform}};

} // namespace

int main(int argc, char** argv) { return bw::testing::run_scenario(argc, argv, kScenarios); }
