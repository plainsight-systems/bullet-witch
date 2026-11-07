#ifndef BW_HOSTS_MACOS_MODEL_SCENE_HPP
#define BW_HOSTS_MACOS_MODEL_SCENE_HPP

#include "host_scene.hpp"

#include "bw/hosts/game_host_options.hpp"
#include "bw/rendering/materials/material_buffer.hpp"
#include "bw/rendering/model_draw.hpp"
#include "bw/rendering/model_resources.hpp"
#include "bw/rendering/rhi/command_list.hpp"
#include "bw/rendering/rhi/device.hpp"
#include "bw/rendering/rhi/format.hpp"
#include "bw/rendering/rhi/resources.hpp"

#include <string>

// Responsibility: game_host's draw with --cache and --asset: the model of
// that id, loaded by bw/rendering/model_resources.hpp and drawn with its own
// materials by bw/rendering/model_draw.hpp in a pass begun by
// bw/rendering/target_pass.hpp, under the fixture light
// (bw/rendering/scene/fixture_light.hpp). The matrices are made by
// bw/math/matrix/matrix4.hpp and bw/rendering/scene/view.hpp; the model's
// turn is its object_to_world and the view is a View block, and the scene
// writes no matrix element, resource description or pass description.
//
// The model draw: the layers are opened over --cache as the converted
// originals and --overrides when given, with no published cache
// (bw/content/layers.hpp, open_layers). Every material of the model is made
// and placed in one material buffer and every draw is checked before the
// first frame; a model whose draws cannot all be drawn with their materials
// is a named failure (model_draw.hpp, ModelDrawError), never a fallback.
// The camera frames the mesh's bounding sphere (view.hpp, frame_sphere) and
// the mesh turns about the vertical axis through the sphere's center, so
// the whole model stays in view at every angle; a mesh whose bounding
// sphere cannot be framed is a named failure. The pass has a depth target
// sized to the viewport, recreated when the viewport's size changes; the
// View block's buffer is remade when the view changes.
//
// Host-private: a source of the game_host target, included by no test.
namespace bw::hosts {

// Everything the model draw holds: the model, the material buffer its
// instances live in, its pipelines and draws, the scene blocks, the depth
// target, and the --asset id its failure lines name.
struct AssetScene : HostScene {
  rendering::ModelResources model{};
  materials::MaterialBuffer material_buffer{};
  rendering::ModelDraw draw{};
  rendering::SceneBlocks blocks{};
  rhi::Ref<rhi::Texture> depth{};
  std::string asset{};

  // The converted asset's commands into the list between the back buffer's
  // two transitions. False after a failure line.
  [[nodiscard]] bool record(rhi::Device& device, rhi::CommandList& list, rhi::Texture& back_buffer,
                            rhi::Extent2 size, double elapsed) override;
};

// Loads the model --asset names from the layers over --cache and
// --overrides and makes its materials and pipelines from the artifacts in
// --shaders. Prints the failure line and returns false on any failure.
bool build_asset_scene(rhi::Device& device, const GameHostOptions& options,
                       rhi::Format target_format, AssetScene& scene);

} // namespace bw::hosts

#endif
