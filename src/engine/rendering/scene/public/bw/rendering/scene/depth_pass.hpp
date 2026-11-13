#ifndef BW_RENDERING_SCENE_DEPTH_PASS_HPP
#define BW_RENDERING_SCENE_DEPTH_PASS_HPP

#include "bw/rendering/graph/builder.hpp"
#include "bw/rendering/graph/declaration.hpp"
#include "bw/rendering/rhi/device.hpp"
#include "bw/rendering/rhi/format.hpp"
#include "bw/rendering/rhi/resources.hpp"
#include "bw/rendering/scene/culling.hpp"
#include "bw/rendering/scene/depth_shaders.hpp"
#include "bw/rendering/scene/draw_records.hpp"
#include "bw/rendering/scene/instance_set.hpp"
#include "bw/rendering/scene/scene_geometry.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

// Responsibility: the depth pass (docs/architecture/scene-renderer.md,
// "Passes a template provides", the Depth row; scene.md, invariants 8 and
// 9): the pipelines of the lit template's depth-only shaders
// (depth_shaders.hpp) and the declaration of one draw of the main view's
// records into the depth target, which a frame makes twice, once per
// culling phase.
// The depth and opaque passes draw the same culled records. There are no
// position-only vertex streams and masked draws are not sorted behind
// opaque ones: vertices are pulled, and a list is one pipeline.
//
// Contract.
//   1. The shaders and what each stage computes are depth_shaders.hpp's.
//   2. prepare(device, table) makes one pipeline per key of the table
//      that has none: the depth format given at creation, depth tested
//      LessEqual and written, no color target, no
//      cull, triangle lists. A missing artifact or a refused pipeline is
//      returned by name and the pass keeps what it had.
//   3. declare_depth_draw(pass, graph, inputs) declares one Raster pass
//      with `inputs.depth` DepthWrite and `inputs.contents`, reading the
//      frame's records and counts as Arguments and the instance records
//      Sampled (instance_set.hpp, contract 7), whose record function binds
//      the view block and, for each pipeline index in order, that
//      pipeline, the scene's index buffer, a RecordDraw naming the list
//      (kMainView, pipeline, inputs.phase) and one counted multi-draw of
//      it with the list's capacity as the maximum. The first draw of a
//      frame clears depth; the second keeps it. Precondition: prepare
//      returned no failure for the table since its last key was added, so
//      every pipeline index of the frame's layout has a pipeline; a broken
//      one stops the process with a line on stderr, and no list is skipped.
//
// Core guidelines read: I.1, E.27, C.21, R.3. Performance guidelines read:
// GDSA.17 (pipelines made before the first draw that needs them), GPU.6
// (one multi-draw per pipeline, whatever is visible).
namespace bw::scene {

enum class DepthPassError : std::uint8_t {
  None,
  ArtifactFailed,
  StateFailed,
  PipelineFailed,
};

struct DepthPassFailure {
  DepthPassError error{DepthPassError::None};
  // The pipeline index concerned, or 0.
  std::uint32_t pipeline{};
  std::string detail{};
  [[nodiscard]] bool ok() const noexcept { return error == DepthPassError::None; }
};

// What one draw of the pass reads. Observers; the owner keeps each alive
// through the frame's execution.
struct DepthDrawInputs {
  graph::TextureHandle depth{};
  graph::Contents contents{graph::Contents::Clear};
  const CullingFrame* culling{};
  DrawPhase phase{DrawPhase::PhaseOne};
  const InstanceBuffer* instance_buffer{};
  const GeometryBuffers* geometry_buffers{};
  // The buffer holding the frame's View block (view.hpp, kViewSlot).
  rhi::Buffer* view_block{};
};

class DepthPass {
public:
  DepthPass();
  DepthPass(const DepthPass&) = delete;
  DepthPass& operator=(const DepthPass&) = delete;
  DepthPass(DepthPass&&) noexcept;
  DepthPass& operator=(DepthPass&&) noexcept;
  ~DepthPass();

  [[nodiscard]] DepthPassFailure prepare(rhi::Device& device, const PipelineTable& table);

private:
  friend struct DepthPassAccess;
  struct State;
  std::unique_ptr<State> state_;
};

[[nodiscard]] DepthPassFailure create_depth_pass(rhi::Device& device,
                                                 const std::filesystem::path& shader_directory,
                                                 rhi::Format depth_format, DepthPass& out);

void declare_depth_draw(DepthPass& pass, graph::FrameGraph& graph, const DepthDrawInputs& inputs);

} // namespace bw::scene

#endif
