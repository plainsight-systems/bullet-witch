#include "bw/rendering/graph/access.hpp"
#include "bw/rendering/graph/builder.hpp"
#include "bw/rendering/graph/declaration.hpp"
#include "bw/rendering/postprocess/exposure.hpp"
#include "bw/rendering/postprocess/post_shaders.hpp"
#include "bw/rendering/rhi/access.hpp"
#include "bw/rendering/rhi/command_list.hpp"
#include "bw/rendering/rhi/device.hpp"
#include "bw/rendering/rhi/format.hpp"
#include "bw/rendering/rhi/pipeline_states.hpp"
#include "bw/rendering/rhi/resources.hpp"
#include "post_support.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <utility>

// Responsibility: the Exposure object of bw/rendering/postprocess/
// exposure.hpp (contracts 6 to 10): the two texels, their views and
// pre-exposure blocks, and the two compute passes that meter a frame and
// write its adapted exposure.
//
// The histogram pass records two dispatches of the histogram kernel, one
// group in clear mode, then one thread per sample in accumulate mode. The
// Metal backend records a pass's dispatches on one serial compute encoder,
// where each dispatch completes before the next begins; a backend whose API
// needs a barrier between dependent dispatches inserts it itself, as the
// depth hierarchy's build already requires. The histogram is a frame-only
// buffer; both passes make its views, and scene colour's, while they record,
// since those exist only while the frame executes. A refused view records
// no adaptation, so the written texel keeps what it held; record_failure()
// then names the view until the next begin_exposure_frame returns it.
//
// The texels swap at begin_exposure_frame when the frame before declared
// its exposure and was not dropped: the texel that frame wrote becomes the
// one read.
namespace bw::postprocess {

namespace {

// Thread group of post_histogram.hlsl: one sample per thread.
constexpr std::uint32_t kHistogramGroupSize = 8;
// Each bucket is a low word and a carry word (bw/postprocess/exposure.hlsli).
constexpr std::uint64_t kHistogramBytes = std::uint64_t{kHistogramBuckets} * 8U;

ExposureFailure failed(ExposureError error, std::string detail) {
  return ExposureFailure{error, std::move(detail)};
}

} // namespace

struct Exposure::State {
  // The record function of one of the two passes.
  struct Record {
    State* state{};
    bool adaptation{};
    void operator()(graph::RecordContext& context) const;
  };

  rhi::Device* device{};
  rhi::Ref<rhi::ComputePipeline> histogram{};
  rhi::Ref<rhi::ComputePipeline> adaptation{};
  std::array<rhi::Ref<rhi::Texture>, 2> texels{};
  std::array<rhi::Ref<rhi::ShaderResourceView>, 2> sampled{};
  std::array<rhi::Ref<rhi::UnorderedAccessView>, 2> storage{};
  // Texel i's PreExposure block.
  std::array<rhi::Ref<rhi::Buffer>, 2> blocks{};
  // The texel the next frame reads as its factor.
  std::size_t read{};
  // A frame declared its exposure since the last begin and was not dropped.
  bool declared{};
  // A view a pass could not make; begin_exposure_frame returns it.
  std::string view_failure{};
  // The histogram pass recorded its dispatches this frame.
  bool metered{};
  // The frame's inputs.
  ExposureFrame frame{};
  graph::TextureHandle scene_color{};
  graph::BufferHandle histogram_buffer{};
  rhi::Extent2 size{};
  ExposureInputs inputs{};
  std::array<Record, 2> records{};
};

struct ExposureAccess {
  using State = Exposure::State;
  [[nodiscard]] static Exposure::State& state(Exposure& exposure) noexcept {
    return *exposure.state_;
  }
};

Exposure::Exposure() : state_(std::make_unique<State>()) {}
Exposure::Exposure(Exposure&&) noexcept = default;
Exposure& Exposure::operator=(Exposure&&) noexcept = default;
Exposure::~Exposure() = default;

void Exposure::frame_dropped() noexcept { state_->declared = false; }

ExposureFailure Exposure::record_failure() const {
  if (state_->view_failure.empty()) {
    return {};
  }
  return failed(ExposureError::ViewFailed, state_->view_failure);
}

// The texels start at kInitialExposure (exposure.hpp).
ExposureFailure create_exposure(rhi::Device& device, const std::filesystem::path& shader_directory,
                                Exposure& out) {
  out = Exposure{};
  Exposure built;
  ExposureAccess::State& state = ExposureAccess::state(built);
  state.device = &device;
  for (const auto& [shader, pipeline] : {std::pair{kHistogramShader, &state.histogram},
                                         std::pair{kAdaptationShader, &state.adaptation}}) {
    const detail::MakeFailure made =
        detail::make_compute_pipeline(device, shader_directory, shader, *pipeline);
    if (!made.ok()) {
      return failed(made.step == detail::MakeStep::Artifact ? ExposureError::ArtifactFailed
                                                            : ExposureError::PipelineFailed,
                    made.detail);
    }
  }
  const float initial = kInitialExposure;
  const std::array<rhi::SubresourceData, 1> texel_data{
      rhi::SubresourceData{std::as_bytes(std::span<const float, 1>{&initial, 1}), 4U, 0U}};
  for (std::size_t t = 0; t < 2U; ++t) {
    rhi::TextureDescription description{};
    description.extent = rhi::Extent3{1, 1, 1};
    description.format = kExposureFormat;
    description.usage = rhi::TextureUsage::ShaderResource | rhi::TextureUsage::UnorderedAccess;
    description.initial_access = rhi::kShaderResourceMask;
    description.debug_name = t == 0U ? "exposure texel 0" : "exposure texel 1";
    rhi::TextureResult texture = device.create_texture(description, texel_data);
    if (!texture.ok()) {
      return failed(ExposureError::TextureFailed,
                    detail::result_text(description.debug_name, texture.error(), texture.detail()));
    }
    state.texels[t] = std::move(texture.value());
    rhi::ShaderResourceViewResult sampled =
        device.create_shader_resource_view(state.texels[t], rhi::TextureViewDescription{});
    if (!sampled.ok()) {
      return failed(
          ExposureError::ViewFailed,
          detail::result_text("exposure texel's view", sampled.error(), sampled.detail()));
    }
    state.sampled[t] = std::move(sampled.value());
    rhi::UnorderedAccessViewResult storage =
        device.create_unordered_access_view(state.texels[t], rhi::TextureViewDescription{});
    if (!storage.ok()) {
      return failed(
          ExposureError::ViewFailed,
          detail::result_text("exposure texel's storage view", storage.error(), storage.detail()));
    }
    state.storage[t] = std::move(storage.value());

    PreExposure block{};
    block.factor = state.sampled[t]->texture_index();
    std::array<std::byte, sizeof(PreExposure)> bytes{};
    std::memcpy(bytes.data(), &block, sizeof(PreExposure));
    rhi::BufferDescription buffer{};
    buffer.size = sizeof(PreExposure);
    buffer.usage = rhi::BufferUsage::Uniform;
    buffer.initial_access = rhi::Access::ShaderResourceNonPixel | rhi::Access::ShaderResourcePixel;
    buffer.debug_name = t == 0U ? "pre-exposure block 0" : "pre-exposure block 1";
    rhi::BufferResult created = device.create_buffer(buffer, bytes);
    if (!created.ok()) {
      return failed(ExposureError::BufferFailed,
                    detail::result_text(buffer.debug_name, created.error(), created.detail()));
    }
    state.blocks[t] = std::move(created.value());
  }
  out = std::move(built);
  return {};
}

// The light buffer holds the previous frame's exposure pre-applied.
ExposureFailure begin_exposure_frame(Exposure& exposure, rhi::Device& device,
                                     graph::FrameGraph& graph, ExposureFrame& out) {
  ExposureAccess::State& state = ExposureAccess::state(exposure);
  state.device = &device;
  if (!state.view_failure.empty()) {
    std::string detail = std::move(state.view_failure);
    state.view_failure.clear();
    // The frame that failed wrote nothing, so its texel is not read next.
    state.declared = false;
    return failed(ExposureError::ViewFailed, std::move(detail));
  }
  if (state.declared) {
    state.read = 1U - state.read;
    state.declared = false;
  }
  const std::size_t written = 1U - state.read;
  ExposureFrame frame{};
  frame.pre_exposure.factor = graph.import_texture(graph::ImportedTexture{
      state.texels[state.read].get(), rhi::kShaderResourceMask, rhi::kShaderResourceMask});
  frame.pre_exposure.block = state.blocks[state.read].get();
  frame.adapted = graph.import_texture(graph::ImportedTexture{
      state.texels[written].get(), rhi::kShaderResourceMask, rhi::kShaderResourceMask});
  graph.mark_history(frame.adapted);
  out = frame;
  return {};
}

// The histogram has 64 buckets and counts every texel of the half
// resolution input; then one thread adapts and writes the value.
// A thread takes one sample in groups of 8 by 8; the sums are integers, so
// the grouping does not change them.
void Exposure::State::Record::operator()(graph::RecordContext& context) const {
  rhi::CommandList& list = context.list();
  rhi::Buffer* histogram = context.buffer(state->histogram_buffer);
  if (!adaptation) {
    state->metered = false;
    rhi::ShaderResourceViewResult scene = state->device->create_shader_resource_view(
        rhi::Ref<rhi::Texture>(context.texture(state->scene_color)), rhi::TextureViewDescription{});
    if (!scene.ok()) {
      state->view_failure = "scene colour's sampled view: " + std::string(scene.detail());
      return;
    }
    rhi::UnorderedAccessViewResult words = state->device->create_unordered_access_view(
        rhi::Ref<rhi::Buffer>(histogram), rhi::BufferViewDescription{});
    if (!words.ok()) {
      state->view_failure = "the histogram's storage view: " + std::string(words.detail());
      return;
    }
    PostHistogram block{};
    block.scene_color = scene.value()->texture_index();
    block.factor = state->sampled[state->read]->texture_index();
    block.histogram = words.value()->storage_buffer_index();
    block.width = state->size.width;
    block.height = state->size.height;
    block.log_min = kHistogramLogMin;
    block.log_max = kHistogramLogMax;
    list.set_compute_pipeline(*state->histogram);
    block.mode = kHistogramClear;
    list.set_draw_block(block);
    list.dispatch(rhi::DispatchArguments{1, 1, 1});
    block.mode = kHistogramAccumulate;
    list.set_draw_block(block);
    const std::uint32_t samples_x = (state->size.width + 1U) / 2U;
    const std::uint32_t samples_y = (state->size.height + 1U) / 2U;
    list.dispatch(
        rhi::DispatchArguments{(samples_x + kHistogramGroupSize - 1U) / kHistogramGroupSize,
                               (samples_y + kHistogramGroupSize - 1U) / kHistogramGroupSize, 1});
    state->metered = true;
    return;
  }
  // The histogram pass recorded nothing: the written texel keeps what it
  // held (exposure.hpp, contract 8).
  if (!state->metered) {
    return;
  }
  rhi::ShaderResourceViewResult words = state->device->create_shader_resource_view(
      rhi::Ref<rhi::Buffer>(histogram), rhi::BufferViewDescription{});
  if (!words.ok()) {
    state->view_failure = "the histogram's sampled view: " + std::string(words.detail());
    return;
  }
  const std::size_t written = 1U - state->read;
  PostAdaptation block{};
  block.histogram = words.value()->buffer_index();
  block.previous = state->sampled[state->read]->texture_index();
  block.adapted = state->storage[written]->storage_texture_index();
  block.reset = state->inputs.reset ? 1U : 0U;
  block.compensation_stops = state->inputs.compensation_stops;
  block.delta_seconds = state->inputs.delta_seconds;
  block.log_min = kHistogramLogMin;
  block.log_max = kHistogramLogMax;
  block.min_ev100 = state->inputs.min_ev100;
  block.max_ev100 = state->inputs.max_ev100;
  list.set_compute_pipeline(*state->adaptation);
  list.set_draw_block(block);
  list.dispatch(rhi::DispatchArguments{1, 1, 1});
}

// The histogram over the half resolution scene colour, then the
// adaptation, whose buffer tone mapping reads in the same frame.
void declare_exposure(Exposure& exposure, graph::FrameGraph& graph, const ExposureFrame& frame,
                      graph::TextureHandle scene_color, rhi::Extent2 size,
                      const ExposureInputs& inputs) {
  ExposureAccess::State& state = ExposureAccess::state(exposure);
  state.frame = frame;
  state.scene_color = scene_color;
  state.size = size;
  state.inputs = inputs;
  state.metered = false;
  state.histogram_buffer =
      graph.create_buffer(graph::FrameBufferDescription{kHistogramBytes, "exposure histogram"});
  state.records[0] = ExposureAccess::State::Record{&state, false};
  state.records[1] = ExposureAccess::State::Record{&state, true};
  const std::array<graph::TextureUse, 2> histogram_textures{
      graph::TextureUse{scene_color, graph::Access::Sampled, graph::Contents::Keep, 0},
      graph::TextureUse{frame.pre_exposure.factor, graph::Access::Sampled, graph::Contents::Keep,
                        0}};
  const std::array<graph::BufferUse, 1> histogram_buffers{
      graph::BufferUse{state.histogram_buffer, graph::Access::ComputeWrite}};
  graph.add_pass(graph::PassDeclaration{"exposure histogram", graph::PassKind::Compute,
                                        histogram_textures, histogram_buffers, state.records[0]});
  const std::array<graph::TextureUse, 2> adaptation_textures{
      graph::TextureUse{frame.pre_exposure.factor, graph::Access::Sampled, graph::Contents::Keep,
                        0},
      graph::TextureUse{frame.adapted, graph::Access::ComputeWrite, graph::Contents::Keep, 0}};
  const std::array<graph::BufferUse, 1> adaptation_buffers{
      graph::BufferUse{state.histogram_buffer, graph::Access::Sampled}};
  graph.add_pass(graph::PassDeclaration{"exposure adaptation", graph::PassKind::Compute,
                                        adaptation_textures, adaptation_buffers, state.records[1]});
  state.declared = true;
}

void bind_pre_exposure(rhi::CommandList& list, const FramePreExposure& pre_exposure) {
  list.set_static_block(kPreExposureSlot, *pre_exposure.block, 0);
}

} // namespace bw::postprocess
