// Unit test: public contracts of bw/rendering/scene/cpu_order.hpp. Records
// are built in memory; no device.
#include "bw/rendering/scene/cpu_order.hpp"

#include "bw/math/matrix/matrix4.hpp"
#include "bw/math/vector/vector3.hpp"
#include "bw/rendering/scene/cluster_records.hpp"
#include "bw/rendering/scene/frame_views.hpp"
#include "bw/rendering/scene/instance.hpp"
#include "bw/testing/harness.hpp"

#include <array>
#include <cstdint>
#include <numbers>
#include <vector>

namespace {

namespace s = bw::scene;
using bw::math::Matrix4f;
using bw::math::Vector3f;
using bw::testing::require;

std::vector<std::uint32_t> instances(const std::vector<s::CpuDraw>& draws) {
  std::vector<std::uint32_t> out;
  out.reserve(draws.size());
  for (const s::CpuDraw& d : draws) {
    out.push_back(d.instance);
  }
  return out;
}

// (cpu_order.hpp, contracts 1 to 3): a dispatched, a dead and an outside
// record are absent, the last outside the far plane alone; the blended
// order is priority ascending, distance descending, index ascending; the
// decal order is priority ascending, index ascending.
void key_and_frustum() {
  s::SceneViewDescription d{};
  d.view_to_clip = Matrix4f::perspective(std::numbers::pi_v<float> / 2.0F, 1.0F, 1.0F, 100.0F);
  d.world_to_clip = Matrix4f::look_at(Vector3f{0.0F, 0.0F, 0.0F}, Vector3f{0.0F, 0.0F, -1.0F},
                                      Vector3f{0.0F, 1.0F, 0.0F}) *
                    d.view_to_clip;
  const s::SceneView view = s::make_scene_view(d);

  struct Row {
    bool live;
    std::uint32_t pipeline;
    s::InstanceKind kind;
    std::int32_t priority;
    float depth;
  };
  const std::array rows{
      Row{true, 0, s::InstanceKind::Mesh, 0, 10.0F},           // 0 dispatched
      Row{true, s::kNone, s::InstanceKind::Mesh, 0, 10.0F},    // 1
      Row{true, s::kNone, s::InstanceKind::Mesh, 0, 20.0F},    // 2
      Row{true, s::kNone, s::InstanceKind::Emitter, -1, 5.0F}, // 3
      Row{true, s::kNone, s::InstanceKind::Mesh, 0, 20.0F},    // 4
      Row{true, s::kNone, s::InstanceKind::Mesh, 0, 102.0F},   // 5 past the far plane
      Row{true, s::kNone, s::InstanceKind::Decal, 1, 50.0F},   // 6
      Row{true, s::kNone, s::InstanceKind::Decal, 0, 60.0F},   // 7
      Row{true, s::kNone, s::InstanceKind::Decal, 1, 10.0F},   // 8
      Row{false, s::kNone, s::InstanceKind::Mesh, 0, 10.0F},   // 9 dead
  };
  std::vector<s::InstanceRecord> records(rows.size());
  for (std::size_t i = 0; i < rows.size(); ++i) {
    s::InstanceRecord& r = records[i];
    r.pipeline = rows[i].pipeline;
    r.kind_and_flags =
        static_cast<std::uint32_t>(rows[i].kind) | (rows[i].live ? s::kInstanceLive : 0U);
    r.sort_priority = rows[i].priority;
    r.bounds_center = {0.0F, 0.0F, -rows[i].depth};
    r.bounds_extent = {1.0F, 1.0F, 1.0F};
  }
  s::CpuOrder order;
  s::order_cpu_draws(view, records, std::span<const s::LevelRecord>{}, order);
  require(instances(order.blended) == std::vector<std::uint32_t>{3, 2, 4, 1},
          "blended: priority, then far to near, then index");
  require(instances(order.decals) == std::vector<std::uint32_t>{7, 6, 8},
          "decals: priority, then index");
}

using Scenario = bw::testing::Scenario<void>;
constexpr std::array kScenarios{Scenario{"key_and_frustum", &key_and_frustum}};

} // namespace

int main(int argc, char** argv) { return bw::testing::run_scenario(argc, argv, kScenarios); }
