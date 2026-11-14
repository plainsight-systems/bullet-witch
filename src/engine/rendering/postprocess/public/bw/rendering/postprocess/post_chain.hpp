#ifndef BW_RENDERING_POSTPROCESS_POST_CHAIN_HPP
#define BW_RENDERING_POSTPROCESS_POST_CHAIN_HPP

#include "bw/rendering/graph/builder.hpp"
#include "bw/rendering/graph/declaration.hpp"
#include "bw/rendering/postprocess/bloom.hpp"
#include "bw/rendering/postprocess/exposure.hpp"
#include "bw/rendering/postprocess/grade.hpp"
#include "bw/rendering/postprocess/grade_bake.hpp"
#include "bw/rendering/postprocess/tone_mapping.hpp"
#include "bw/rendering/rhi/device.hpp"
#include "bw/rendering/rhi/format.hpp"
#include "bw/rendering/rhi/viewport.hpp"

#include <cstdint>
#include <filesystem>
#include <string>

// Responsibility: the post chain as one thing a host declares
// (docs/architecture/post-processing.md, "The chain"): from scene colour
// to display colour in the caller's target, through the histogram and
// adaptation (exposure.hpp), the bloom chain (bloom.hpp), the grade bake
// (grade_bake.hpp) and the tone mapping pass (tone_mapping.hpp). The steps
// are those four headers'; this one states their order in a frame and what
// a caller gives. Nothing here reads options, a clock or a window: the
// grade, the display report, the time step, the reset and the frame index
// are arguments.
//
// Contract.
//   1. build_post_chain(device, shader_directory, target_format, out), in
//      this order, each failure ending it: create_exposure
//      (ExposureFailed), create_bloom (BloomFailed), create_grade_bake
//      (GradeBakeFailed), create_tone_mapping for `target_format`
//      (ToneMappingFailed), with the step's own failure beside the error.
//      On a failure `out` is partly built and is only destroyed.
//   2. A frame, in this order:
//        a. begin_post_frame(chain, device, graph, frame) is
//           begin_exposure_frame: it gives the frame's exposure handles
//           (ExposureFailed, nothing imported).
//        b. The caller creates scene colour, a frame-only texture of
//           scene_color_description(size), and declares into it every
//           pass that writes light, each with frame.exposure.pre_exposure
//           (exposure.hpp, FramePreExposure).
//        c. declare_post_chain(chain, device, graph, frame, scene_color,
//           target, size, inputs) declares, in this order:
//           declare_exposure with the grade's exposure compensation and
//           exposure range, inputs.delta_seconds and inputs.reset;
//           declare_bloom
//           (BloomFailed); declare_grade_bake with inputs.grade and
//           inputs.display (GradeBakeFailed); declare_tone_mapping into
//           `target` with the grade's bloom intensity and vignette and
//           inputs.frame_index (ToneMappingFailed). `target` is a texture
//           of `size` and the chain's target format, which the caller
//           imports and marks as its frame needs; the tone mapping pass is
//           the chain's only use of it. On a failure the passes declared
//           before the failed step stay declared and the caller does not
//           execute that graph.
//        d. end_post_frame(chain), after the frame's graph is recorded
//           (graph::execute) and before the device executes the commands:
//           the failure a pass left while it recorded, in the order
//           exposure (ExposureFailed), bloom (BloomFailed), tone mapping
//           (ToneMappingFailed), or None. A caller that gets one neither
//           executes nor presents that frame: a frame in which a step
//           recorded nothing is never shown.
//      So the target holds, for each pixel, the display colour of contract
//      2 of tone_mapping.hpp for scene colour under this frame's adapted
//      exposure, and the next frame's light is pre-multiplied by it.
//   3. A frame the caller does not execute, for a failure here or any
//      reason, is followed by post_frame_dropped(chain):
//      Exposure::frame_dropped and GradeBake::frame_dropped. The next
//      frame then reads the factor the dropped frame read and bakes the
//      table again.
//   4. The same chain state, scene colour, size and inputs give the same
//      target bytes on one device: every source of time and noise is an
//      input (exposure.hpp, contract 7; tone_mapping.hpp, contract 4).
//   5. post_chain_failure_text(failure) is one line for a person naming
//      the step, its error and its detail; its wording is not part of the
//      contract. No failure falls back: there is no path from scene colour
//      to the target that skips a step.
//   6. What the chain does not have, each a step of the document whose
//      inputs do not exist yet: depth of field and motion blur (no
//      temporal stage hands over depth and motion vectors), the two
//      screen-effect slots and the fade (no screen-effect template kind,
//      no world to enable one), the interface composite and its white (no
//      interface), the PQ and scRGB encodings with their shaper and
//      display mapping (the viewport reports one SDR surface), film grain
//      (no grain texture), a grade read from content or blended between
//      stages (no stage), and a setting that leaves bloom out (no
//      settings). A caller cannot ask for one: the grain and the encoding
//      fail by name, and the rest have no argument.
//
// Core guidelines read: I.1, E.27 (a named error per step with the step's
// own failure beside it), C.21, F.7, R.3. Performance guidelines read:
// MEM.4 and GPU.7 (the exposure pair is the only state that crosses a
// frame), GPU.9.
namespace bw::postprocess {

struct PostChain {
  Exposure exposure{};
  Bloom bloom{};
  GradeBake bake{};
  ToneMapping tone_mapping{};
};

enum class PostChainError : std::uint8_t {
  None,
  ExposureFailed,
  BloomFailed,
  GradeBakeFailed,
  ToneMappingFailed,
};

struct PostChainFailure {
  PostChainError error{PostChainError::None};
  // The step's own failure; the others are at their None.
  ExposureFailure exposure{};
  BloomFailure bloom{};
  GradeBakeFailure bake{};
  ToneMappingFailure tone_mapping{};
  [[nodiscard]] bool ok() const noexcept { return error == PostChainError::None; }
};

// The frame's handles a caller passes on: frame.exposure.pre_exposure to
// the passes that write light, the whole frame to declare_post_chain.
struct PostFrame {
  ExposureFrame exposure{};
};

// What one frame of the chain takes beside its textures.
struct PostFrameInputs {
  Grade grade{};
  rhi::DisplayReport display{};
  // Seconds since the frame before, and whether this frame goes straight
  // to its metered exposure: the first frame, and a camera cut.
  float delta_seconds{};
  bool reset{};
  std::uint32_t frame_index{};
};

// Scene colour of `size`: kSceneColorFormat, cleared to black, named
// "scene color".
[[nodiscard]] constexpr graph::FrameTextureDescription
scene_color_description(rhi::Extent2 size) noexcept {
  graph::FrameTextureDescription description{};
  description.extent = size;
  description.format = kSceneColorFormat;
  description.clear_value.color = {0.0F, 0.0F, 0.0F, 0.0F};
  description.debug_name = "scene color";
  return description;
}

[[nodiscard]] PostChainFailure build_post_chain(rhi::Device& device,
                                                const std::filesystem::path& shader_directory,
                                                rhi::Format target_format, PostChain& out);

[[nodiscard]] PostChainFailure begin_post_frame(PostChain& chain, rhi::Device& device,
                                                graph::FrameGraph& graph, PostFrame& frame);

[[nodiscard]] PostChainFailure declare_post_chain(PostChain& chain, rhi::Device& device,
                                                  graph::FrameGraph& graph, const PostFrame& frame,
                                                  graph::TextureHandle scene_color,
                                                  graph::TextureHandle target, rhi::Extent2 size,
                                                  const PostFrameInputs& inputs);

[[nodiscard]] PostChainFailure end_post_frame(const PostChain& chain);

void post_frame_dropped(PostChain& chain) noexcept;

[[nodiscard]] std::string post_chain_failure_text(const PostChainFailure& failure);

} // namespace bw::postprocess

#endif
