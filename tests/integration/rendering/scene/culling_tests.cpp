// Integration test: bw/rendering/scene/culling.hpp on this Mac's Metal
// device with the build's culling and depth hierarchy artifacts. Synthetic
// instances and clusters are entered directly (instance_set.hpp) with their
// cluster and level tables uploaded as buffers; the frame's depth is a
// texture made with chosen depths, row 0 at the top of the screen, which
// both hierarchy builds read, and no geometry is drawn.
//
// Argument after the scenario name: the Metal artifact directory.
#include "bw/math/matrix/matrix4.hpp"
#include "bw/math/vector/vector3.hpp"
#include "bw/rendering/graph/access.hpp"
#include "bw/rendering/graph/builder.hpp"
#include "bw/rendering/graph/compiled_frame.hpp"
#include "bw/rendering/graph/declaration.hpp"
#include "bw/rendering/graph/executor.hpp"
#include "bw/rendering/graph/schedule_policy.hpp"
#include "bw/rendering/rhi/access.hpp"
#include "bw/rendering/rhi/command_list.hpp"
#include "bw/rendering/rhi/device.hpp"
#include "bw/rendering/rhi/resources.hpp"
#include "bw/rendering/rhi/synchronization.hpp"
#include "bw/rendering/scene/cluster_records.hpp"
#include "bw/rendering/scene/cpu_order.hpp"
#include "bw/rendering/scene/culling.hpp"
#include "bw/rendering/scene/depth_hierarchy.hpp"
#include "bw/rendering/scene/draw_records.hpp"
#include "bw/rendering/scene/frame_views.hpp"
#include "bw/rendering/scene/instance.hpp"
#include "bw/rendering/scene/instance_set.hpp"
#include "bw/rendering/scene/scene_geometry.hpp"
#include "bw/rendering/scene/view.hpp"
#include "bw/testing/harness.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

namespace fs = std::filesystem;
namespace graph = bw::graph;
namespace math = bw::math;
namespace rhi = bw::rhi;
namespace scene = bw::scene;
using bw::testing::require;

struct Arguments {
  fs::path shaders{};
};

template <class T, class E> T take(rhi::Result<T, E> result, std::string_view what) {
  if (!result.ok()) {
    throw std::runtime_error(std::string(what) + " failed: " + std::string(result.detail()));
  }
  return std::move(result.value());
}

std::unique_ptr<rhi::Device> metal_device() {
  rhi::DeviceDescription description{};
  description.backend = rhi::BackendKind::Metal;
  description.miss_policy = rhi::PipelineMissPolicy::CreateOnMiss;
  return take(rhi::Device::create(description), "Device::create");
}

constexpr rhi::Extent2 kViewport{64, 64};
constexpr std::uint32_t kClustersPerInstance = 3;

// One unit box per cluster, side by side along x in the mesh's space: an
// instance spans x from -1.5 to 1.5.
std::vector<scene::ClusterRecord> cluster_table() {
  std::vector<scene::ClusterRecord> clusters;
  for (std::uint32_t c = 0; c < kClustersPerInstance; ++c) {
    scene::ClusterRecord cluster{};
    cluster.first_index = c * 30U;
    cluster.index_count = 30U + c;
    cluster.center = bw::shaders::Float3{static_cast<float>(c) - 1.0F, 0.0F, 0.0F};
    cluster.extent = bw::shaders::Float3{0.5F, 0.5F, 0.5F};
    clusters.push_back(cluster);
  }
  return clusters;
}

template <class T>
std::pair<rhi::Ref<rhi::Buffer>, rhi::Ref<rhi::ShaderResourceView>>
table_buffer(rhi::Device& device, const std::vector<T>& values, std::string_view name) {
  rhi::BufferDescription description{};
  description.size = values.size() * sizeof(T);
  description.usage = rhi::BufferUsage::ShaderResource;
  description.initial_access = rhi::kShaderResourceMask;
  description.debug_name = name;
  rhi::Ref<rhi::Buffer> buffer =
      take(device.create_buffer(description, std::as_bytes(std::span<const T>(values))), name);
  rhi::Ref<rhi::ShaderResourceView> view =
      take(device.create_shader_resource_view(buffer, rhi::BufferViewDescription{}), name);
  return {std::move(buffer), std::move(view)};
}

struct Placement {
  math::Vector3f offset{math::zero_init};
  std::uint32_t pipeline{};
};

struct Scene {
  explicit Scene(std::uint32_t capacity) : set(capacity) {}
  scene::InstanceSet set;
  scene::InstanceBuffer instance_buffer{};
  scene::GeometryBuffers geometry{};
  std::vector<scene::ClusterRecord> clusters{cluster_table()};
  scene::FrameViews views{};
  scene::Culling culling{};
  scene::DepthHierarchy hierarchy{};
};

void build(rhi::Device& device, const Arguments& args, const std::vector<Placement>& placements,
           Scene& out) {
  const std::vector<scene::LevelRecord> levels{
      scene::LevelRecord{0, kClustersPerInstance, 1.0F, 0}};
  auto [clusters, clusters_view] = table_buffer(device, out.clusters, "clusters");
  auto [level_buffer, levels_view] = table_buffer(device, levels, "levels");
  out.geometry.clusters = std::move(clusters);
  out.geometry.clusters_view = std::move(clusters_view);
  out.geometry.levels = std::move(level_buffer);
  out.geometry.levels_view = std::move(levels_view);
  for (const Placement& placement : placements) {
    scene::InstanceDescription description{};
    description.geometry.first_level = 0;
    description.geometry.level_count = 1;
    description.geometry.cluster_count = kClustersPerInstance;
    description.geometry.bounds =
        scene::Box{math::Vector3f{-1.5F, -0.5F, -0.5F}, math::Vector3f{1.5F, 0.5F, 0.5F}};
    description.pipeline = placement.pipeline;
    description.transform = math::Matrix4f::translation(placement.offset);
    scene::InstanceId id{};
    require(out.set.enter(description, id) == scene::InstanceError::None, "enter");
  }
  require(scene::create_culling(device, args.shaders, out.culling).ok(), "create_culling");
  require(scene::create_depth_hierarchy(device, args.shaders, out.hierarchy).ok(),
          "create_depth_hierarchy");
}

// A view from the origin down -z, 90 degrees, near 0.5, far 100.
scene::SceneView main_view(math::Vector3f eye) {
  const scene::PerspectiveView perspective{eye,
                                           eye + math::Vector3f{0.0F, 0.0F, -1.0F},
                                           math::Vector3f{0.0F, 1.0F, 0.0F},
                                           1.5707964F,
                                           0.5F,
                                           100.0F};
  scene::SceneViewDescription description{};
  description.world_to_clip = scene::world_to_clip(perspective, 1.0F);
  description.view_to_clip = math::Matrix4f::perspective(1.5707964F, 1.0F, 0.5F, 100.0F);
  description.eye = eye;
  description.viewport_width = static_cast<float>(kViewport.width);
  description.viewport_height = static_cast<float>(kViewport.height);
  description.purpose = scene::ViewPurpose::Main;
  return scene::make_scene_view(description);
}

// The frame's record and count buffers as read back.
struct Lists {
  scene::DrawListLayout layout{};
  std::vector<std::byte> records{};
  std::vector<std::byte> counts{};

  [[nodiscard]] std::vector<scene::DrawRecord> list(std::uint32_t view, std::uint32_t pipeline,
                                                    scene::DrawPhase phase) const {
    const scene::DrawList place = layout.list(view, pipeline, phase);
    std::uint32_t count = 0;
    std::memcpy(&count, counts.data() + place.count_offset, sizeof(count));
    require(count <= place.capacity, "a count within its list's capacity");
    std::vector<scene::DrawRecord> out(count);
    std::memcpy(out.data(), records.data() + place.record_offset,
                count * sizeof(scene::DrawRecord));
    return out;
  }
};

// One frame: phase one, a depth texture of `depth` (kViewport pixels by
// rows), the first build, phase two, the second build; the lists read back.
Lists run_frame(rhi::Device& device, Scene& s, const std::vector<scene::SceneView>& extra,
                math::Vector3f eye, const std::vector<float>& depth, double seconds) {
  s.set.begin_frame();
  s.views.begin_frame(main_view(eye), false, seconds);
  for (const scene::SceneView& view : extra) {
    std::uint32_t index = 0;
    require(s.views.add(view, index) == scene::ViewError::None, "add a view");
  }
  graph::FrameGraph frame_graph;
  require(scene::declare_instance_upload(device, s.set, s.instance_buffer, frame_graph).ok(),
          "declare_instance_upload");
  require(s.hierarchy.begin_frame(device, kViewport).ok(), "hierarchy begin_frame");

  const scene::DepthHierarchyFrame hierarchy =
      scene::declare_depth_hierarchy(s.hierarchy, frame_graph);
  scene::CullingInputs inputs{&s.views,    &s.set,       &s.instance_buffer, nullptr,
                              &s.geometry, &s.hierarchy, hierarchy.history};
  scene::CullingFrame frame;
  require(scene::declare_culling_phase_one(s.culling, device, frame_graph, inputs, frame).ok(),
          "phase one");
  rhi::TextureDescription pattern{};
  pattern.extent = rhi::Extent3{kViewport.width, kViewport.height, 1};
  pattern.format = rhi::Format::D32Float;
  pattern.usage = rhi::TextureUsage::ShaderResource | rhi::TextureUsage::DepthStencil;
  pattern.initial_access = rhi::kShaderResourceMask;
  pattern.debug_name = "test depth";
  const std::array<rhi::SubresourceData, 1> initial{
      rhi::SubresourceData{std::as_bytes(std::span<const float>(depth)), kViewport.width * 4U,
                           kViewport.width * kViewport.height * 4U}};
  const rhi::Ref<rhi::Texture> texture = take(device.create_texture(pattern, initial), "depth");
  const graph::TextureHandle target = frame_graph.import_texture(
      graph::ImportedTexture{texture.get(), rhi::kShaderResourceMask, rhi::kShaderResourceMask});
  scene::declare_depth_hierarchy_build(s.hierarchy, frame_graph, hierarchy, target,
                                       scene::DepthHierarchyBuild::First);
  scene::declare_culling_phase_two(s.culling, frame_graph, frame, hierarchy.first_build);
  scene::declare_depth_hierarchy_build(s.hierarchy, frame_graph, hierarchy, target,
                                       scene::DepthHierarchyBuild::Second);

  const std::uint64_t record_bytes =
      frame_graph.imported_buffer(frame.records)->buffer->description().size;
  const std::uint64_t count_bytes =
      frame_graph.imported_buffer(frame.counts)->buffer->description().size;
  const rhi::Ref<rhi::StagingBuffer> staging =
      take(device.create_staging_buffer(record_bytes + count_bytes, "lists"), "staging");
  const rhi::Ref<rhi::GpuFence> fence = take(device.create_fence("lists"), "fence");
  frame_graph.mark_readback(frame.records, graph::ReadBack{staging.get(), fence.get(), 0});
  frame_graph.mark_readback(frame.counts,
                            graph::ReadBack{staging.get(), fence.get(), record_bytes});
  graph::CompiledFrame compiled;
  require(frame_graph
              .compile(*graph::schedule_policy_for(rhi::BackendKind::Metal), device.capabilities(),
                       compiled)
              .ok(),
          "the graph compiles");
  rhi::CommandList list;
  require(graph::execute(compiled, frame_graph, device, list).ok(), "the graph executes");
  require(device.execute(list).ok(), "execute");
  const rhi::EndFrameResult ended = device.end_frame();
  const rhi::StagingLock lock =
      take(device.lock_staging(*staging, *fence, rhi::WaitPolicy::Block), "lock_staging");
  require(!ended.gpu_fault, "no GPU fault");
  Lists out{frame.layout, {}, {}};
  out.records.assign(lock.bytes.begin(),
                     lock.bytes.begin() + static_cast<std::ptrdiff_t>(record_bytes));
  out.counts.assign(lock.bytes.begin() + static_cast<std::ptrdiff_t>(record_bytes),
                    lock.bytes.end());
  require(device.unlock_staging(*staging) == rhi::StagingError::None, "unlock_staging");
  return out;
}

// What a list must hold for a view by culling.hpp, contracts 2 and 6: the
// clusters of each live instance of the pipeline that box_outside keeps, in
// instance then cluster order, by the CPU's test over the same boxes.
std::vector<std::pair<std::uint32_t, std::uint32_t>>
expected(const Scene& s, const scene::SceneView& view, std::uint32_t pipeline) {
  std::vector<std::pair<std::uint32_t, std::uint32_t>> out;
  const std::span<const scene::InstanceRecord> records = s.set.records();
  for (std::uint32_t i = 0; i < records.size(); ++i) {
    const scene::InstanceRecord& record = records[i];
    if (record.pipeline != pipeline ||
        scene::box_outside(view.planes, record.bounds_center, record.bounds_extent)) {
      continue;
    }
    math::Matrix4f transform{};
    std::memcpy(&transform, &record.object_to_world, sizeof(transform));
    for (std::uint32_t c = 0; c < kClustersPerInstance; ++c) {
      const scene::ClusterRecord& cluster = s.clusters[c];
      const scene::Box local{
          math::Vector3f{cluster.center.x - cluster.extent.x, cluster.center.y - cluster.extent.y,
                         cluster.center.z - cluster.extent.z},
          math::Vector3f{cluster.center.x + cluster.extent.x, cluster.center.y + cluster.extent.y,
                         cluster.center.z + cluster.extent.z}};
      const scene::Box box = scene::world_box(local, transform, record.max_displacement);
      const bw::shaders::Float3 center{(box.min.x + box.max.x) * 0.5F,
                                       (box.min.y + box.max.y) * 0.5F,
                                       (box.min.z + box.max.z) * 0.5F};
      const bw::shaders::Float3 extent{(box.max.x - box.min.x) * 0.5F,
                                       (box.max.y - box.min.y) * 0.5F,
                                       (box.max.z - box.min.z) * 0.5F};
      if (!scene::box_outside(view.planes, center, extent)) {
        out.emplace_back(i, c);
      }
    }
  }
  return out;
}

void require_list(const Lists& lists, const Scene& s, const scene::SceneView& view,
                  std::uint32_t view_index, std::uint32_t pipeline, std::string_view what) {
  const std::vector<scene::DrawRecord> got =
      lists.list(view_index, pipeline, scene::DrawPhase::PhaseOne);
  const auto want = expected(s, view, pipeline);
  require(got.size() == want.size(), std::string(what) + ": the list holds what the CPU keeps");
  for (std::size_t r = 0; r < got.size(); ++r) {
    const scene::ClusterRecord& cluster = s.clusters[want[r].second];
    require(got[r].draw_index == r && got[r].instance == want[r].first &&
                got[r].first_index == cluster.first_index &&
                got[r].index_count == cluster.index_count && got[r].instance_count == 1U &&
                got[r].base_vertex == 0 && got[r].first_instance == 0U,
            r, std::string(what) + ": record in order, draw_index its position");
  }
}

// (culling.hpp, contracts 1, 2 and 6; draw_records.hpp, contracts 1 and
// 4): instances of two pipelines placed across the main view's frustum, and
// a second view looking the other way, give per view and pipeline the
// records of the clusters box_outside keeps, in instance then cluster
// order, each record's draw_index its position. Every instance has one
// level, so no detail level choice is compared. The first frame has no
// history, so the main view tests frustums alone and its PhaseTwo lists are
// empty, and every list's capacity holds every cluster of its pipeline.
void records_match_cpu(const Arguments& args) {
  const std::unique_ptr<rhi::Device> device = metal_device();
  // Down the left edge of the 90-degree frustum (|x| = |z| at the edge),
  // so some instances are wholly in, some cross, some are out.
  std::vector<Placement> placements;
  placements.reserve(9);
  for (std::uint32_t i = 0; i < 8; ++i) {
    placements.push_back(
        Placement{math::Vector3f{-6.3F + (static_cast<float>(i) * 0.8F), 0.0F, -5.0F}, i % 2U});
  }
  placements.push_back(Placement{math::Vector3f{0.0F, 0.0F, 20.0F}, 0});
  Scene s(static_cast<std::uint32_t>(placements.size()));
  build(*device, args, placements, s);
  const math::Vector3f eye{0.0F, 0.0F, 0.0F};
  scene::SceneView behind = main_view(eye);
  {
    const scene::PerspectiveView back{
        eye,   math::Vector3f{0.0F, 0.0F, 1.0F}, math::Vector3f{0.0F, 1.0F, 0.0F}, 1.5707964F, 0.5F,
        100.0F};
    scene::SceneViewDescription description{};
    description.world_to_clip = scene::world_to_clip(back, 1.0F);
    description.view_to_clip = math::Matrix4f::perspective(1.5707964F, 1.0F, 0.5F, 100.0F);
    description.eye = eye;
    description.viewport_width = 64.0F;
    description.viewport_height = 64.0F;
    description.purpose = scene::ViewPurpose::SunCascade;
    behind = scene::make_scene_view(description);
  }
  const Lists lists =
      run_frame(*device, s, {behind}, eye,
                std::vector<float>(std::size_t{kViewport.width} * kViewport.height, 1.0F), 0.0);
  for (std::uint32_t p = 0; p < 2; ++p) {
    require(lists.layout.capacities[p] == s.set.cluster_capacity(p),
            "capacity holds every cluster");
    require_list(lists, s, s.views.main(), scene::kMainView, p, "main view");
    require_list(lists, s, behind, 1, p, "second view");
    require(lists.list(scene::kMainView, p, scene::DrawPhase::PhaseTwo).empty(),
            "no PhaseTwo record without a history");
  }
  require(!lists.list(scene::kMainView, 0, scene::DrawPhase::PhaseOne).empty() &&
              lists.list(scene::kMainView, 0, scene::DrawPhase::PhaseOne).size() <
                  s.set.cluster_capacity(0),
          "the placement keeps some clusters of pipeline 0 and culls others");
  require(!lists.list(1, 0, scene::DrawPhase::PhaseOne).empty(), "the second view keeps one");
}

// (culling.hpp, contracts 3 and 4): three instances at one depth (about
// 0.91) to the main view and a depth of 0.3 over pixels x < 32, y < 24 of
// the 64 by 64 view, the far plane elsewhere. Instance 0 is under that
// occluder, instance 1 to its right and instance 2 below it. Instance 0's
// rectangle (pixels 6 to 24 by 12 to 19) reads level 2, wholly under the
// occluder; level 3 and above hold the far plane there, and a swap of rows
// or columns moves instance 0's test onto far depth and instance 1's or 2's
// onto the occluder. With that depth as the history, phase one culls
// instance 0 alone; this frame's depth, all far, lets phase two draw it the
// same frame, and no cluster is in both lists.
void occlusion_two_phase(const Arguments& args) {
  const std::unique_ptr<rhi::Device> device = metal_device();
  const std::vector<Placement> placements{Placement{math::Vector3f{-3.0F, 3.0F, -6.0F}, 0},
                                          Placement{math::Vector3f{3.0F, 3.0F, -6.0F}, 0},
                                          Placement{math::Vector3f{-3.0F, -3.0F, -6.0F}, 0}};
  Scene s(3);
  build(*device, args, placements, s);
  const math::Vector3f eye{0.0F, 0.0F, 0.0F};
  const std::vector<float> far(std::size_t{kViewport.width} * kViewport.height, 1.0F);
  std::vector<float> occluded = far;
  for (std::uint32_t y = 0; y < 24U; ++y) {
    std::fill_n(occluded.begin() + (std::ptrdiff_t{y} * kViewport.width), 32, 0.3F);
  }
  // Frame 1: no history; the occluded depth becomes the history.
  run_frame(*device, s, {}, eye, occluded, 0.0);
  // Frame 2: the history hides instance 0 alone; the occluder has moved away.
  const Lists second = run_frame(*device, s, {}, eye, far, 0.1);
  const std::vector<scene::DrawRecord> early =
      second.list(scene::kMainView, 0, scene::DrawPhase::PhaseOne);
  require(early.size() == std::size_t{2} * kClustersPerInstance,
          "frame 2: phase one keeps instances 1 and 2");
  for (std::size_t r = 0; r < early.size(); ++r) {
    require(early[r].instance == 1U + (r / kClustersPerInstance), r,
            "frame 2: phase one culls instance 0 alone against the history");
  }
  const std::vector<scene::DrawRecord> late =
      second.list(scene::kMainView, 0, scene::DrawPhase::PhaseTwo);
  require(late.size() == kClustersPerInstance, "frame 2: phase two draws it the same frame");
  for (std::size_t r = 0; r < late.size(); ++r) {
    require(late[r].draw_index == r && late[r].instance == 0U &&
                late[r].first_index == s.clusters[r].first_index,
            r, "phase two records in cluster order");
  }
}

constexpr std::array<bw::testing::Scenario<Arguments>, 2> kScenarios{{
    {"records_match_cpu", &records_match_cpu},
    {"occlusion_two_phase", &occlusion_two_phase},
}};

} // namespace

int main(int argc, char** argv) {
  return bw::testing::run_scenario(argc, argv, kScenarios, "<metal artifact directory>",
                                   [](std::span<char* const> rest, Arguments& out) {
                                     if (rest.size() != 1U) {
                                       return false;
                                     }
                                     out = Arguments{rest[0]};
                                     return true;
                                   });
}
