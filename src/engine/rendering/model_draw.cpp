#include "bw/rendering/model_draw.hpp"

#include "bw/math/matrix/matrix4.hpp"
#include "bw/rendering/geometry/mesh_resources.hpp"
#include "bw/rendering/geometry/vertex_streams.hpp"
#include "bw/rendering/materials/lit_template.hpp"
#include "bw/rendering/materials/material.hpp"
#include "bw/rendering/materials/material_buffer.hpp"
#include "bw/rendering/materials/shading.hpp"
#include "bw/rendering/model_resources.hpp"
#include "bw/rendering/rhi/access.hpp"
#include "bw/rendering/rhi/command_list.hpp"
#include "bw/rendering/rhi/device.hpp"
#include "bw/rendering/rhi/format.hpp"
#include "bw/rendering/rhi/pipeline_states.hpp"
#include "bw/rendering/rhi/resources.hpp"
#include "bw/rendering/rhi/result.hpp"
#include "bw/rendering/scene/fixture_light.hpp"
#include "bw/rendering/scene/forward_pass.hpp"
#include "bw/rendering/scene/view.hpp"
#include "bw/rendering/shaders/artifact_file.hpp"
#include "bw/rendering/shaders/parameter_block.hpp"
#include "bw/rendering/shaders/permutation.hpp"
#include "bw/rendering/texture_asset.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Responsibility: the out-of-line part of bw/rendering/model_draw.hpp.
//
// Contract, with the readings the header leaves to the implementation:
//   - The one sampler is trilinear and is made before the first material
//     (its failure is StateFailed, index 0); the rasterizer, depth and blend
//     states are made once, before the first pipeline, whatever blend modes
//     the model uses.
//   - The forward lit shaders have the lit template's domain
//     (forward_pass.hpp, contract 1), so a material's permutation is a lit
//     permutation and the vertex types checked are the lit template's
//     (lit_template.hpp, contract 2). The lit template is the one template
//     material_templates() holds.
//   - Translucent and additive blend the alpha channel with the same
//     factors as the color.
//   - A draw whose material index is kNoMaterial or not below the model's
//     material count is DrawWithoutMaterial.
namespace bw::rendering {

namespace {

template <class Code>
ModelDrawFailure failed(ModelDrawError error, std::uint32_t index, std::string_view what, Code code,
                        std::string_view detail) {
  ModelDrawFailure failure{};
  failure.error = error;
  failure.index = index;
  failure.detail = std::string(what) + ": error " + std::to_string(static_cast<unsigned>(code));
  if (!detail.empty()) {
    failure.detail += ": ";
    failure.detail += detail;
  }
  return failure;
}

[[nodiscard]] std::string_view attribute_name(geometry::VertexAttribute attribute) noexcept {
  static constexpr std::array<std::string_view, geometry::kVertexAttributeCount> kNames{
      "Position",  "Normal",    "Tangent",   "Color0",    "Color1",    "Color2",
      "TexCoord0", "TexCoord1", "TexCoord2", "TexCoord3", "BoneSlots", "BoneWeights"};
  return kNames[static_cast<std::size_t>(attribute)];
}

// The element type the lit template decodes an attribute as.
[[nodiscard]] geometry::VertexElementType lit_type(geometry::VertexAttribute attribute) noexcept {
  switch (attribute) {
  case geometry::VertexAttribute::Position:
    return materials::kLitPositionType;
  case geometry::VertexAttribute::Normal:
  case geometry::VertexAttribute::Tangent:
    return materials::kLitFrameType;
  default:
    return materials::kLitTexCoordType;
  }
}

// Whether the layout carries the attribute with the type the template
// decodes.
[[nodiscard]] bool carries(const geometry::VertexStreamLayout& layout,
                           geometry::VertexAttribute attribute) noexcept {
  for (std::size_t i = 0; i < layout.attribute_count; ++i) {
    if (layout.attributes[i].attribute == attribute) {
      return layout.attributes[i].type == lit_type(attribute);
    }
  }
  return false;
}

ModelDrawFailure create_states(rhi::Device& device, ModelDraw& built) {
  rhi::RasterizerStateResult rasterizer = device.rasterizer_state(rhi::RasterizerDescription{});
  if (!rasterizer.ok()) {
    return failed(ModelDrawError::StateFailed, 0, "rasterizer state", rasterizer.error(),
                  rasterizer.detail());
  }
  built.rasterizer = std::move(rasterizer.value());
  for (const bool write : {true, false}) {
    rhi::DepthStencilDescription depth{};
    depth.depth_write = write;
    depth.depth_test = rhi::CompareFunction::LessEqual;
    rhi::DepthStencilStateResult state = device.depth_stencil_state(depth);
    if (!state.ok()) {
      return failed(ModelDrawError::StateFailed, 0, "depth stencil state", state.error(),
                    state.detail());
    }
    (write ? built.depth_write : built.depth_read) = std::move(state.value());
  }
  // None, source alpha over, added.
  const std::array<std::pair<rhi::BlendFactor, rhi::BlendFactor>, 3> factors{{
      {rhi::BlendFactor::One, rhi::BlendFactor::Zero},
      {rhi::BlendFactor::SourceAlpha, rhi::BlendFactor::InverseSourceAlpha},
      {rhi::BlendFactor::SourceAlpha, rhi::BlendFactor::One},
  }};
  const std::array<rhi::Ref<rhi::BlendState>*, 3> targets{&built.blend_none, &built.blend_over,
                                                          &built.blend_add};
  for (std::size_t i = 0; i < factors.size(); ++i) {
    rhi::BlendDescription blend{};
    blend.targets[0].color_source = factors[i].first;
    blend.targets[0].color_destination = factors[i].second;
    blend.targets[0].alpha_source = factors[i].first;
    blend.targets[0].alpha_destination = factors[i].second;
    rhi::BlendStateResult state = device.blend_state(blend);
    if (!state.ok()) {
      return failed(ModelDrawError::StateFailed, 0, "blend state", state.error(), state.detail());
    }
    *targets[i] = std::move(state.value());
  }
  return {};
}

// Step b: each material, its textures resolved, made and placed.
ModelDrawFailure create_materials(rhi::Device& device, const ModelResources& model,
                                  materials::MaterialBuffer& material_buffer, ModelDraw& built) {
  const std::span<const std::string_view> parameters = model_texture_parameters();
  for (std::uint32_t m = 0; m < model.materials.size(); ++m) {
    std::vector<materials::MaterialTextureBinding> bindings;
    for (const std::string_view parameter : parameters) {
      const ModelTexture* texture = model.texture(m, parameter);
      if (texture == nullptr) {
        continue;
      }
      if (texture->dimension != TextureAssetDimension::Texture2D) {
        ModelDrawFailure failure{};
        failure.error = ModelDrawError::TextureNotTexture2D;
        failure.index = m;
        failure.name = std::string(parameter);
        failure.detail = std::string(texture->id.view()) + " is not a 2D texture";
        return failure;
      }
      bindings.push_back({parameter, texture->view->texture_index()});
    }
    materials::Material material;
    const materials::MaterialFailure made = materials::create_material(
        model.materials[m].view, bindings, built.sampler->index(), material);
    if (!made.ok()) {
      ModelDrawFailure failure{};
      failure.error = ModelDrawError::MaterialFailed;
      failure.index = m;
      failure.material_error = made.error;
      failure.name = made.name;
      failure.detail = std::string(model.materials[m].asset.id().view()) +
                       ": create_material error " +
                       std::to_string(static_cast<unsigned>(made.error)) + " " + made.name;
      return failure;
    }
    const materials::MaterialInstance instance(material);
    materials::MaterialSlot slot{};
    const materials::MaterialBufferFailure placed = material_buffer.add(device, instance, slot);
    if (!placed.ok()) {
      ModelDrawFailure failure = failed(ModelDrawError::MaterialBufferFailed, m, "material buffer",
                                        placed.error, placed.detail);
      return failure;
    }
    built.instances.push_back(instance);
    built.slots.push_back(slot);
  }
  return {};
}

// Step d, one pair: the two artifacts of the permutation, read here and
// dropped after the pipeline is made (a pipeline keeps their hashes only).
ModelDrawFailure create_pipeline(rhi::Device& device, const std::filesystem::path& shader_directory,
                                 shaders::PermutationId permutation, materials::BlendMode blend,
                                 rhi::Format color_format, ModelDraw& built) {
  shaders::ArtifactFile vertex;
  shaders::ArtifactFile pixel;
  for (const auto& [name, file] : {std::pair{scene::kForwardLitVertexShader, &vertex},
                                   std::pair{scene::kForwardLitPixelShader, &pixel}}) {
    const shaders::ArtifactFileFailure read =
        shaders::read_artifact_file(shader_directory, name, permutation, *file);
    if (!read.ok()) {
      ModelDrawFailure failure{};
      failure.error = ModelDrawError::ArtifactFailed;
      failure.detail = shaders::artifact_file_failure_text(read, name);
      return failure;
    }
  }
  const bool blended =
      blend == materials::BlendMode::Translucent || blend == materials::BlendMode::Additive;
  rhi::GraphicsPipelineDescription description{};
  description.vertex_shader = &vertex.artifact;
  description.pixel_shader = &pixel.artifact;
  description.rasterizer = &*built.rasterizer;
  description.depth_stencil = blended ? &*built.depth_read : &*built.depth_write;
  description.blend = blend == materials::BlendMode::Translucent ? &*built.blend_over
                      : blend == materials::BlendMode::Additive  ? &*built.blend_add
                                                                 : &*built.blend_none;
  description.targets.color_formats[0] = color_format;
  description.targets.color_count = 1;
  description.targets.depth_stencil_format = kModelDepthFormat;
  description.targets.sample_count = 1;
  description.primitive = rhi::PrimitiveType::TriangleList;
  description.debug_name = "model draw forward lit";
  const rhi::Status<rhi::PipelineError> precompiled =
      device.precompile(std::span<const rhi::GraphicsPipelineDescription>{&description, 1}, {});
  if (!precompiled.ok()) {
    return failed(ModelDrawError::PipelineFailed, 0, "pipeline precompile", precompiled.error(),
                  precompiled.detail());
  }
  rhi::GraphicsPipelineResult pipeline = device.graphics_pipeline(description);
  if (!pipeline.ok()) {
    return failed(ModelDrawError::PipelineFailed, 0, "graphics pipeline", pipeline.error(),
                  pipeline.detail());
  }
  built.pipelines.push_back(ModelPipeline{permutation, blend, std::move(pipeline.value())});
  return {};
}

// A block's bytes as they are uploaded: each member's value at its offset,
// the padding around members zero, so equal values give equal bytes.
template <class Block>
[[nodiscard]] std::array<std::byte, sizeof(Block)> block_bytes(const Block& value) noexcept {
  std::array<std::byte, sizeof(Block)> image{};
  std::memcpy(image.data(), &value, sizeof(Block));
  std::array<std::byte, sizeof(Block)> out{};
  for (const shaders::MemberLayout& member : shaders::BlockLayoutOf<Block>::value.members) {
    const std::uint32_t size = shaders::place_in_hlsl(member, member.offset).size;
    std::memcpy(out.data() + member.offset, image.data() + member.offset, size);
  }
  return out;
}

// A uniform buffer holding block_bytes of one block.
template <class Block>
rhi::Status<rhi::ResourceError> make_block_buffer(rhi::Device& device, const Block& value,
                                                  std::string_view name,
                                                  rhi::Ref<rhi::Buffer>& out) {
  rhi::BufferDescription description{};
  description.size = sizeof(Block);
  description.usage = rhi::BufferUsage::Uniform;
  description.initial_access =
      rhi::Access::ShaderResourceNonPixel | rhi::Access::ShaderResourcePixel;
  description.debug_name = name;
  const std::array<std::byte, sizeof(Block)> bytes = block_bytes(value);
  rhi::BufferResult created = device.create_buffer(description, bytes);
  if (!created.ok()) {
    return {created.error(), created.detail()};
  }
  out = std::move(created.value());
  return {};
}

// Whether two values of a block upload the same bytes. The order of the two
// does not matter.
template <class Block>
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
[[nodiscard]] bool same_bytes(const Block& a, const Block& b) noexcept {
  return block_bytes(a) == block_bytes(b);
}

} // namespace

// An allocation failure while the list is first built ends the program, as
// any exception leaving a noexcept function does.
// NOLINTNEXTLINE(bugprone-exception-escape)
std::span<const std::string_view> model_texture_parameters() noexcept {
  // Every template's texture parameter names, each once, in template order.
  static const std::vector<std::string_view> kNames = [] {
    std::vector<std::string_view> names;
    for (const materials::MaterialTemplate* material_template : materials::material_templates()) {
      for (const materials::MaterialTextureParameter& texture : material_template->textures) {
        if (std::find(names.begin(), names.end(), texture.name) == names.end()) {
          names.push_back(texture.name);
        }
      }
    }
    return names;
  }();
  return kNames;
}

ModelDrawFailure create_model_draw(rhi::Device& device, const ModelResources& model,
                                   const std::filesystem::path& shader_directory,
                                   materials::MaterialBuffer& material_buffer,
                                   rhi::Format color_format, ModelDraw& out) {
  out = ModelDraw{};
  // a. Every draw names a material.
  for (std::uint32_t d = 0; d < model.mesh.draws.size(); ++d) {
    const std::uint32_t material = model.mesh.draws[d].material;
    if (material == geometry::kNoMaterial || material >= model.materials.size()) {
      ModelDrawFailure failure{};
      failure.error = ModelDrawError::DrawWithoutMaterial;
      failure.index = d;
      failure.detail = "the draw names no material of the model";
      return failure;
    }
  }
  ModelDraw built;
  built.material_buffer = &material_buffer;
  rhi::SamplerDescription trilinear{};
  trilinear.filter = rhi::Filter::Trilinear;
  rhi::SamplerResult sampler = device.sampler(trilinear);
  if (!sampler.ok()) {
    return failed(ModelDrawError::StateFailed, 0, "sampler", sampler.error(), sampler.detail());
  }
  built.sampler = std::move(sampler.value());
  // b. The materials.
  ModelDrawFailure failure = create_materials(device, model, material_buffer, built);
  if (!failure.ok()) {
    return failure;
  }
  // c. Each draw's group carries what its material's permutation reads.
  for (std::uint32_t d = 0; d < model.mesh.draws.size(); ++d) {
    const geometry::MeshDrawRange& draw = model.mesh.draws[d];
    const materials::MaterialInstance& instance = built.instances[draw.material];
    const std::uint32_t reads =
        instance.material_template()->vertex_attributes(instance.permutation());
    const geometry::VertexStreamLayout& layout = model.mesh.groups[draw.group].layout;
    for (std::uint32_t a = 0; a < geometry::kVertexAttributeCount; ++a) {
      const auto attribute = static_cast<geometry::VertexAttribute>(a);
      if ((reads & (1U << a)) != 0U && !carries(layout, attribute)) {
        ModelDrawFailure missing{};
        missing.error = ModelDrawError::VertexAttributeMissing;
        missing.index = d;
        missing.name = std::string(attribute_name(attribute));
        missing.detail = "group " + std::to_string(draw.group) + " lacks " + missing.name +
                         " with the type the lit template decodes";
        return missing;
      }
    }
  }
  // d. One pipeline per (permutation, blend mode), in first-use order.
  failure = create_states(device, built);
  if (!failure.ok()) {
    return failure;
  }
  for (const geometry::MeshDrawRange& draw : model.mesh.draws) {
    const materials::MaterialInstance& instance = built.instances[draw.material];
    std::size_t found = built.pipelines.size();
    for (std::size_t p = 0; p < built.pipelines.size(); ++p) {
      if (built.pipelines[p].permutation == instance.permutation() &&
          built.pipelines[p].blend_mode == instance.blend_mode()) {
        found = p;
      }
    }
    if (found == built.pipelines.size()) {
      failure = create_pipeline(device, shader_directory, instance.permutation(),
                                instance.blend_mode(), color_format, built);
      if (!failure.ok()) {
        return failure;
      }
    }
    built.draw_pipelines.push_back(static_cast<std::uint32_t>(found));
  }
  out = std::move(built);
  return {};
}

void record_model_draw(rhi::CommandList& list, const ModelDraw& draw, const ModelResources& model,
                       const math::Matrix4f& object_to_world) {
  scene::ForwardDraw block{};
  block.object_to_world = scene::shader_matrix(object_to_world);
  auto bound_group = static_cast<std::uint32_t>(model.mesh.groups.size());
  auto bound_pipeline = static_cast<std::uint32_t>(draw.pipelines.size());
  for (std::size_t d = 0; d < model.mesh.draws.size(); ++d) {
    const geometry::MeshDrawRange& range = model.mesh.draws[d];
    if (draw.draw_pipelines[d] != bound_pipeline) {
      bound_pipeline = draw.draw_pipelines[d];
      list.set_graphics_pipeline(*draw.pipelines[bound_pipeline].pipeline);
    }
    const geometry::MeshGroupResources& group = model.mesh.groups[range.group];
    if (range.group != bound_group) {
      list.set_index_buffer(*group.indices, rhi::IndexFormat::Uint16, 0);
      bound_group = range.group;
    }
    block.streams = group.block;
    block.streams.base_vertex = range.base_vertex;
    block.material = draw.material_buffer->reference(draw.slots[range.material]);
    list.set_draw_block(block);
    list.draw_indexed(rhi::DrawIndexedArguments{range.index_count, 1, range.first_index, 0, 0});
  }
}

rhi::Status<rhi::ResourceError> update_scene_blocks(rhi::Device& device, const scene::View& view,
                                                    const scene::FixtureLight& light,
                                                    SceneBlocks& blocks) {
  rhi::Ref<rhi::Buffer> view_buffer = blocks.view;
  rhi::Ref<rhi::Buffer> light_buffer = blocks.fixture_light;
  if (!view_buffer || !same_bytes(view, blocks.view_value)) {
    const rhi::Status<rhi::ResourceError> made =
        make_block_buffer(device, view, "scene view block", view_buffer);
    if (!made.ok()) {
      return made;
    }
  }
  if (!light_buffer || !same_bytes(light, blocks.fixture_light_value)) {
    const rhi::Status<rhi::ResourceError> made =
        make_block_buffer(device, light, "scene fixture light block", light_buffer);
    if (!made.ok()) {
      return made;
    }
  }
  blocks.view = std::move(view_buffer);
  blocks.fixture_light = std::move(light_buffer);
  blocks.view_value = view;
  blocks.fixture_light_value = light;
  return {};
}

void bind_scene_blocks(rhi::CommandList& list, const SceneBlocks& blocks) {
  list.set_static_block(scene::kViewSlot, *blocks.view, 0);
  list.set_static_block(scene::kFixtureLightSlot, *blocks.fixture_light, 0);
}

} // namespace bw::rendering
