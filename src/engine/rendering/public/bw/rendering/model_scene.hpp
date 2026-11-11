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
#include "bw/rendering/scene/view.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

// Responsibility: one model as a scene on the frame graph: loaded by
// bw/rendering/model_resources.hpp, its materials and pipelines made by
// bw/rendering/model_draw.hpp, and drawn in one raster pass declared into a
// FrameGraph (bw/rendering/graph/builder.hpp) under the fixture light
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
//      shader_directory, out), in this order, each failure ending it:
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
//         out.material_buffer, target_format, out.draw) (ModelDrawFailed,
//         `draw` set).
//      On a failure `out` is partly built and is only destroyed.
//   2. declare_model_scene(scene, device, graph, target, size, view,
//      object_to_world) declares one frame: update_scene_blocks with
//      make_view(world_to_clip(view, aspect), view.eye, size), aspect the
//      target's width over its height as a float, and fixture_light()
//      (SceneBlocksFailed, `resource` and detail
//      set, nothing declared); then a frame-only depth texture of `size`,
//      kModelDepthFormat, the default clear (far plane) and one Raster pass
//      with `target` as the color attachment at slot 0, Clear, and the
//      depth texture DepthWrite, Clear, whose record function binds the
//      scene blocks and records the model's draws with `object_to_world`.
//      The caller imports `target` and marks it as its frame needs; the
//      pass is the frame's only use of it here.
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
// The frame game_host draws through this module is the frame it drew
// before: the same loader, materials, draw, light, pass, clear values and
// depth format; only the pass and resource debug names changed. The view
// and the turn are arguments because the two callers differ in exactly
// those.
//
// Core guidelines read: I.1 (loader, device, view and turn are arguments),
// E.27 (a named error per step, with the step's own failure beside it),
// C.21 (the five special members declared together, all deleted but the
// constructor and destructor), R.3 and C.32 (`record` holds an observer of
// its scene; the loader and device are observers for the call), F.7.
// Performance guidelines read: MEM.9 (everything is made at build, nothing
// per frame but the scene blocks' compare), GPU.7 (a replaced block buffer
// goes through its Ref, so frames in flight keep theirs).
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
  // The frame's model-to-world matrix, the one declare_model_scene was
  // given; read by record.
  math::Matrix4f object_to_world{};
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
  // SphereUnframeable's centre and radius; SceneBlocksFailed's device detail.
  std::string detail{};
  [[nodiscard]] bool ok() const noexcept { return error == ModelSceneError::None; }
};

[[nodiscard]] ModelSceneFailure
build_model_scene(rhi::Device& device, const content::Loader& loader,
                  const content::AssetId& mesh_id, rhi::Format target_format,
                  const std::filesystem::path& shader_directory, ModelScene& out);

[[nodiscard]] ModelSceneFailure declare_model_scene(ModelScene& scene, rhi::Device& device,
                                                    graph::FrameGraph& graph,
                                                    graph::TextureHandle target, rhi::Extent2 size,
                                                    const scene::PerspectiveView& view,
                                                    const math::Matrix4f& object_to_world);

[[nodiscard]] std::string model_scene_failure_text(const ModelSceneFailure& failure,
                                                   std::string_view asset);

} // namespace bw::rendering

#endif
