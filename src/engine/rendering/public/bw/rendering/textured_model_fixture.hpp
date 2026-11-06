#ifndef BW_RENDERING_TEXTURED_MODEL_FIXTURE_HPP
#define BW_RENDERING_TEXTURED_MODEL_FIXTURE_HPP

#include "bw/math/matrix/matrix4.hpp"
#include "bw/rendering/geometry/textured_mesh_fixture.hpp"
#include "bw/rendering/materials/material_asset.hpp"
#include "bw/rendering/model_resources.hpp"
#include "bw/rendering/rhi/command_list.hpp"
#include "bw/rendering/rhi/device.hpp"
#include "bw/rendering/rhi/format.hpp"
#include "bw/rendering/rhi/pipeline_states.hpp"
#include "bw/rendering/rhi/resources.hpp"
#include "bw/rendering/shaders/artifact.hpp"
#include "bw/rendering/shaders/parameter_block.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// Responsibility: the fixture draw of a loaded model: the textured pair of
// bw/rendering/geometry/textured_mesh_fixture.hpp over a ModelResources,
// each draw shaded by its material's base color. It is what draws converted
// content until the renderer's material templates do; it leaves then, and
// bw/rendering/model_resources.hpp stays. The host's window and the
// offscreen test both make the pipeline and record the draws through these
// two functions, inside a pass begun by bw/rendering/target_pass.hpp and
// seen through a view from bw/rendering/scene/view.hpp.
//
// Contract.
//   1. A model drawn here is loaded with kTexturedModelTextureParameters.
//   2. create_textured_model checks every group with textured_pair_reads
//      (GroupLayoutUnsupported, index the group); resolves each draw's base
//      color: the material's base_color texture, which must be 2D
//      (BaseColorNotTexture2D, index the draw), or a 1x1 opaque white
//      texture when the draw has no material or its material names no base
//      color; creates the white texture only if some draw needs it
//      (WhiteTextureFailed); and makes the states, the trilinear sampler
//      and the pipeline: the textured pair, no cull, depth tested LessEqual
//      and written, one color target of `color_format`, depth
//      kTexturedModelDepthFormat, one sample, triangle lists, precompiled
//      before it is created (StateFailed, PipelineFailed).
//   3. draw_base_colors has one entry per model.mesh.draws, in that order.
//      fallback_draws counts the draws shaded by the white texture and
//      first_fallback_draw is the first of them; a caller reports the
//      count, and nothing hides that a draw fell back.
//   4. record_textured_model sets the pipeline and, for each draw in
//      model.mesh.draws order, binds the group's index buffer when the
//      group changes, sets a TexturedMeshDraw block (world_to_clip, the
//      group's block with the draw's base vertex, the draw's base color,
//      the sampler) and records draw_indexed(index_count, 1, first_index,
//      0, 0): exactly model.mesh.draws.size() draws.
//      Preconditions: `list` is inside a render pass whose targets have the
//      formats in 2 with the viewport and scissor set (begin_cleared_pass
//      with a depth target of kTexturedModelDepthFormat does both);
//      `textured` was created from `model`.
//   5. On a failure `out` is left empty. The detail text carries the
//      device's error and detail; its wording is not part of the contract.
//
// Read guidance: I.1, E.27, R.1; GDSA.12 (texture and sampler by direct
// index).
namespace bw::rendering {

inline constexpr rhi::Format kTexturedModelDepthFormat = rhi::Format::D32Float;
inline constexpr std::array<std::string_view, 1> kTexturedModelTextureParameters{
    materials::kBaseColorTexture};

struct TexturedModel {
  rhi::Ref<rhi::RasterizerState> rasterizer{};
  rhi::Ref<rhi::DepthStencilState> depth_stencil{};
  rhi::Ref<rhi::BlendState> blend{};
  rhi::Ref<rhi::Sampler> sampler{};
  rhi::Ref<rhi::GraphicsPipeline> pipeline{};
  // Null when no draw falls back.
  rhi::Ref<rhi::Texture> white{};
  rhi::Ref<rhi::ShaderResourceView> white_view{};
  std::vector<shaders::TextureIndex> draw_base_colors{};
  std::uint32_t fallback_draws{};
  // Meaningful when fallback_draws is not 0.
  std::uint32_t first_fallback_draw{};
};

enum class TexturedModelError : std::uint8_t {
  None,
  GroupLayoutUnsupported,
  BaseColorNotTexture2D,
  WhiteTextureFailed,
  StateFailed,
  PipelineFailed,
};

struct TexturedModelFailure {
  TexturedModelError error{TexturedModelError::None};
  // The group for GroupLayoutUnsupported, the draw for
  // BaseColorNotTexture2D, otherwise 0.
  std::uint32_t index{};
  std::string detail{};
  [[nodiscard]] bool ok() const noexcept { return error == TexturedModelError::None; }
};

// `vertex` and `pixel` are the loaded artifacts of kTexturedMeshShader and
// kTexturedColorShader; both must outlive the call.
[[nodiscard]] TexturedModelFailure
create_textured_model(rhi::Device& device, const ModelResources& model,
                      const shaders::Artifact& vertex, const shaders::Artifact& pixel,
                      rhi::Format color_format, TexturedModel& out);

void record_textured_model(rhi::CommandList& list, const TexturedModel& textured,
                           const ModelResources& model, const math::Matrix4f& world_to_clip);

} // namespace bw::rendering

#endif
