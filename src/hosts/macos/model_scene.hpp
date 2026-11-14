#ifndef BW_HOSTS_MACOS_MODEL_SCENE_HPP
#define BW_HOSTS_MACOS_MODEL_SCENE_HPP

#include "host_scene.hpp"

#include "bw/hosts/game_host_options.hpp"
#include "bw/rendering/graph/builder.hpp"
#include "bw/rendering/graph/declaration.hpp"
#include "bw/rendering/model_scene.hpp"
#include "bw/rendering/postprocess/post_chain.hpp"
#include "bw/rendering/rhi/device.hpp"
#include "bw/rendering/rhi/format.hpp"

#include <string>

// Responsibility: game_host's draw with --cache and --asset: an adapter over
// the engine's model scene (bw/rendering/model_scene.hpp), which loads the
// model of that id, makes its materials and pipelines and declares its one
// raster pass under the fixture light. What the adapter adds is game_host's:
// the layers opened from its options, the view and the turn, the post chain
// (bw/rendering/postprocess/post_chain.hpp) that takes the scene's light to
// the back buffer, and the program name on every failure line.
//
// A frame, in the chain's order (post_chain.hpp, contract 2): the chain's
// frame begins, scene colour of the target's size is created, the model
// scene is declared into it with the frame's pre-exposure, and the chain is
// declared from scene colour into the back buffer under the model scene's
// grade (bw/rendering/model_scene.hpp, model_scene_grade: a model has no
// stage, and its exposure is pinned at the fixture light's camera), the
// viewport's display report, the loop's time
// step, a reset on the first frame and the count of frames drawn. The model
// scene is built for scene colour's format and the chain for the back
// buffer's. After the graph is recorded the chain is asked whether a pass
// recorded less than it declared (end_post_frame); such a frame is dropped,
// never executed. Every failure is one line and ends the run; no frame is
// drawn without the chain.
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

// The model scene, the post chain and the --asset id the failure lines
// name. None is moved: the scene's record function points at it
// (model_scene.hpp, contract 3), and HostScene is not movable.
struct AssetScene : HostScene {
  rendering::ModelScene scene{};
  postprocess::PostChain chain{};
  std::string asset{};

  // The turning model under the framing view, declared through the model
  // scene into scene colour and through the chain onto `target`. False
  // after a failure line.
  [[nodiscard]] bool declare(rhi::Device& device, graph::FrameGraph& graph,
                             graph::TextureHandle target, rhi::Extent2 size,
                             const HostFrame& frame) override;
  // The chain's end_post_frame (post_chain.hpp, contract 2d); on a failure
  // its line, then frame_dropped, and false.
  [[nodiscard]] bool frame_recorded() override;
  // The depth hierarchy's history and the exposure texels are not the
  // dropped frame's (model_scene.hpp, contract 2; post_chain.hpp,
  // contract 3).
  void frame_dropped() override;
};

// Opens the layers over --cache and --overrides, checks the --asset id,
// builds the model scene for scene colour and the post chain for
// `target_format` from the artifacts in --shaders. Prints the failure line
// and returns false on any failure.
bool build_asset_scene(rhi::Device& device, const GameHostOptions& options,
                       rhi::Format target_format, AssetScene& scene);

} // namespace bw::hosts

#endif
