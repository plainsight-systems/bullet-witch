#include "bw/rendering/model_scene.hpp"

#include "bw/content/asset_id.hpp"
#include "bw/content/loader.hpp"
#include "bw/math/matrix/matrix4.hpp"
#include "bw/math/vector/vector3.hpp"
#include "bw/rendering/geometry/mesh_asset.hpp"
#include "bw/rendering/geometry/mesh_resources.hpp"
#include "bw/rendering/geometry/vertex_streams.hpp"
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
#include "bw/rendering/scene/cpu_order.hpp"
#include "bw/rendering/scene/culling.hpp"
#include "bw/rendering/scene/depth_hierarchy.hpp"
#include "bw/rendering/scene/depth_pass.hpp"
#include "bw/rendering/scene/draw_records.hpp"
#include "bw/rendering/scene/fixture_light.hpp"
#include "bw/rendering/scene/frame_views.hpp"
#include "bw/rendering/scene/instance.hpp"
#include "bw/rendering/scene/instance_set.hpp"
#include "bw/rendering/scene/scene_geometry.hpp"
#include "bw/rendering/scene/view.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

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

ModelSceneFailure scene_failed(std::string step, std::string detail) {
  ModelSceneFailure failure{};
  failure.error = ModelSceneError::SceneFailed;
  failure.detail = std::move(step) + ": " + std::move(detail);
  return failure;
}

std::string_view table_name(scene::GeometryTable table) {
  switch (table) {
  case scene::GeometryTable::Clusters:
    return "clusters";
  case scene::GeometryTable::Levels:
    return "levels";
  case scene::GeometryTable::Indices:
    return "indices";
  }
  return "a table";
}

// Step e: the mesh in the scene's tables, grid * grid copies of its draws
// as instances, and the culling dispatch, hierarchy and depth pass.
void build_scene(rhi::Device& device, const std::filesystem::path& shader_directory,
                 std::uint32_t grid, ModelScene& out, ModelSceneFailure& failure) {
  const geometry::MeshAssetView& mesh = out.model.mesh_view;
  std::uint32_t indices = 0;
  for (std::uint32_t g = 0; g < mesh.group_count(); ++g) {
    indices += mesh.group(g).index_count;
  }
  out.geometry = scene::SceneGeometry(scene::SceneGeometryCapacity{
      mesh.cluster_count(), mesh.draw_count() * mesh.level_count(), indices});
  std::vector<geometry::VertexStreams> blocks;
  blocks.reserve(out.model.mesh.groups.size());
  for (const geometry::MeshGroupResources& group : out.model.mesh.groups) {
    blocks.push_back(group.block);
  }
  const scene::GeometryFailure added = out.geometry.add_mesh(mesh, blocks, out.mesh);
  if (!added.ok()) {
    failure = scene_failed("the mesh's tables", std::string(table_name(added.table)) + " is full");
    return;
  }
  const std::uint32_t draws = mesh.draw_count();
  out.instances = scene::InstanceSet(grid * grid * draws);
  const float spacing = kModelGridSpacing * out.model.mesh.radius;
  out.copy_offsets.clear();
  out.instance_ids.clear();
  for (std::uint32_t i = 0; i < grid; ++i) {
    for (std::uint32_t j = 0; j < grid; ++j) {
      out.copy_offsets.push_back(math::Matrix4f::translation(
          math::Vector3f{static_cast<float>(i) * spacing, 0.0F, -static_cast<float>(j) * spacing}));
    }
  }
  for (const math::Matrix4f& offset : out.copy_offsets) {
    for (std::uint32_t d = 0; d < draws; ++d) {
      const std::uint32_t material = out.model.mesh.draws[d].material;
      const materials::MaterialInstance& instance = out.draw.instances[material];
      scene::InstanceDescription description{};
      description.geometry = out.mesh.draws[d];
      description.material = out.material_buffer.reference(out.draw.slots[material]);
      description.blend_mode = instance.blend_mode();
      description.pipeline = out.draw.pipelines[out.draw.draw_pipelines[d]].table_index;
      description.transform = offset;
      description.kind = scene::InstanceKind::Mesh;
      description.flags = scene::kInstanceCastsShadow;
      scene::InstanceId id{};
      if (out.instances.enter(description, id) != scene::InstanceError::None) {
        failure = scene_failed("an instance", "the instance set is full");
        return;
      }
      out.instance_ids.push_back(id);
    }
  }
  // Each pipeline index's list holds every cluster of its instances
  // (culling.hpp, contract 6), and a counted multi-draw takes at most the
  // device's maximum.
  const std::uint32_t maximum = device.capabilities().max_multi_draw_count;
  for (const ModelPipeline& pipeline : out.draw.pipelines) {
    if (pipeline.table_index == scene::kNone) {
      continue;
    }
    const std::uint32_t clusters = out.instances.cluster_capacity(pipeline.table_index);
    if (clusters > maximum) {
      failure =
          scene_failed("the draw lists", "pipeline index " + std::to_string(pipeline.table_index) +
                                             " has " + std::to_string(clusters) +
                                             " clusters, above the device's multi-draw "
                                             "maximum of " +
                                             std::to_string(maximum));
      return;
    }
  }
  const scene::CullingFailure culling =
      scene::create_culling(device, shader_directory, out.culling);
  if (!culling.ok()) {
    failure = scene_failed("the culling dispatch", culling.detail);
    return;
  }
  const scene::DepthHierarchyFailure hierarchy =
      scene::create_depth_hierarchy(device, shader_directory, out.hierarchy);
  if (!hierarchy.ok()) {
    failure = scene_failed("the depth hierarchy", hierarchy.detail);
    return;
  }
  scene::DepthPassFailure depth =
      scene::create_depth_pass(device, shader_directory, kModelDepthFormat, out.depth_pass);
  if (depth.ok()) {
    depth = out.depth_pass.prepare(device, out.pipelines);
  }
  if (!depth.ok()) {
    failure = scene_failed("the depth pass", depth.detail);
  }
}

} // namespace

ModelSceneFailure build_model_scene(rhi::Device& device, const content::Loader& loader,
                                    const content::AssetId& mesh_id, rhi::Format target_format,
                                    const std::filesystem::path& shader_directory,
                                    std::uint32_t grid, ModelScene& out) {
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
                                   out.pipelines, target_format, out.draw);
  if (!failure.draw.ok()) {
    failure.error = ModelSceneError::ModelDrawFailed;
    return failure;
  }
  build_scene(device, shader_directory, grid, out, failure);
  return failure;
}

void ModelScene::Record::operator()(graph::RecordContext& context) const {
  bind_scene_blocks(context.list(), scene->blocks);
  ModelDrawFrame frame{};
  frame.culling = &scene->culling_frame;
  frame.records = context.buffer(scene->culling_frame.records);
  frame.counts = context.buffer(scene->culling_frame.counts);
  frame.instance_buffer = &scene->instance_buffer;
  frame.geometry_buffers = &scene->geometry_buffers;
  frame.order = &scene->order;
  frame.instances = &scene->instances;
  frame.geometry = &scene->geometry;
  // The record buffer exists only in a frame with a blended entry, and only
  // such a frame reads its index.
  if (!scene->order.blended.empty()) {
    frame.cpu_records_index = scene->cpu_records_view->buffer_index();
  }
  record_model_draw(context.list(), scene->draw, frame);
}

// The scene blocks follow the view (model_draw.hpp, update_scene_blocks).
// The depth texture is frame-only: the graph creates it at the target's size
// for the frame and gives it back after the pass, so a resized target needs
// no step here. The CPU records buffer is made only in a frame whose order
// has a blended entry; a frame with none drops the scene's Ref, and a frame
// in flight keeps its buffer through the device's deferred release.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
ModelSceneFailure declare_model_scene(ModelScene& scene, rhi::Device& device,
                                      graph::FrameGraph& graph, graph::TextureHandle target,
                                      rhi::Extent2 size, const scene::PerspectiveView& view,
                                      const math::Matrix4f& object_to_world, bool camera_cut,
                                      double seconds) {
  ModelSceneFailure failure{};
  const float aspect = static_cast<float>(size.width) / static_cast<float>(size.height);
  const math::Matrix4f world_to_clip = scene::world_to_clip(view, aspect);
  const scene::View view_block = scene::make_view(
      world_to_clip, view.eye, static_cast<float>(size.width), static_cast<float>(size.height));
  const rhi::Status<rhi::ResourceError> updated =
      update_scene_blocks(device, view_block, scene::fixture_light(), scene.blocks);
  if (!updated.ok()) {
    failure.error = ModelSceneError::SceneBlocksFailed;
    failure.resource = updated.error();
    failure.detail = std::string(updated.detail());
    return failure;
  }
  scene.instances.begin_frame();
  if (!scene.placed || scene.placed_object_to_world.m != object_to_world.m) {
    for (std::size_t i = 0; i < scene.instance_ids.size(); ++i) {
      const std::size_t copy = i / std::max<std::size_t>(scene.mesh.draws.size(), 1U);
      const scene::InstanceError moved =
          scene.instances.move(scene.instance_ids[i], object_to_world * scene.copy_offsets[copy]);
      if (moved != scene::InstanceError::None) {
        return scene_failed("an instance", "move refused instance " + std::to_string(i) +
                                               ": error " +
                                               std::to_string(static_cast<unsigned>(moved)));
      }
    }
    scene.placed_object_to_world = object_to_world;
    scene.placed = true;
  }
  scene::SceneViewDescription main{};
  main.world_to_clip = world_to_clip;
  main.view_to_clip =
      math::Matrix4f::perspective(view.vertical_fov, aspect, view.near_plane, view.far_plane);
  main.eye = view.eye;
  main.viewport_width = static_cast<float>(size.width);
  main.viewport_height = static_cast<float>(size.height);
  main.purpose = scene::ViewPurpose::Main;
  scene.views.begin_frame(scene::make_scene_view(main), camera_cut, seconds);
  const scene::GeometryUploadFailure geometry =
      scene::upload_geometry(device, scene.geometry, scene.geometry_buffers);
  if (!geometry.ok()) {
    return scene_failed("the mesh tables", geometry.detail);
  }
  const scene::DepthHierarchyFailure begun = scene.hierarchy.begin_frame(device, size);
  if (!begun.ok()) {
    return scene_failed("the depth hierarchy", begun.detail);
  }
  scene::order_cpu_draws(scene.views.main(), scene.instances.records(), scene.geometry.levels(),
                         scene.order);
  scene.cpu_record_data.assign(scene.order.blended.size(), scene::DrawRecord{});
  for (std::size_t k = 0; k < scene.order.blended.size(); ++k) {
    scene.cpu_record_data[k].instance = scene.order.blended[k].instance;
    scene.cpu_record_data[k].detail_level = scene.order.blended[k].detail_level;
  }
  scene.cpu_records = {};
  scene.cpu_records_view = {};
  if (!scene.cpu_record_data.empty()) {
    rhi::BufferDescription records{};
    records.size = scene.cpu_record_data.size() * sizeof(scene::DrawRecord);
    records.usage = rhi::BufferUsage::ShaderResource;
    records.initial_access = rhi::kShaderResourceMask;
    records.debug_name = "model scene cpu records";
    rhi::BufferResult made = device.create_buffer(
        records, std::as_bytes(std::span<const scene::DrawRecord>(scene.cpu_record_data)));
    if (!made.ok()) {
      return scene_failed("the CPU records", std::string(made.detail()));
    }
    rhi::ShaderResourceViewResult viewed =
        device.create_shader_resource_view(made.value(), rhi::BufferViewDescription{});
    if (!viewed.ok()) {
      return scene_failed("the CPU records", std::string(viewed.detail()));
    }
    scene.cpu_records = std::move(made.value());
    scene.cpu_records_view = std::move(viewed.value());
  }
  const scene::InstanceUploadFailure instances =
      scene::declare_instance_upload(device, scene.instances, scene.instance_buffer, graph);
  if (!instances.ok()) {
    return scene_failed("the instance records", instances.detail);
  }

  const scene::DepthHierarchyFrame hierarchy =
      scene::declare_depth_hierarchy(scene.hierarchy, graph);
  const scene::CullingInputs inputs{
      &scene.views,     &scene.instances,        &scene.instance_buffer,
      &scene.geometry,  &scene.geometry_buffers, &scene.hierarchy,
      hierarchy.history};
  const scene::CullingFailure culled =
      scene::declare_culling_phase_one(scene.culling, device, graph, inputs, scene.culling_frame);
  if (!culled.ok()) {
    return scene_failed("the culling dispatch", culled.detail);
  }
  // Cleared to the far plane (the default clear depth, 1).
  const graph::TextureHandle depth = graph.create_texture(
      graph::FrameTextureDescription{size, kModelDepthFormat, {}, "model scene depth"});
  scene::DepthDrawInputs draw{depth,
                              graph::Contents::Clear,
                              &scene.culling_frame,
                              scene::DrawPhase::PhaseOne,
                              &scene.instance_buffer,
                              &scene.geometry_buffers,
                              scene.blocks.view.get()};
  scene::declare_depth_draw(scene.depth_pass, graph, draw);
  scene::declare_depth_hierarchy_build(scene.hierarchy, graph, hierarchy, depth,
                                       scene::DepthHierarchyBuild::First);
  scene::declare_culling_phase_two(scene.culling, graph, scene.culling_frame,
                                   hierarchy.first_build);
  draw.contents = graph::Contents::Keep;
  draw.phase = scene::DrawPhase::PhaseTwo;
  scene::declare_depth_draw(scene.depth_pass, graph, draw);
  scene::declare_depth_hierarchy_build(scene.hierarchy, graph, hierarchy, depth,
                                       scene::DepthHierarchyBuild::Second);
  const std::array<graph::TextureUse, 2> uses{
      graph::TextureUse{target, graph::Access::Attachment, graph::Contents::Clear, 0},
      graph::TextureUse{depth, graph::Access::DepthTest, graph::Contents::Keep, 0}};
  // Both forward stages read the instance records.
  const std::array<graph::BufferUse, 3> lists{
      graph::BufferUse{scene.culling_frame.records, graph::Access::Arguments},
      graph::BufferUse{scene.culling_frame.counts, graph::Access::Arguments},
      graph::BufferUse{scene.instance_buffer.handle, graph::Access::Sampled}};
  graph.add_pass(
      graph::PassDeclaration{"model scene", graph::PassKind::Raster, uses, lists, scene.record});
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
  case ModelSceneError::SceneFailed:
    text << "the scene of " << asset << " failed: " << failure.detail;
    break;
  }
  return text.str();
}

} // namespace bw::rendering
