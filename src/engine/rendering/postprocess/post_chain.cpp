#include "bw/rendering/postprocess/post_chain.hpp"

#include "bw/rendering/graph/builder.hpp"
#include "bw/rendering/graph/declaration.hpp"
#include "bw/rendering/postprocess/bloom.hpp"
#include "bw/rendering/postprocess/exposure.hpp"
#include "bw/rendering/postprocess/grade_bake.hpp"
#include "bw/rendering/postprocess/tone_mapping.hpp"
#include "bw/rendering/rhi/device.hpp"
#include "bw/rendering/rhi/format.hpp"

#include <filesystem>
#include <string>
#include <utility>

// Responsibility: the out-of-line part of bw/rendering/postprocess/
// post_chain.hpp: the four steps made and declared in the header's order,
// each failure returned with the step's own beside it.
namespace bw::postprocess {

namespace {

PostChainFailure step_failed(PostChainError error) {
  PostChainFailure failure{};
  failure.error = error;
  return failure;
}

std::string with_detail(std::string text, unsigned code, const std::string& detail) {
  text += " error ";
  text += std::to_string(code);
  if (!detail.empty()) {
    text += ": ";
    text += detail;
  }
  return text;
}

} // namespace

PostChainFailure build_post_chain(rhi::Device& device,
                                  const std::filesystem::path& shader_directory,
                                  rhi::Format target_format, PostChain& out) {
  if (ExposureFailure made = create_exposure(device, shader_directory, out.exposure); !made.ok()) {
    PostChainFailure failure = step_failed(PostChainError::ExposureFailed);
    failure.exposure = std::move(made);
    return failure;
  }
  if (BloomFailure made = create_bloom(device, shader_directory, out.bloom); !made.ok()) {
    PostChainFailure failure = step_failed(PostChainError::BloomFailed);
    failure.bloom = std::move(made);
    return failure;
  }
  if (GradeBakeFailure made = create_grade_bake(device, shader_directory, out.bake); !made.ok()) {
    PostChainFailure failure = step_failed(PostChainError::GradeBakeFailed);
    failure.bake = std::move(made);
    return failure;
  }
  if (ToneMappingFailure made =
          create_tone_mapping(device, shader_directory, target_format, out.tone_mapping);
      !made.ok()) {
    PostChainFailure failure = step_failed(PostChainError::ToneMappingFailed);
    failure.tone_mapping = std::move(made);
    return failure;
  }
  return {};
}

PostChainFailure begin_post_frame(PostChain& chain, rhi::Device& device, graph::FrameGraph& graph,
                                  PostFrame& frame) {
  if (ExposureFailure begun = begin_exposure_frame(chain.exposure, device, graph, frame.exposure);
      !begun.ok()) {
    PostChainFailure failure = step_failed(PostChainError::ExposureFailed);
    failure.exposure = std::move(begun);
    return failure;
  }
  return {};
}

// The histogram, then the adaptation, then tone mapping given this frame's
// adapted buffer, the bloom result and the grade's table.
PostChainFailure declare_post_chain(PostChain& chain, rhi::Device& device, graph::FrameGraph& graph,
                                    const PostFrame& frame, graph::TextureHandle scene_color,
                                    graph::TextureHandle target, rhi::Extent2 size,
                                    const PostFrameInputs& inputs) {
  ExposureInputs exposure{};
  exposure.compensation_stops = inputs.grade.exposure_compensation;
  exposure.delta_seconds = inputs.delta_seconds;
  exposure.reset = inputs.reset;
  exposure.min_ev100 = inputs.grade.exposure_min_ev100;
  exposure.max_ev100 = inputs.grade.exposure_max_ev100;
  declare_exposure(chain.exposure, graph, frame.exposure, scene_color, size, exposure);
  graph::TextureHandle bloom{};
  if (BloomFailure declared = declare_bloom(chain.bloom, device, graph, scene_color, size, bloom);
      !declared.ok()) {
    PostChainFailure failure = step_failed(PostChainError::BloomFailed);
    failure.bloom = std::move(declared);
    return failure;
  }
  graph::TextureHandle table{};
  if (GradeBakeFailure declared =
          declare_grade_bake(chain.bake, device, graph, inputs.grade, inputs.display, table);
      !declared.ok()) {
    PostChainFailure failure = step_failed(PostChainError::GradeBakeFailed);
    failure.bake = std::move(declared);
    return failure;
  }
  ToneMappingInputs tone{};
  tone.scene_color = scene_color;
  tone.bloom = bloom;
  tone.factor = frame.exposure.pre_exposure.factor;
  tone.adapted = frame.exposure.adapted;
  tone.table = table;
  tone.target = target;
  tone.size = size;
  tone.bloom_intensity = inputs.grade.bloom_intensity;
  tone.vignette = inputs.grade.vignette;
  tone.frame_index = inputs.frame_index;
  if (ToneMappingFailure declared = declare_tone_mapping(chain.tone_mapping, device, graph, tone);
      !declared.ok()) {
    PostChainFailure failure = step_failed(PostChainError::ToneMappingFailed);
    failure.tone_mapping = std::move(declared);
    return failure;
  }
  return {};
}

// The steps that make views while the graph is recorded, in the header's
// order; the grade bake records with views it made at creation.
PostChainFailure end_post_frame(const PostChain& chain) {
  if (ExposureFailure recorded = chain.exposure.record_failure(); !recorded.ok()) {
    PostChainFailure failure = step_failed(PostChainError::ExposureFailed);
    failure.exposure = std::move(recorded);
    return failure;
  }
  if (BloomFailure recorded = chain.bloom.record_failure(); !recorded.ok()) {
    PostChainFailure failure = step_failed(PostChainError::BloomFailed);
    failure.bloom = std::move(recorded);
    return failure;
  }
  if (ToneMappingFailure recorded = chain.tone_mapping.record_failure(); !recorded.ok()) {
    PostChainFailure failure = step_failed(PostChainError::ToneMappingFailed);
    failure.tone_mapping = std::move(recorded);
    return failure;
  }
  return {};
}

void post_frame_dropped(PostChain& chain) noexcept {
  chain.exposure.frame_dropped();
  chain.bake.frame_dropped();
}

std::string post_chain_failure_text(const PostChainFailure& failure) {
  switch (failure.error) {
  case PostChainError::None:
    return "post chain: no failure";
  case PostChainError::ExposureFailed:
    return with_detail("post chain: exposure", static_cast<unsigned>(failure.exposure.error),
                       failure.exposure.detail);
  case PostChainError::BloomFailed:
    return with_detail("post chain: bloom", static_cast<unsigned>(failure.bloom.error),
                       failure.bloom.detail);
  case PostChainError::GradeBakeFailed:
    return with_detail("post chain: grade bake", static_cast<unsigned>(failure.bake.error),
                       failure.bake.detail);
  case PostChainError::ToneMappingFailed:
    return with_detail("post chain: tone mapping",
                       static_cast<unsigned>(failure.tone_mapping.error),
                       failure.tone_mapping.detail);
  }
  return "post chain: unknown failure";
}

} // namespace bw::postprocess
