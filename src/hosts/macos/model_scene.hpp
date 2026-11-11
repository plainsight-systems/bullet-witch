#ifndef BW_HOSTS_MACOS_MODEL_SCENE_HPP
#define BW_HOSTS_MACOS_MODEL_SCENE_HPP

#include "host_scene.hpp"

#include "bw/hosts/game_host_options.hpp"
#include "bw/rendering/graph/builder.hpp"
#include "bw/rendering/graph/declaration.hpp"
#include "bw/rendering/model_scene.hpp"
#include "bw/rendering/rhi/device.hpp"
#include "bw/rendering/rhi/format.hpp"

#include <string>

// Responsibility: game_host's draw with --cache and --asset: an adapter over
// the engine's model scene (bw/rendering/model_scene.hpp), which loads the
// model of that id, makes its materials and pipelines and declares its one
// raster pass under the fixture light. What the adapter adds is game_host's:
// the layers opened from its options, the view and the turn, and the
// program name on every failure line.
//
// The layers are opened over --cache as the converted originals and
// --overrides when given, with no published cache (bw/content/layers.hpp,
// open_layers). The camera frames the mesh's bounding sphere at the
// target's aspect (bw/rendering/scene/view.hpp, frame_sphere) and the mesh
// turns about the vertical axis through the sphere's center, so the whole
// model stays in view at every angle; a mesh whose bounding sphere cannot
// be framed is a named failure.
//
// Host-private: a source of the game_host target, included by no test.
namespace bw::hosts {

// The model scene and the --asset id its failure lines name. Neither is
// moved: the scene's record function points at it (model_scene.hpp,
// contract 3), and HostScene is not movable.
struct AssetScene : HostScene {
  rendering::ModelScene scene{};
  std::string asset{};

  // The turning model under the framing view, declared through the model
  // scene into `graph` onto `target`. False after a failure line.
  [[nodiscard]] bool declare(rhi::Device& device, graph::FrameGraph& graph,
                             graph::TextureHandle target, rhi::Extent2 size,
                             double elapsed) override;
};

// Opens the layers over --cache and --overrides, checks the --asset id and
// builds the model scene from the artifacts in --shaders. Prints the
// failure line and returns false on any failure.
bool build_asset_scene(rhi::Device& device, const GameHostOptions& options,
                       rhi::Format target_format, AssetScene& scene);

} // namespace bw::hosts

#endif
