#include "model_scene.hpp"

#include "host_failure.hpp"
#include "scene_support.hpp"

#include "bw/content/asset_id.hpp"
#include "bw/content/layers.hpp"
#include "bw/content/loader.hpp"
#include "bw/hosts/game_host_options.hpp"
#include "bw/math/matrix/matrix4.hpp"
#include "bw/math/rotation/quaternion.hpp"
#include "bw/math/vector/vector3.hpp"
#include "bw/rendering/geometry/mesh_resources.hpp"
#include "bw/rendering/materials/material.hpp"
#include "bw/rendering/materials/material_buffer.hpp"
#include "bw/rendering/model_draw.hpp"
#include "bw/rendering/model_resources.hpp"
#include "bw/rendering/rhi/command_list.hpp"
#include "bw/rendering/rhi/device.hpp"
#include "bw/rendering/rhi/format.hpp"
#include "bw/rendering/rhi/resources.hpp"
#include "bw/rendering/scene/fixture_light.hpp"
#include "bw/rendering/scene/view.hpp"
#include "bw/rendering/target_pass.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>

namespace bw::hosts {

namespace {

// The model's turn about the vertical axis through its bounding sphere's
// center.
math::Matrix4f asset_object_to_world(double elapsed_seconds, const geometry::MeshResources& mesh) {
  const math::Vector3f center{mesh.center[0], mesh.center[1], mesh.center[2]};
  const math::Quaternion4f turn = math::Quaternion4f::from_axis_angle(
      math::Vector3f{0.0F, 1.0F, 0.0F}, turn_angle(elapsed_seconds, 0.5));
  return math::Matrix4f::translation(-center) * math::Matrix4f::rotation_translation(turn, center);
}

// The view that frames the mesh's bounding sphere at the target's aspect
// (bw/rendering/scene/view.hpp, frame_sphere); empty when the sphere cannot
// be framed.
std::optional<bw::scene::PerspectiveView> asset_view(rhi::Extent2 size,
                                                     const geometry::MeshResources& mesh) {
  const math::Vector3f center{mesh.center[0], mesh.center[1], mesh.center[2]};
  return bw::scene::frame_sphere(center, mesh.radius, aspect_of(size));
}

// The failure line of a mesh whose bounding sphere cannot be framed; false.
bool fail_unframeable(std::string_view asset, const geometry::MeshResources& mesh) {
  std::cerr << "game_host: the mesh " << asset << " has the bounding sphere center ("
            << mesh.center[0] << ", " << mesh.center[1] << ", " << mesh.center[2] << ") radius "
            << mesh.radius << " and cannot be framed\n";
  return false;
}

// The layers over --cache and --overrides, with no published cache; prints
// the failure and, for every failure of the converted layer (the directory
// absent, its record missing, unreadable, invalid or of another format
// version), the converter command that makes one. LayerDirectoryUnreadable
// gets no pointer: it may name the overrides directory, which the converter
// does not make; its detail names the path.
std::optional<content::LayerDirectories> open_asset_layers(const GameHostOptions& options) {
  std::optional<std::filesystem::path> overrides;
  if (!options.overrides.empty()) {
    overrides = options.overrides;
  }
  const content::OpenLayersResult opened =
      content::open_layers(overrides, std::nullopt, options.cache);
  if (opened.ok()) {
    return opened.layers;
  }
  std::cerr << "game_host: opening the content layers failed: error "
            << static_cast<unsigned>(opened.error) << " " << content::layer_error_name(opened.error)
            << ": " << opened.detail << '\n';
  if (opened.error == content::LayerError::ConvertedNotADirectory ||
      opened.error == content::LayerError::ConvertedRecordMissing ||
      opened.error == content::LayerError::ConvertedRecordUnreadable ||
      opened.error == content::LayerError::ConvertedRecordInvalid ||
      opened.error == content::LayerError::ConvertedRecordVersionMismatch) {
    std::cerr << "game_host: " << options.cache.string()
              << " is not a converted-originals cache this game_host can read; make it with: "
                 "content_converter convert --install <install directory> --out "
              << options.cache.string() << '\n';
  }
  return std::nullopt;
}

} // namespace

bool build_asset_scene(rhi::Device& device, const GameHostOptions& options,
                       rhi::Format target_format, AssetScene& scene) {
  scene.asset = options.asset;
  const std::optional<content::LayerDirectories> layers = open_asset_layers(options);
  if (!layers.has_value()) {
    return false;
  }
  const content::Loader loader(*layers);
  const content::AssetIdResult id = content::make_asset_id(options.asset);
  if (!id.ok()) {
    std::cerr << "game_host: the asset id " << options.asset << " does not validate: error "
              << static_cast<unsigned>(id.error) << '\n';
    return false;
  }
  const rendering::ModelLoadFailure loaded = rendering::load_model(
      device, loader, id.id, rendering::model_texture_parameters(), scene.model);
  if (!loaded.ok()) {
    std::cerr << "game_host: loading the model " << options.asset << " failed: error "
              << static_cast<unsigned>(loaded.error) << " for " << loaded.id << ": "
              << loaded.detail << '\n';
    return false;
  }
  const geometry::MeshResources& mesh = scene.model.mesh;
  // A square target puts the eye nearest the center, so a sphere framed
  // here is framed at every aspect.
  if (!asset_view(rhi::Extent2{1, 1}, mesh).has_value()) {
    return fail_unframeable(options.asset, mesh);
  }
  // Room for every material's block at the largest block size.
  const auto capacity = static_cast<std::uint32_t>(
      std::max<std::size_t>(scene.model.materials.size(), 1U) * materials::kMaxMaterialBlockBytes);
  const materials::MaterialBufferFailure buffer = materials::create_material_buffer(
      device, capacity, "game_host material buffer", scene.material_buffer);
  if (!buffer.ok()) {
    std::cerr << "game_host: the material buffer of " << options.asset << " failed: error "
              << static_cast<unsigned>(buffer.error) << ": " << buffer.detail << '\n';
    return false;
  }
  const rendering::ModelDrawFailure made = rendering::create_model_draw(
      device, scene.model, options.shaders, scene.material_buffer, target_format, scene.draw);
  if (!made.ok()) {
    std::cerr << "game_host: the draw of " << options.asset << " with its materials failed: error "
              << static_cast<unsigned>(made.error) << " at index " << made.index;
    if (!made.name.empty()) {
      std::cerr << " (" << made.name << ")";
    }
    std::cerr << ": " << made.detail << '\n';
    return false;
  }
  return true;
}

// The depth target follows the viewport's size (target_pass.hpp,
// resize_depth_target): a frame whose viewport no longer matches it (after
// a Resized event or a mismatch resize) gets a new one. The scene blocks
// follow the view (model_draw.hpp, update_scene_blocks). False after a
// failure line.
bool AssetScene::record(rhi::Device& device, rhi::CommandList& list, rhi::Texture& back_buffer,
                        rhi::Extent2 size, double elapsed) {
  const rhi::Status<rhi::ResourceError> resized = rendering::resize_depth_target(
      device, size, rendering::kModelDepthFormat, "game_host depth", depth);
  if (!resized.ok()) {
    fail("depth target", resized.error(), resized.detail());
    return false;
  }
  const std::optional<bw::scene::PerspectiveView> view = asset_view(size, model.mesh);
  // The startup check framed this sphere at the aspect that gives the
  // nearest eye, so this is not reached; the empty result is still handled.
  if (!view.has_value()) {
    return fail_unframeable(asset, model.mesh);
  }
  const bw::scene::View view_block =
      bw::scene::make_view(bw::scene::world_to_clip(*view, aspect_of(size)), view->eye,
                           static_cast<float>(size.width), static_cast<float>(size.height));
  const rhi::Status<rhi::ResourceError> updated =
      rendering::update_scene_blocks(device, view_block, bw::scene::fixture_light(), blocks);
  if (!updated.ok()) {
    fail("scene blocks", updated.error(), updated.detail());
    return false;
  }
  rendering::begin_cleared_pass(list, back_buffer, depth.get(), size, "game_host frame");
  rendering::bind_scene_blocks(list, blocks);
  rendering::record_model_draw(list, draw, model, asset_object_to_world(elapsed, model.mesh));
  list.end_render_pass();
  return true;
}

} // namespace bw::hosts
