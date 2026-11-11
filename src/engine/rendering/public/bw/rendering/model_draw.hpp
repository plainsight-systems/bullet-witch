#ifndef BW_RENDERING_MODEL_DRAW_HPP
#define BW_RENDERING_MODEL_DRAW_HPP

#include "bw/math/matrix/matrix4.hpp"
#include "bw/rendering/materials/material.hpp"
#include "bw/rendering/materials/material_buffer.hpp"
#include "bw/rendering/materials/shading.hpp"
#include "bw/rendering/model_resources.hpp"
#include "bw/rendering/rhi/command_list.hpp"
#include "bw/rendering/rhi/device.hpp"
#include "bw/rendering/rhi/format.hpp"
#include "bw/rendering/rhi/pipeline_states.hpp"
#include "bw/rendering/rhi/resources.hpp"
#include "bw/rendering/rhi/result.hpp"
#include "bw/rendering/scene/fixture_light.hpp"
#include "bw/rendering/scene/view.hpp"
#include "bw/rendering/shaders/permutation.hpp"

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// Responsibility: a loaded model drawn with its own materials: each
// material asset made into a material and an instance in the material
// buffer, one pipeline per (permutation, blend mode) the model's materials
// use, and the draws recorded through the forward pass's lit shaders
// (docs/architecture/materials.md, "Material to pipelines";
// bw/rendering/scene/forward_pass.hpp). The host's window and the
// offscreen test both draw a model through these functions, inside a pass
// the render graph opened for them (bw/rendering/graph/executor.hpp).
//
// Contract.
//   1. A model drawn here is loaded with model_texture_parameters(): the
//      texture parameter names of every material template.
//   2. create_model_draw, in this order, each failure ending it:
//      a. every draw of the mesh names a material (DrawWithoutMaterial,
//         index the draw);
//      b. for each material of the model, in index order: each texture
//         the model holds for it is 2D (TextureNotTexture2D, index the
//         material, name the parameter); create_material from its asset,
//         those textures and the one sampler (MaterialFailed, index the
//         material, material_error and name from create_material); an
//         instance of it is added to `material_buffer`
//         (MaterialBufferFailed);
//      c. for each draw, its group's layout carries every vertex
//         attribute its material's permutation reads, with the type the
//         template decodes (VertexAttributeMissing, index the draw, name
//         the attribute);
//      d. for each distinct (permutation, blend mode) among the materials
//         that a draw uses, the two forward lit artifacts of that
//         permutation are read from `shader_directory` (ArtifactFailed,
//         detail artifact_file_failure_text) and one pipeline is made and
//         precompiled before it is created: no cull; one color target of
//         `color_format`, depth kModelDepthFormat, one sample, triangle
//         lists; depth tested LessEqual; and by blend mode
//           Opaque, Masked  depth written, no blending;
//           Translucent     depth not written, source alpha over
//                           (SourceAlpha, InverseSourceAlpha);
//           Additive        depth not written, added (SourceAlpha, One)
//         with the same factors for the alpha channel as for color
//         (StateFailed, PipelineFailed).
//      The one sampler is made before the materials of b, since each
//      material takes its index; its failure is StateFailed with index 0.
//      pipelines holds one entry per distinct pair, in first-use order by
//      draw; draw_pipelines has one entry per model.mesh.draws, the index
//      of the draw's pipeline. No vertex layout enters a pipeline.
//   3. record_model_draw records, for each draw in model.mesh.draws order
//      (the mesh asset's order is kept; nothing here sorts blended
//      draws): the draw's pipeline when it changes, the
//      group's index buffer when the group changes, a ForwardDraw block
//      (object_to_world, the group's block with the draw's base vertex,
//      the draw's material reference) and draw_indexed(index_count, 1,
//      first_index, 0, 0): exactly model.mesh.draws.size() draws.
//      Preconditions: `list` is inside a render pass whose targets have
//      the formats in 2d with the viewport and scissor set
//      (the render graph's executor sets both for a pass, graph/executor.hpp
//      contract 1c); bind_scene_blocks was recorded in this frame; `draw` was
//      created from `model`; `material_buffer` outlives the draw.
//   4. Scene blocks. update_scene_blocks makes a uniform buffer holding
//      `view` and one holding `light` (usage Uniform, the block's bytes as
//      initial data) when `blocks` has none or the block's bytes differ
//      from those its buffer was made with; a replaced buffer is released
//      through its Ref, so frames in flight keep reading the old one. On a
//      failure `blocks` is unchanged. bind_scene_blocks records
//      set_static_block(scene::kViewSlot) and
//      set_static_block(scene::kFixtureLightSlot) with those buffers.
//   5. On a failure of create_model_draw `out` is left empty. The detail
//      text is for a person; its wording is not part of the contract.
//
// Read guidance: I.1, E.27, R.1 (the Refs own every GPU object); GDSA.12
// (a material by buffer index and offset), GDSA.17 (pipelines are keyed by
// permutation and blend state and made before the first draw).
namespace bw::rendering {

inline constexpr rhi::Format kModelDepthFormat = rhi::Format::D32Float;

[[nodiscard]] std::span<const std::string_view> model_texture_parameters() noexcept;

struct ModelPipeline {
  shaders::PermutationId permutation{};
  materials::BlendMode blend_mode{materials::BlendMode::Opaque};
  rhi::Ref<rhi::GraphicsPipeline> pipeline{};
};

struct ModelDraw {
  // One per material of the model, in index order: the instance drawn and
  // its range in the material buffer. A caller that changes an instance
  // passes it and its slot to MaterialBuffer::update.
  std::vector<materials::MaterialInstance> instances{};
  std::vector<materials::MaterialSlot> slots{};
  std::vector<ModelPipeline> pipelines{};
  // One per model.mesh.draws: the index into pipelines.
  std::vector<std::uint32_t> draw_pipelines{};
  rhi::Ref<rhi::Sampler> sampler{};
  rhi::Ref<rhi::RasterizerState> rasterizer{};
  // Depth written, and depth tested only.
  rhi::Ref<rhi::DepthStencilState> depth_write{};
  rhi::Ref<rhi::DepthStencilState> depth_read{};
  // By blend mode: none, source alpha over, added.
  rhi::Ref<rhi::BlendState> blend_none{};
  rhi::Ref<rhi::BlendState> blend_over{};
  rhi::Ref<rhi::BlendState> blend_add{};
  // The buffer the slots are ranges of; observed, not owned.
  const materials::MaterialBuffer* material_buffer{};
};

enum class ModelDrawError : std::uint8_t {
  None,
  DrawWithoutMaterial,
  TextureNotTexture2D,
  MaterialFailed,
  MaterialBufferFailed,
  VertexAttributeMissing,
  ArtifactFailed,
  StateFailed,
  PipelineFailed,
};

struct ModelDrawFailure {
  ModelDrawError error{ModelDrawError::None};
  // The draw for DrawWithoutMaterial and VertexAttributeMissing, the
  // material for TextureNotTexture2D, MaterialFailed and
  // MaterialBufferFailed, otherwise 0.
  std::uint32_t index{};
  // Set for MaterialFailed.
  materials::MaterialError material_error{materials::MaterialError::None};
  // The parameter, the name create_material gave, or the attribute.
  std::string name{};
  std::string detail{};
  [[nodiscard]] bool ok() const noexcept { return error == ModelDrawError::None; }
};

[[nodiscard]] ModelDrawFailure create_model_draw(rhi::Device& device, const ModelResources& model,
                                                 const std::filesystem::path& shader_directory,
                                                 materials::MaterialBuffer& material_buffer,
                                                 rhi::Format color_format, ModelDraw& out);

void record_model_draw(rhi::CommandList& list, const ModelDraw& draw, const ModelResources& model,
                       const math::Matrix4f& object_to_world);

struct SceneBlocks {
  rhi::Ref<rhi::Buffer> view{};
  rhi::Ref<rhi::Buffer> fixture_light{};
  // The values the two buffers were made with.
  scene::View view_value{};
  scene::FixtureLight fixture_light_value{};
};

[[nodiscard]] rhi::Status<rhi::ResourceError> update_scene_blocks(rhi::Device& device,
                                                                  const scene::View& view,
                                                                  const scene::FixtureLight& light,
                                                                  SceneBlocks& blocks);

// Precondition: update_scene_blocks succeeded on `blocks`.
void bind_scene_blocks(rhi::CommandList& list, const SceneBlocks& blocks);

} // namespace bw::rendering

#endif
