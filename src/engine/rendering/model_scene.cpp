#include "bw/rendering/model_scene.hpp"

#include "bw/content/asset_id.hpp"
#include "bw/content/loader.hpp"
#include "bw/math/matrix/matrix4.hpp"
#include "bw/math/vector/vector3.hpp"
#include "bw/rendering/geometry/mesh_resources.hpp"
#include "bw/rendering/graph/access.hpp"
#include "bw/rendering/graph/builder.hpp"
#include "bw/rendering/graph/declaration.hpp"
#include "bw/rendering/materials/material.hpp"
#include "bw/rendering/materials/material_buffer.hpp"
#include "bw/rendering/model_draw.hpp"
#include "bw/rendering/model_resources.hpp"
#include "bw/rendering/rhi/device.hpp"
#include "bw/rendering/rhi/format.hpp"
#include "bw/rendering/rhi/resources.hpp"
#include "bw/rendering/scene/fixture_light.hpp"
#include "bw/rendering/scene/view.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <sstream>
#include <string>
#include <string_view>

// Responsibility: the out-of-line part of bw/rendering/model_scene.hpp. The
// steps and their order are the ones game_host's model draw took before the
// module existed; nothing here prints.
namespace bw::rendering {
namespace {

math::Vector3f centre_of(const geometry::MeshResources& mesh) {
  return math::Vector3f{mesh.center[0], mesh.center[1], mesh.center[2]};
}

std::string sphere_text(const geometry::MeshResources& mesh) {
  std::ostringstream text;
  text << "centre (" << mesh.center[0] << ", " << mesh.center[1] << ", " << mesh.center[2]
       << ") radius " << mesh.radius;
  return text.str();
}

} // namespace

ModelSceneFailure build_model_scene(rhi::Device& device, const content::Loader& loader,
                                    const content::AssetId& mesh_id, rhi::Format target_format,
                                    const std::filesystem::path& shader_directory,
                                    ModelScene& out) {
  ModelSceneFailure failure{};
  failure.load = load_model(device, loader, mesh_id, model_texture_parameters(), out.model);
  if (!failure.load.ok()) {
    failure.error = ModelSceneError::ModelLoadFailed;
    return failure;
  }
  const geometry::MeshResources& mesh = out.model.mesh;
  // A square target puts the eye nearest the centre, so a sphere framed
  // here is framed at every aspect.
  if (!scene::frame_sphere(centre_of(mesh), mesh.radius, 1.0F).has_value()) {
    failure.error = ModelSceneError::SphereUnframeable;
    failure.detail = sphere_text(mesh);
    return failure;
  }
  // Room for every material's block at the largest block size.
  const auto capacity = static_cast<std::uint32_t>(
      std::max<std::size_t>(out.model.materials.size(), 1U) * materials::kMaxMaterialBlockBytes);
  failure.material_buffer = materials::create_material_buffer(
      device, capacity, "model scene material buffer", out.material_buffer);
  if (!failure.material_buffer.ok()) {
    failure.error = ModelSceneError::MaterialBufferFailed;
    return failure;
  }
  failure.draw = create_model_draw(device, out.model, shader_directory, out.material_buffer,
                                   target_format, out.draw);
  if (!failure.draw.ok()) {
    failure.error = ModelSceneError::ModelDrawFailed;
    return failure;
  }
  return failure;
}

void ModelScene::Record::operator()(graph::RecordContext& context) const {
  bind_scene_blocks(context.list(), scene->blocks);
  record_model_draw(context.list(), scene->draw, scene->model, scene->object_to_world);
}

// The scene blocks follow the view (model_draw.hpp, update_scene_blocks).
// The depth texture is frame-only: the graph creates it at the target's size
// for the frame and gives it back after the pass, so a resized target needs
// no step here.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
ModelSceneFailure declare_model_scene(ModelScene& scene, rhi::Device& device,
                                      graph::FrameGraph& graph, graph::TextureHandle target,
                                      rhi::Extent2 size, const scene::PerspectiveView& view,
                                      const math::Matrix4f& object_to_world) {
  ModelSceneFailure failure{};
  const float aspect = static_cast<float>(size.width) / static_cast<float>(size.height);
  const scene::View view_block =
      scene::make_view(scene::world_to_clip(view, aspect), view.eye, static_cast<float>(size.width),
                       static_cast<float>(size.height));
  const rhi::Status<rhi::ResourceError> updated =
      update_scene_blocks(device, view_block, scene::fixture_light(), scene.blocks);
  if (!updated.ok()) {
    failure.error = ModelSceneError::SceneBlocksFailed;
    failure.resource = updated.error();
    failure.detail = std::string(updated.detail());
    return failure;
  }
  scene.object_to_world = object_to_world;
  // Cleared to the far plane (the default clear depth, 1).
  const graph::TextureHandle depth = graph.create_texture(
      graph::FrameTextureDescription{size, kModelDepthFormat, {}, "model scene depth"});
  const std::array<graph::TextureUse, 2> uses{
      graph::TextureUse{target, graph::Access::Attachment, graph::Contents::Clear, 0},
      graph::TextureUse{depth, graph::Access::DepthWrite, graph::Contents::Clear, 0}};
  graph.add_pass(
      graph::PassDeclaration{"model scene", graph::PassKind::Raster, uses, {}, scene.record});
  return failure;
}

std::string model_scene_failure_text(const ModelSceneFailure& failure, std::string_view asset) {
  std::ostringstream text;
  switch (failure.error) {
  case ModelSceneError::None:
    text << "the model scene of " << asset << " reported no failure";
    break;
  case ModelSceneError::ModelLoadFailed:
    text << "loading the model " << asset << " failed: error "
         << static_cast<unsigned>(failure.load.error) << " for " << failure.load.id << ": "
         << failure.load.detail;
    break;
  case ModelSceneError::SphereUnframeable:
    text << "the mesh " << asset << " has the bounding sphere " << failure.detail
         << " and cannot be framed";
    break;
  case ModelSceneError::MaterialBufferFailed:
    text << "the material buffer of " << asset << " failed: error "
         << static_cast<unsigned>(failure.material_buffer.error) << ": "
         << failure.material_buffer.detail;
    break;
  case ModelSceneError::ModelDrawFailed:
    text << "the draw of " << asset << " with its materials failed: error "
         << static_cast<unsigned>(failure.draw.error) << " at index " << failure.draw.index;
    if (!failure.draw.name.empty()) {
      text << " (" << failure.draw.name << ")";
    }
    text << ": " << failure.draw.detail;
    break;
  case ModelSceneError::SceneBlocksFailed:
    text << "the scene blocks of " << asset << " failed: error "
         << static_cast<unsigned>(failure.resource) << ": " << failure.detail;
    break;
  }
  return text.str();
}

} // namespace bw::rendering
