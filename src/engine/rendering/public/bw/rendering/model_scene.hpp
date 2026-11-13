#ifndef BW_RENDERING_MODEL_SCENE_HPP
#define BW_RENDERING_MODEL_SCENE_HPP

#include "bw/content/asset_id.hpp"
#include "bw/content/loader.hpp"
#include "bw/math/matrix/matrix4.hpp"
#include "bw/rendering/graph/builder.hpp"
#include "bw/rendering/graph/declaration.hpp"
#include "bw/rendering/materials/material_buffer.hpp"
#include "bw/rendering/model_draw.hpp"
#include "bw/rendering/model_resources.hpp"
#include "bw/rendering/rhi/device.hpp"
#include "bw/rendering/rhi/format.hpp"
#include "bw/rendering/rhi/resources.hpp"
#include "bw/rendering/scene/culling.hpp"
#include "bw/rendering/scene/depth_hierarchy.hpp"
#include "bw/rendering/scene/depth_pass.hpp"
#include "bw/rendering/scene/draw_records.hpp"
#include "bw/rendering/scene/frame_views.hpp"
#include "bw/rendering/scene/instance_set.hpp"
#include "bw/rendering/scene/scene_geometry.hpp"
#include "bw/rendering/scene/view.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

// Responsibility: one model as a scene on the frame graph: loaded by
// bw/rendering/model_resources.hpp, its materials and pipelines made by
// bw/rendering/model_draw.hpp, its draws entered as instances of the scene
// (docs/architecture/scene.md), and drawn through the culling dispatch,
// the depth pass with the depth hierarchy built after each of its two
// draws, and the forward lit pass over the main view's draw records, all
// declared into a FrameGraph (bw/rendering/graph/builder.hpp) under the
// fixture light
// (bw/rendering/scene/fixture_light.hpp), from a view the caller gives
// (bw/rendering/scene/view.hpp). game_host composes it under its turning
// framing and character_viewer under its orbit camera; the image is the
// same draw from a different view (docs/architecture/source-layout.md,
// "Several compositions, one runtime"). Nothing here reads options, prints,
// or names an executable: every failure is returned by name and the caller
// prints its own line with model_scene_failure_text.
//
// Contract.
//   1. build_model_scene(device, loader, mesh_id, target_format,
//      shader_directory, grid, out), in this order, each failure ending it:
//      a. load_model(device, loader, mesh_id, model_texture_parameters(),
//         out.model) (ModelLoadFailed, `load` set);
//      b. the mesh's bounding sphere frames at aspect 1, the aspect that
//         puts the eye nearest the centre, so it frames at every aspect
//         (frame_sphere; SphereUnframeable, detail names the centre and
//         radius);
//      c. one material buffer with room for every material's block at
//         kMaxMaterialBlockBytes, at least one, named "model scene
//         material buffer" (MaterialBufferFailed, `material_buffer` set);
//      d. create_model_draw(device, out.model, shader_directory,
//         out.material_buffer, out.pipelines, target_format, out.draw)
//         (ModelDrawFailed, `draw` set);
//      e. the scene: the mesh registered in out.geometry (SceneFailed,
//         detail names the table that is full); grid * grid copies of the
//         model, copy (i, j) at the offset (i * s, 0, -j * s), s =
//         kModelGridSpacing times the mesh's radius, so copy (0, 0) is the
//         model's own place and a grid of 1 is the model alone; for each
//         copy one instance per draw of the mesh, in draw order, with the
//         draw's geometry, its material's slot and blend mode, its
//         pipeline's table index (kNone for a blended material, which
//         the CPU order then draws), kind Mesh, casting a shadow, at the
//         copy's offset (SceneFailed); the culling dispatch, the depth
//         hierarchy and the depth pass created from `shader_directory`
//         with a pipeline per table key (SceneFailed, detail the step's
//         text). The capacities are exactly what the grid needs; a grid
//         whose clusters under one pipeline index exceed the device's
//         Capabilities::max_multi_draw_count is SceneFailed here, the
//         detail naming the pipeline index and the two numbers.
//         Precondition: grid is 1 to kMaxModelGrid.
//      On a failure `out` is partly built and is only destroyed.
//   2. declare_model_scene(scene, device, graph, target, size, view,
//      object_to_world, camera_cut, seconds) declares one frame:
//      update_scene_blocks
//      with make_view(world_to_clip(view, aspect), view.eye, size), aspect
//      the target's width over its height as a float, and fixture_light()
//      (SceneBlocksFailed, `resource` and detail set, nothing declared);
//      the frame start of the instance set; every instance moved to its
//      copy's offset after `object_to_world` on the first frame and on a
//      frame whose `object_to_world` differs from the frame before's, and
//      none moved otherwise; the main view registered with `camera_cut`
//      as the publisher's cut flag (true on the frame after the caller
//      replaced its camera) and `seconds` as the clock; the geometry
//      uploaded; the hierarchy's begin_frame at
//      `size`; the CPU order of the main view's blended instances and,
//      when it has an entry, a buffer of one DrawRecord per entry, new
//      each frame and held through its Ref so frames in flight keep
//      theirs (SceneFailed, nothing declared); the changed instance
//      records' upload declared (scene/instance_set.hpp, contract 7;
//      SceneFailed, nothing declared);
//      then a frame-only depth texture of
//      `size`, kModelDepthFormat, the default clear (far plane), and in
//      this order: culling phase one; the depth pass's first draw, Clear;
//      the hierarchy's first build; culling phase two; the depth pass's
//      second draw, Keep; the hierarchy's second build; and one Raster
//      pass with `target` as the color attachment at slot 0, Clear, the
//      depth texture DepthTest and the instance records Sampled, whose
//      record function binds the scene
//      blocks and records the model's draws (model_draw.hpp, contract 3).
//      The caller imports `target` and marks it as its frame needs; the
//      pass is the frame's only use of it here. A failure of culling
//      phase one is SceneFailed with the instance upload and the
//      hierarchy's two imports already declared, and the caller does not
//      execute that graph. A frame
//      whose graph the caller does not execute, for this or any reason,
//      is followed by scene.hierarchy.invalidate().
//   2a. The image. With a grid of 1 and a model whose every material is
//      Opaque or Masked, each pixel of `target` is shaded from the
//      model's surface nearest the camera there: the depth pass leaves
//      each pixel's nearest depth, and a forward draw passes only where
//      its depth equals it. A blended draw is drawn after every opaque
//      and masked one, far to near. On one device the same model, size
//      and camera give the same bytes on every run, and on every build
//      until one changes the shading.
//   3. The pass's record function is the scene's `record` member, borrowed
//      by the graph until the frame is executed (graph/declaration.hpp,
//      contract 5), and it points at the scene, so a ModelScene is neither
//      copied nor moved: a caller that holds several keeps each behind a
//      unique_ptr, and the scene outlives the graph's execute and the
//      command list's execution of that frame.
//   4. model_scene_failure_text(failure, asset) is one line for a person
//      naming the asset id, the failure and its detail; its wording is not
//      part of the contract. No failure falls back: a model whose draws
//      cannot all be drawn with their materials fails by name.
//
// The view and the turn are arguments because the two callers differ in
// exactly those; the clock is an argument because nothing here reads one.
//
// Core guidelines read: I.1 (loader, device, view and turn are arguments),
// E.27 (a named error per step, with the step's own failure beside it),
// C.21 (the five special members declared together, all deleted but the
// constructor and destructor), R.3 and C.32 (`record` holds an observer of
// its scene; the loader and device are observers for the call), F.7.
// Performance guidelines read: MEM.9 (everything is made at build; a frame
// makes the scene blocks' compare and, when a blended draw is in view, its
// record buffer, and keeps its vectors' storage), GPU.7 (a replaced block
// or record buffer goes through its Ref, so frames in flight keep theirs).
namespace bw::rendering {

struct ModelScene {
  // Binds the scene blocks and records the model's draws inside the pass
  // the graph opened.
  struct Record {
    ModelScene* scene;
    void operator()(graph::RecordContext& context) const;
  };

  ModelResources model{};
  materials::MaterialBuffer material_buffer{};
  ModelDraw draw{};
  SceneBlocks blocks{};
  // The scene the model's draws are instances of.
  scene::PipelineTable pipelines{};
  scene::SceneGeometry geometry{scene::SceneGeometryCapacity{}};
  scene::GeometryBuffers geometry_buffers{};
  scene::SceneMesh mesh{};
  scene::InstanceSet instances{0};
  scene::InstanceBuffer instance_buffer{};
  // One id per copy per draw, copy-major, and each copy's offset.
  std::vector<scene::InstanceId> instance_ids{};
  std::vector<math::Matrix4f> copy_offsets{};
  scene::FrameViews views{};
  scene::Culling culling{};
  scene::DepthHierarchy hierarchy{};
  scene::DepthPass depth_pass{};
  // The frame declare_model_scene declared last; read by record.
  scene::CullingFrame culling_frame{};
  scene::CpuOrder order{};
  // The blended draws' records of the frame declared last; the storage is
  // kept across frames.
  std::vector<scene::DrawRecord> cpu_record_data{};
  // The turn the instances were last moved to; none before the first frame.
  math::Matrix4f placed_object_to_world{};
  bool placed{};
  rhi::Ref<rhi::Buffer> cpu_records{};
  rhi::Ref<rhi::ShaderResourceView> cpu_records_view{};
  Record record{this};

  ModelScene() = default;
  ModelScene(const ModelScene&) = delete;
  ModelScene& operator=(const ModelScene&) = delete;
  ModelScene(ModelScene&&) = delete;
  ModelScene& operator=(ModelScene&&) = delete;
  ~ModelScene() = default;
};

enum class ModelSceneError : std::uint8_t {
  None,
  ModelLoadFailed,
  SphereUnframeable,
  MaterialBufferFailed,
  ModelDrawFailed,
  SceneBlocksFailed,
  SceneFailed,
};

struct ModelSceneFailure {
  ModelSceneError error{ModelSceneError::None};
  // The step's own failure: load for ModelLoadFailed, material_buffer for
  // MaterialBufferFailed, draw for ModelDrawFailed, resource for
  // SceneBlocksFailed; the others are at their None.
  ModelLoadFailure load{};
  materials::MaterialBufferFailure material_buffer{};
  ModelDrawFailure draw{};
  rhi::ResourceError resource{rhi::ResourceError::None};
  // SphereUnframeable's centre and radius; SceneBlocksFailed's device
  // detail; SceneFailed's step and its own detail text.
  std::string detail{};
  [[nodiscard]] bool ok() const noexcept { return error == ModelSceneError::None; }
};

// Copies stand kModelGridSpacing mesh radii apart; a grid is at most
// kMaxModelGrid copies a side.
inline constexpr float kModelGridSpacing = 2.5F;
inline constexpr std::uint32_t kMaxModelGrid = 64;

[[nodiscard]] ModelSceneFailure build_model_scene(rhi::Device& device,
                                                  const content::Loader& loader,
                                                  const content::AssetId& mesh_id,
                                                  rhi::Format target_format,
                                                  const std::filesystem::path& shader_directory,
                                                  std::uint32_t grid, ModelScene& out);

[[nodiscard]] ModelSceneFailure declare_model_scene(ModelScene& scene, rhi::Device& device,
                                                    graph::FrameGraph& graph,
                                                    graph::TextureHandle target, rhi::Extent2 size,
                                                    const scene::PerspectiveView& view,
                                                    const math::Matrix4f& object_to_world,
                                                    bool camera_cut, double seconds);

[[nodiscard]] std::string model_scene_failure_text(const ModelSceneFailure& failure,
                                                   std::string_view asset);

} // namespace bw::rendering

#endif
