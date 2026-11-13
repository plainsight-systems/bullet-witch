#ifndef BW_RENDERING_SCENE_DEPTH_HIERARCHY_STATE_HPP
#define BW_RENDERING_SCENE_DEPTH_HIERARCHY_STATE_HPP

#include "bw/rendering/graph/declaration.hpp"
#include "bw/rendering/rhi/device.hpp"
#include "bw/rendering/rhi/format.hpp"
#include "bw/rendering/rhi/pipeline_states.hpp"
#include "bw/rendering/rhi/resources.hpp"
#include "bw/rendering/scene/depth_hierarchy.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

// Responsibility: the private state of a DepthHierarchy
// (bw/rendering/scene/depth_hierarchy.hpp), shared inside the scene module
// with the culling dispatch, which reads the two textures through their
// sampled views. Not a public header: nothing outside src/engine/rendering/
// scene includes it.
namespace bw::scene {

// One level's rows in the scratch buffer.
struct DepthHierarchyLevel {
  rhi::Extent2 extent{};
  std::uint32_t offset{};
  std::uint32_t pitch{};
};

struct DepthHierarchy::State {
  // The record function of one build: the reduce pass or the copy pass.
  struct Record {
    State* state{};
    DepthHierarchyBuild build{DepthHierarchyBuild::First};
    bool copy{};
    void operator()(graph::RecordContext& context) const;
  };

  rhi::Device* device{};
  rhi::Ref<rhi::ComputePipeline> reduce{};
  rhi::Extent2 view{};
  rhi::Extent2 extent{};
  std::uint32_t levels{};
  std::vector<DepthHierarchyLevel> layout{};
  // History then first build, each with a view of every level.
  std::array<rhi::Ref<rhi::Texture>, 2> textures{};
  std::array<rhi::Ref<rhi::ShaderResourceView>, 2> views{};
  rhi::Ref<rhi::Buffer> scratch{};
  rhi::Ref<rhi::UnorderedAccessView> scratch_view{};
  bool has_history{};
  // A second build was declared since the last begin_frame and no
  // invalidate followed it.
  bool second_declared{};
  // A sampled view of a frame's depth texture the reduce pass could not
  // make; begin_frame returns it as ViewFailed.
  std::string view_failure{};
  // By build: its reduce pass recorded its dispatches this frame. The copy
  // pass records nothing without them, so a refused view leaves the build's
  // texture as it was.
  std::array<bool, 2> reduced{};
  // The frame's imports and the depth textures each build reads.
  DepthHierarchyFrame frame{};
  graph::BufferHandle scratch_handle{};
  std::array<graph::TextureHandle, 2> depth{};
  std::array<Record, 4> records{};
};

struct DepthHierarchyAccess {
  using State = DepthHierarchy::State;
  [[nodiscard]] static DepthHierarchy::State& state(const DepthHierarchy& hierarchy) noexcept {
    return *hierarchy.state_;
  }
};

} // namespace bw::scene

#endif
