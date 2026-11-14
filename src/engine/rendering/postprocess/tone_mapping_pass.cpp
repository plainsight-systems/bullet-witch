#include "bw/rendering/graph/access.hpp"
#include "bw/rendering/graph/builder.hpp"
#include "bw/rendering/graph/declaration.hpp"
#include "bw/rendering/postprocess/post_shaders.hpp"
#include "bw/rendering/postprocess/tone_mapping.hpp"
#include "bw/rendering/rhi/command_list.hpp"
#include "bw/rendering/rhi/device.hpp"
#include "bw/rendering/rhi/format.hpp"
#include "bw/rendering/rhi/pipeline_states.hpp"
#include "bw/rendering/rhi/resources.hpp"
#include "post_support.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>

// Responsibility: the ToneMapping object of bw/rendering/postprocess/
// tone_mapping.hpp (contracts 1, 4, 5 and 6): the pipeline for one target
// format, the sampler, and the one raster pass.
//
// The pass makes the sampled views of its five textures while it records:
// scene colour and the bloom result exist only while the frame executes, and
// the pass is given handles, not the owners' views. The table is viewed as a
// 3D texture. A refused view records no draw; record_failure() then names
// it until the next declare_tone_mapping returns it.
namespace bw::postprocess {

namespace {

// The noise offset repeats every eight frames (tone_mapping.hpp, contract 4).
constexpr std::uint32_t kDitherPeriod = 8;

ToneMappingFailure failed(ToneMappingError error, std::string detail) {
  return ToneMappingFailure{error, std::move(detail)};
}

// The radical inverse of index in the given base.
[[nodiscard]] float radical_inverse(std::uint32_t index, std::uint32_t base) noexcept {
  float result = 0.0F;
  const float inverse_base = 1.0F / static_cast<float>(base);
  float fraction = inverse_base;
  while (index > 0U) {
    result += static_cast<float>(index % base) * fraction;
    index /= base;
    fraction *= inverse_base;
  }
  return result;
}

} // namespace

struct ToneMapping::State {
  struct Record {
    State* state{};
    void operator()(graph::RecordContext& context) const;
  };

  rhi::Device* device{};
  rhi::Format target_format{rhi::Format::Unknown};
  rhi::Ref<rhi::Sampler> sampler{};
  rhi::Ref<rhi::GraphicsPipeline> pipeline{};
  std::string view_failure{};
  ToneMappingInputs inputs{};
  Record record{};
};

struct ToneMappingAccess {
  using State = ToneMapping::State;
  [[nodiscard]] static ToneMapping::State& state(ToneMapping& tone_mapping) noexcept {
    return *tone_mapping.state_;
  }
};

ToneMapping::ToneMapping() : state_(std::make_unique<State>()) {}
ToneMapping::ToneMapping(ToneMapping&&) noexcept = default;
ToneMapping& ToneMapping::operator=(ToneMapping&&) noexcept = default;
ToneMapping::~ToneMapping() = default;

ToneMappingFailure ToneMapping::record_failure() const {
  if (state_->view_failure.empty()) {
    return {};
  }
  return failed(ToneMappingError::ViewFailed, state_->view_failure);
}

ToneMappingFailure create_tone_mapping(rhi::Device& device,
                                       const std::filesystem::path& shader_directory,
                                       rhi::Format target_format, ToneMapping& out) {
  out = ToneMapping{};
  if (dither_amplitude(target_format) == 0.0F) {
    return failed(ToneMappingError::TargetFormatUnsupported,
                  "the tone mapping target is B8G8R8A8Unorm; a hardware-sRGB target would "
                  "encode twice");
  }
  ToneMapping built;
  ToneMappingAccess::State& state = ToneMappingAccess::state(built);
  state.device = &device;
  state.target_format = target_format;
  detail::MakeFailure made = detail::make_fullscreen_pipeline(
      device, shader_directory, kToneMapShader, target_format, "tone mapping", state.pipeline);
  if (made.ok()) {
    made = detail::make_clamped_sampler(device, state.sampler);
  }
  if (!made.ok()) {
    switch (made.step) {
    case detail::MakeStep::Artifact:
      return failed(ToneMappingError::ArtifactFailed, made.detail);
    case detail::MakeStep::State:
      return failed(ToneMappingError::StateFailed, made.detail);
    case detail::MakeStep::Pipeline:
    case detail::MakeStep::None:
      break;
    }
    return failed(ToneMappingError::PipelineFailed, made.detail);
  }
  out = std::move(built);
  return {};
}

// The dither is one code value of the target; the noise offset is the
// radical inverses in bases 2 and 3 of the frame index modulo 8. The pass
// takes B8G8R8A8Unorm alone (tone_mapping.hpp, contract 1).
void ToneMapping::State::Record::operator()(graph::RecordContext& context) const {
  const ToneMappingInputs& in = state->inputs;
  const std::array<graph::TextureHandle, 5> handles{in.scene_color, in.bloom, in.factor, in.adapted,
                                                    in.table};
  std::array<rhi::Ref<rhi::ShaderResourceView>, 5> views{};
  for (std::size_t i = 0; i < handles.size(); ++i) {
    rhi::TextureViewDescription description{};
    if (i == 4U) {
      description.dimension = rhi::TextureDimension::Texture3D;
    }
    rhi::ShaderResourceViewResult viewed = state->device->create_shader_resource_view(
        rhi::Ref<rhi::Texture>(context.texture(handles[i])), description);
    if (!viewed.ok()) {
      state->view_failure = "a tone mapping input's sampled view: " + std::string(viewed.detail());
      return;
    }
    views[i] = std::move(viewed.value());
  }
  const std::uint32_t frame = in.frame_index % kDitherPeriod;
  PostToneMap block{};
  block.scene_color = views[0]->texture_index();
  block.bloom = views[1]->texture_index();
  block.factor = views[2]->texture_index();
  block.adapted = views[3]->texture_index();
  block.table = views[4]->texture_index();
  block.sampler = state->sampler->index();
  block.bloom_intensity = in.bloom_intensity;
  block.vignette = in.vignette;
  block.target_size =
      shaders::Float2{static_cast<float>(in.size.width), static_cast<float>(in.size.height)};
  block.dither_offset = shaders::Float2{radical_inverse(frame, 2U), radical_inverse(frame, 3U)};
  block.dither_amplitude = dither_amplitude(state->target_format);
  rhi::CommandList& list = context.list();
  list.set_graphics_pipeline(*state->pipeline);
  list.set_draw_block(block);
  list.draw(rhi::DrawArguments{3, 1, 0, 0});
}

// One pass reads scene colour, bloom, the pre-exposure factor, the adapted
// value and the table into the output.
ToneMappingFailure declare_tone_mapping(ToneMapping& tone_mapping, rhi::Device& device,
                                        graph::FrameGraph& graph, const ToneMappingInputs& inputs) {
  ToneMappingAccess::State& state = ToneMappingAccess::state(tone_mapping);
  state.device = &device;
  if (!state.view_failure.empty()) {
    std::string detail = std::move(state.view_failure);
    state.view_failure.clear();
    return failed(ToneMappingError::ViewFailed, std::move(detail));
  }
  state.inputs = inputs;
  state.record = ToneMappingAccess::State::Record{&state};
  const std::array<graph::TextureUse, 6> textures{
      graph::TextureUse{inputs.target, graph::Access::Attachment, graph::Contents::Overwrite, 0},
      graph::TextureUse{inputs.scene_color, graph::Access::Sampled, graph::Contents::Keep, 0},
      graph::TextureUse{inputs.bloom, graph::Access::Sampled, graph::Contents::Keep, 0},
      graph::TextureUse{inputs.factor, graph::Access::Sampled, graph::Contents::Keep, 0},
      graph::TextureUse{inputs.adapted, graph::Access::Sampled, graph::Contents::Keep, 0},
      graph::TextureUse{inputs.table, graph::Access::Sampled, graph::Contents::Keep, 0}};
  graph.add_pass(
      graph::PassDeclaration{"tone mapping", graph::PassKind::Raster, textures, {}, state.record});
  return {};
}

} // namespace bw::postprocess
