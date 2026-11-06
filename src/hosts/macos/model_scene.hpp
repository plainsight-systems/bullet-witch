#ifndef BW_HOSTS_MACOS_MODEL_SCENE_HPP
#define BW_HOSTS_MACOS_MODEL_SCENE_HPP

#include "host_scene.hpp"

#include "bw/hosts/game_host_options.hpp"
#include "bw/rendering/model_resources.hpp"
#include "bw/rendering/rhi/command_list.hpp"
#include "bw/rendering/rhi/device.hpp"
#include "bw/rendering/rhi/format.hpp"
#include "bw/rendering/rhi/resources.hpp"
#include "bw/rendering/shaders/artifact_file.hpp"
#include "bw/rendering/textured_model_fixture.hpp"

#include <string>

// Responsibility: game_host's draw with --cache and --asset: the model of
// that id, loaded by bw/rendering/model_resources.hpp and drawn by
// bw/rendering/textured_model_fixture.hpp in a pass begun by
// bw/rendering/target_pass.hpp. The matrices are made by
// bw/math/matrix/matrix4.hpp and bw/rendering/scene/view.hpp; the scene
// composes the model's turn with its view, and writes no matrix element,
// resource description or pass description.
//
// The model draw: the layers are opened over --cache as the converted
// originals and --overrides when given, with no published cache
// (bw/content/layers.hpp, open_layers). Once the model is built, one stdout
// line `base_color_fallback draws=K of=T first=ID` names the K of the T
// draws that sample the white fallback and the material id of the first of
// them (kNoMaterial for a draw without a material, none when K is 0), so a
// run whose draws all took the fallback reads differently from a textured
// one. The camera frames the mesh's bounding sphere (view.hpp,
// frame_sphere) and the mesh turns about the vertical axis through the
// sphere's center, so the whole model stays in view at every angle; a mesh
// whose bounding sphere cannot be framed is a named failure. The pass has
// a depth target sized to the viewport, recreated when the viewport's size
// changes.
//
// Host-private: a source of the game_host target, included by no test.
namespace bw::hosts {

// Everything the model draw holds: the two artifacts, the model, its
// fixture pipeline and the depth target, and the --asset id its failure
// lines name.
struct AssetScene : HostScene {
  shaders::ArtifactFile vertex{};
  shaders::ArtifactFile pixel{};
  rendering::ModelResources model{};
  rendering::TexturedModel textured{};
  rhi::Ref<rhi::Texture> depth{};
  std::string asset{};

  // The converted asset's commands into the list between the back buffer's
  // two transitions. False after a failure line.
  [[nodiscard]] bool record(rhi::Device& device, rhi::CommandList& list, rhi::Texture& back_buffer,
                            rhi::Extent2 size, double elapsed) override;
};

// Loads the model --asset names from the layers over --cache and
// --overrides and builds its fixture pipeline. Prints the failure line and
// returns false on any failure.
bool build_asset_scene(rhi::Device& device, const GameHostOptions& options,
                       rhi::Format target_format, AssetScene& scene);

} // namespace bw::hosts

#endif
