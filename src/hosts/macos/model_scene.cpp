#include "model_scene.hpp"

#include "scene_support.hpp"

#include "bw/content/asset_id.hpp"
#include "bw/content/layers.hpp"
#include "bw/content/loader.hpp"
#include "bw/hosts/game_host_options.hpp"
#include "bw/math/matrix/matrix4.hpp"
#include "bw/math/rotation/quaternion.hpp"
#include "bw/math/vector/vector3.hpp"
#include "bw/rendering/geometry/mesh_resources.hpp"
#include "bw/rendering/graph/builder.hpp"
#include "bw/rendering/graph/declaration.hpp"
#include "bw/rendering/model_scene.hpp"
#include "bw/rendering/postprocess/exposure.hpp"
#include "bw/rendering/postprocess/post_chain.hpp"
#include "bw/rendering/rhi/device.hpp"
#include "bw/rendering/rhi/format.hpp"
#include "bw/rendering/scene/view.hpp"

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
  const rendering::ModelSceneFailure built = rendering::build_model_scene(
      device, loader, id.id, postprocess::kSceneColorFormat, options.shaders, 1, scene.scene);
  if (!built.ok()) {
    std::cerr << "game_host: " << rendering::model_scene_failure_text(built, options.asset) << '\n';
    return false;
  }
  const postprocess::PostChainFailure chain =
      postprocess::build_post_chain(device, options.shaders, target_format, scene.chain);
  if (!chain.ok()) {
    std::cerr << "game_host: " << postprocess::post_chain_failure_text(chain) << '\n';
    return false;
  }
  return true;
}

// The framing view at the target's aspect and the turn at the frame's
// elapsed seconds, handed to the model scene, whose light the chain takes
// to `target`. False after a failure line.
bool AssetScene::declare(rhi::Device& device, graph::FrameGraph& graph, graph::TextureHandle target,
                         rhi::Extent2 size, const HostFrame& frame) {
  const geometry::MeshResources& mesh = scene.model.mesh;
  const math::Vector3f center{mesh.center[0], mesh.center[1], mesh.center[2]};
  const std::optional<bw::scene::PerspectiveView> view =
      bw::scene::frame_sphere(center, mesh.radius, aspect_of(size));
  // The build framed this sphere at the aspect that gives the nearest eye,
  // so this is not reached; the empty result is still handled.
  if (!view.has_value()) {
    return fail_unframeable(asset, mesh);
  }
  postprocess::PostFrame post{};
  const postprocess::PostChainFailure begun =
      postprocess::begin_post_frame(chain, device, graph, post);
  if (!begun.ok()) {
    std::cerr << "game_host: " << postprocess::post_chain_failure_text(begun) << '\n';
    return false;
  }
  const graph::TextureHandle scene_color =
      graph.create_texture(postprocess::scene_color_description(size));
  const rendering::ModelSceneFailure declared = rendering::declare_model_scene(
      scene, device, graph, scene_color, size, *view, asset_object_to_world(frame.elapsed, mesh),
      false, frame.elapsed, post.exposure.pre_exposure);
  if (!declared.ok()) {
    std::cerr << "game_host: " << rendering::model_scene_failure_text(declared, asset) << '\n';
    return false;
  }
  postprocess::PostFrameInputs inputs{};
  inputs.grade = rendering::model_scene_grade();
  inputs.display = frame.display;
  inputs.delta_seconds = frame.delta_seconds;
  // The exposure resets on the first frame only, not on a large camera turn
  // or move or a time reset: the model scene's exposure is pinned
  // (model_scene_grade), so a reset changes nothing it shows, and this
  // camera turns the model, not itself.
  inputs.reset = frame.first;
  inputs.frame_index = frame.frame_index;
  const postprocess::PostChainFailure chained = postprocess::declare_post_chain(
      chain, device, graph, post, scene_color, target, size, inputs);
  if (!chained.ok()) {
    std::cerr << "game_host: " << postprocess::post_chain_failure_text(chained) << '\n';
    return false;
  }
  return true;
}

bool AssetScene::frame_recorded() {
  const postprocess::PostChainFailure recorded = postprocess::end_post_frame(chain);
  if (recorded.ok()) {
    return true;
  }
  std::cerr << "game_host: " << postprocess::post_chain_failure_text(recorded) << '\n';
  frame_dropped();
  return false;
}

void AssetScene::frame_dropped() {
  scene.hierarchy.invalidate();
  postprocess::post_frame_dropped(chain);
}

} // namespace bw::hosts
