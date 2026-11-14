#include "bw/rendering/postprocess/bloom.hpp"

#include "bw/rendering/graph/access.hpp"
#include "bw/rendering/graph/builder.hpp"
#include "bw/rendering/graph/declaration.hpp"
#include "bw/rendering/postprocess/post_shaders.hpp"
#include "bw/rendering/rhi/command_list.hpp"
#include "bw/rendering/rhi/device.hpp"
#include "bw/rendering/rhi/format.hpp"
#include "bw/rendering/rhi/pipeline_states.hpp"
#include "bw/rendering/rhi/resources.hpp"
#include "post_support.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

// Responsibility: the out-of-line part of bw/rendering/postprocess/bloom.hpp.
//
// The chain is eleven raster passes, each drawing the module's full-screen
// triangle into one frame-only level: six downsamples D_0 to D_5, then five
// upsamples U_4 to U_0, U_k reading D_k and U_(k+1) (D_5 for U_4). A pass
// never reads what it writes, so the upsamples write textures of their own.
// U_5 is never written: the first pass up reads D_5 itself and scales it
// by one sixth there, as every pass scales the level it adds (bloom.hpp,
// "The filter", Up). Each pass makes its source's sampled views while it
// records, since the levels exist only while the frame executes; once one is
// refused, that pass and every bloom pass after it in the frame record
// nothing, record_failure() names the view, and the next declare_bloom
// returns it.
namespace bw::postprocess {

namespace {

constexpr std::size_t kUpsamples = kBloomLevels - 1U;

constexpr std::array<std::string_view, kBloomLevels> kDownNames{
    "bloom down 0", "bloom down 1", "bloom down 2", "bloom down 3", "bloom down 4", "bloom down 5"};
constexpr std::array<std::string_view, kUpsamples> kUpNames{
    "bloom up 0", "bloom up 1", "bloom up 2", "bloom up 3", "bloom up 4"};

BloomFailure failed(BloomError error, std::string detail) {
  return BloomFailure{error, std::move(detail)};
}

[[nodiscard]] shaders::Float2 inverse_size(rhi::Extent2 size) noexcept {
  return shaders::Float2{1.0F / static_cast<float>(size.width),
                         1.0F / static_cast<float>(size.height)};
}

} // namespace

struct Bloom::State {
  // The record function of one pass: downsample `level`, or upsample into
  // U_level.
  struct Record {
    State* state{};
    std::uint32_t level{};
    bool up{};
    void operator()(graph::RecordContext& context) const;
  };

  rhi::Device* device{};
  rhi::Ref<rhi::Sampler> sampler{};
  rhi::Ref<rhi::GraphicsPipeline> downsample{};
  rhi::Ref<rhi::GraphicsPipeline> upsample{};
  // A view a pass could not make; the next declare_bloom returns it.
  std::string view_failure{};
  // A pass of this frame was refused a view: the passes after it record
  // nothing.
  bool refused{};
  // The frame's textures: scene colour, the levels down and the sums up.
  graph::TextureHandle scene_color{};
  rhi::Extent2 size{};
  std::array<graph::TextureHandle, kBloomLevels> down{};
  std::array<graph::TextureHandle, kUpsamples> up{};
  std::array<Record, kBloomLevels + kUpsamples> records{};
};

struct BloomAccess {
  using State = Bloom::State;
  [[nodiscard]] static Bloom::State& state(Bloom& bloom) noexcept { return *bloom.state_; }
};

Bloom::Bloom() : state_(std::make_unique<State>()) {}
Bloom::Bloom(Bloom&&) noexcept = default;
Bloom& Bloom::operator=(Bloom&&) noexcept = default;
Bloom::~Bloom() = default;

BloomFailure Bloom::record_failure() const {
  if (state_->view_failure.empty()) {
    return {};
  }
  return failed(BloomError::ViewFailed, state_->view_failure);
}

BloomFailure create_bloom(rhi::Device& device, const std::filesystem::path& shader_directory,
                          Bloom& out) {
  out = Bloom{};
  Bloom built;
  BloomAccess::State& state = BloomAccess::state(built);
  state.device = &device;
  const auto error_of = [](detail::MakeStep step) {
    switch (step) {
    case detail::MakeStep::Artifact:
      return BloomError::ArtifactFailed;
    case detail::MakeStep::State:
      return BloomError::StateFailed;
    case detail::MakeStep::Pipeline:
    case detail::MakeStep::None:
      break;
    }
    return BloomError::PipelineFailed;
  };
  detail::MakeFailure made =
      detail::make_fullscreen_pipeline(device, shader_directory, kBloomDownsampleShader,
                                       kBloomFormat, "bloom downsample", state.downsample);
  if (made.ok()) {
    made = detail::make_fullscreen_pipeline(device, shader_directory, kBloomUpsampleShader,
                                            kBloomFormat, "bloom upsample", state.upsample);
  }
  if (made.ok()) {
    made = detail::make_clamped_sampler(device, state.sampler);
  }
  if (!made.ok()) {
    return failed(error_of(made.step), made.detail);
  }
  out = std::move(built);
  return {};
}

// The downsample, with the firefly average on the first level, the
// progressive tent upsample, and six R11G11B10 levels whose sum carries
// one sixth.
void Bloom::State::Record::operator()(graph::RecordContext& context) const {
  if (state->refused) {
    return;
  }
  rhi::CommandList& list = context.list();
  const auto view_of = [this, &context](graph::TextureHandle handle,
                                        rhi::Ref<rhi::ShaderResourceView>& out) {
    rhi::ShaderResourceViewResult viewed = state->device->create_shader_resource_view(
        rhi::Ref<rhi::Texture>(context.texture(handle)), rhi::TextureViewDescription{});
    if (!viewed.ok()) {
      state->view_failure = "a bloom level's sampled view: " + std::string(viewed.detail());
      state->refused = true;
      return false;
    }
    out = std::move(viewed.value());
    return true;
  };
  if (!up) {
    const graph::TextureHandle source = level == 0U ? state->scene_color : state->down[level - 1U];
    const rhi::Extent2 source_size =
        level == 0U ? state->size : bloom_level_extent(state->size, level - 1U);
    rhi::Ref<rhi::ShaderResourceView> viewed;
    if (!view_of(source, viewed)) {
      return;
    }
    PostBloomDownsample block{};
    block.source = viewed->texture_index();
    block.sampler = state->sampler->index();
    block.source_texel = inverse_size(source_size);
    block.firefly = level == 0U ? 1U : 0U;
    list.set_graphics_pipeline(*state->downsample);
    list.set_draw_block(block);
    list.draw(rhi::DrawArguments{3, 1, 0, 0});
    return;
  }
  const graph::TextureHandle below =
      level + 1U == kUpsamples ? state->down[kBloomLevels - 1U] : state->up[level + 1U];
  rhi::Ref<rhi::ShaderResourceView> level_view;
  rhi::Ref<rhi::ShaderResourceView> below_view;
  if (!view_of(state->down[level], level_view) || !view_of(below, below_view)) {
    return;
  }
  PostBloomUpsample block{};
  block.level = level_view->texture_index();
  block.below = below_view->texture_index();
  block.sampler = state->sampler->index();
  constexpr float kLevelShare = 1.0F / static_cast<float>(kBloomLevels);
  block.level_scale = kLevelShare;
  // The first pass up reads D_5, not yet scaled; the others read U_(k+1).
  block.below_scale = level + 1U == kUpsamples ? kLevelShare : 1.0F;
  block.below_texel = inverse_size(bloom_level_extent(state->size, level + 1U));
  block.radius = kBloomTentRadius;
  list.set_graphics_pipeline(*state->upsample);
  list.set_draw_block(block);
  list.draw(rhi::DrawArguments{3, 1, 0, 0});
}

// Six levels down, then up from the smallest, summing as it goes.
BloomFailure declare_bloom(Bloom& bloom, rhi::Device& device, graph::FrameGraph& graph,
                           graph::TextureHandle scene_color, rhi::Extent2 size,
                           graph::TextureHandle& result) {
  BloomAccess::State& state = BloomAccess::state(bloom);
  state.device = &device;
  if (!state.view_failure.empty()) {
    std::string detail = std::move(state.view_failure);
    state.view_failure.clear();
    return failed(BloomError::ViewFailed, std::move(detail));
  }
  state.refused = false;
  state.scene_color = scene_color;
  state.size = size;
  const auto level_texture = [&graph, size](std::uint32_t level, std::string_view name) {
    graph::FrameTextureDescription description{};
    description.extent = bloom_level_extent(size, level);
    description.format = kBloomFormat;
    description.debug_name = name;
    return graph.create_texture(description);
  };
  for (std::uint32_t level = 0; level < kBloomLevels; ++level) {
    state.down[level] = level_texture(level, kDownNames[level]);
    state.records[level] = BloomAccess::State::Record{&state, level, false};
    const graph::TextureHandle source = level == 0U ? scene_color : state.down[level - 1U];
    const std::array<graph::TextureUse, 2> textures{
        graph::TextureUse{source, graph::Access::Sampled, graph::Contents::Keep, 0},
        graph::TextureUse{state.down[level], graph::Access::Attachment, graph::Contents::Overwrite,
                          0}};
    graph.add_pass(graph::PassDeclaration{
        kDownNames[level], graph::PassKind::Raster, textures, {}, state.records[level]});
  }
  for (std::uint32_t step = 0; step < kUpsamples; ++step) {
    const std::uint32_t level = static_cast<std::uint32_t>(kUpsamples) - 1U - step;
    state.up[level] = level_texture(level, kUpNames[level]);
    BloomAccess::State::Record& record = state.records[kBloomLevels + level];
    record = BloomAccess::State::Record{&state, level, true};
    const graph::TextureHandle below =
        level + 1U == kUpsamples ? state.down[kBloomLevels - 1U] : state.up[level + 1U];
    const std::array<graph::TextureUse, 3> textures{
        graph::TextureUse{state.down[level], graph::Access::Sampled, graph::Contents::Keep, 0},
        graph::TextureUse{below, graph::Access::Sampled, graph::Contents::Keep, 0},
        graph::TextureUse{state.up[level], graph::Access::Attachment, graph::Contents::Overwrite,
                          0}};
    graph.add_pass(
        graph::PassDeclaration{kUpNames[level], graph::PassKind::Raster, textures, {}, record});
  }
  result = state.up[0];
  return {};
}

} // namespace bw::postprocess
