#ifndef BW_RENDERING_POSTPROCESS_TONE_MAPPING_HPP
#define BW_RENDERING_POSTPROCESS_TONE_MAPPING_HPP

#include "bw/rendering/graph/builder.hpp"
#include "bw/rendering/graph/declaration.hpp"
#include "bw/rendering/rhi/device.hpp"
#include "bw/rendering/rhi/format.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

// Responsibility: the tone mapping pass (docs/architecture/
// post-processing.md, invariant 9): the one place scene colour becomes
// display colour. One raster pass that applies this frame's exposure, adds
// bloom, applies the vignette, looks the result up in the grade's table
// (grade_bake.hpp) and adds a dither of one code value of the target.
//
// Contract.
//   1. The target. A ToneMapping is made for one target format, which is
//      B8G8R8A8Unorm, the viewport's format that takes encoded values
//      (bw/rendering/rhi/viewport.hpp, invariant 7); any other is
//      TargetFormatUnsupported. A hardware-sRGB format is refused because
//      the table holds encoded values and the pass never encodes: such a
//      target would encode twice. dither_amplitude(format) is one code
//      value of it, 1 / 255.
//   2. The pass. For the target pixel whose centre is at uv, in this order:
//        a. c is scene colour sampled at uv and b the bloom result sampled
//           at uv, both bilinear and clamped at the edge; s is e / f, the
//           frame's adapted texel over its factor texel (exposure.hpp,
//           contract 6); m is vignette_mask(uv, aspect, inputs.vignette),
//           aspect the target's height over its width.
//        b. x = (c + b * inputs.bloom_intensity) * s * m: exposure, bloom
//           add, vignette.
//        c. Grain: none; a grade with grain does not validate (grade.hpp).
//        d. d = the table sampled trilinearly, clamped, at
//           lookup_coordinate(shaper_encode(x)) per channel, times
//           kLookupScale. Away from the table's interpolation d is
//           grade_display(x, grade) of the grade the table was baked from.
//        e. The target receives float4(d + n * dither_amplitude(format),
//           1), n one triangular noise value in -1 to 1 for the pixel,
//           the same for the three channels.
//      Nothing else is in the pass, and nothing after the lookup encodes.
//   3. The vignette. vignette_mask(u, v, aspect, intensity), with q =
//      (2u - 1, 2v - 1) * (1, aspect) * sqrt(2) / sqrt(1 + aspect^2) *
//      intensity, is 1 / (1 + dot(q, q))^2, and 1 when intensity is not
//      above 0. It is 1 at the centre of the target and the same at its
//      four corners whatever the aspect.
//   4. The dither. n is triangular noise in -1 to 1 that depends on the
//      pixel's uv and on inputs.frame_index modulo 8 and on nothing else.
//      The frame index is an input: nothing here reads a clock or a
//      counter of its own, so a frame of the same inputs is the same
//      pixels.
//   5. declare_tone_mapping(tone_mapping, device, graph, inputs) declares
//      one Raster pass with inputs.target as the colour attachment at slot
//      0, Overwrite, reading scene colour, the bloom result, the two
//      exposure texels and the table Sampled. inputs.target has the format
//      the ToneMapping was made for and inputs.size, which is scene
//      colour's size. A pass reads the frame's textures through views made
//      while the frame is recorded; when the device refuses one the pass
//      records no draw; record_failure() is then ViewFailed until the next
//      declare_tone_mapping, which returns it before anything else, with
//      nothing declared. A caller reads record_failure() after the frame's
//      graph is recorded and does not execute a frame that has one
//      (post_chain.hpp, end_post_frame).
//   6. create_tone_mapping reads the two artifacts from `shader_directory`
//      and makes the sampler and the pipeline; a failure is returned by
//      name with a detail text and `out` is left empty.
//
// The noise, which the kernel states: a uniform value h in 0 to 1 made
// triangular, with w = 2h - 1, n = sign(w) * (1 - sqrt(1 - abs(w))); h is
// frac(sin(p.x + p.y * 543.31) * 493013), p = uv + (H2, H3), the radical
// inverses in bases 2 and 3 of the frame index modulo 8.
//
// Notes. Bloom's intensity multiplies the bloom result in this pass.
// Alpha is 1. The noise is one value for the three channels. The pass has
// no PQ or linear-output branch: the viewport's surface is tagged sRGB
// (bw/rendering/rhi/viewport.hpp, invariant 8) and the table's encode is
// the sRGB curve.
//
// Core guidelines read: I.1 (the frame index, the intensity and the
// vignette are arguments), E.27, C.21, Con.5, R.3. Performance guidelines
// read: GPU.9 (the pipeline and sampler are made once).
namespace bw::postprocess {

// Contract 1; 0 for a format the pass does not take.
[[nodiscard]] constexpr float dither_amplitude(rhi::Format target) noexcept {
  return target == rhi::Format::B8G8R8A8Unorm ? 1.0F / 255.0F : 0.0F;
}

// Contract 3. Precondition: aspect > 0.
[[nodiscard]] float vignette_mask(float u, float v, float aspect, float intensity) noexcept;

enum class ToneMappingError : std::uint8_t {
  None,
  TargetFormatUnsupported,
  ArtifactFailed,
  StateFailed,
  PipelineFailed,
  ViewFailed,
};

struct ToneMappingFailure {
  ToneMappingError error{ToneMappingError::None};
  std::string detail{};
  [[nodiscard]] bool ok() const noexcept { return error == ToneMappingError::None; }
};

// What one frame's pass reads and writes. The handles are the frame's:
// scene colour and the target from the caller, the bloom result from
// declare_bloom, the texels from begin_exposure_frame, the table from
// declare_grade_bake.
struct ToneMappingInputs {
  graph::TextureHandle scene_color{};
  graph::TextureHandle bloom{};
  graph::TextureHandle factor{};
  graph::TextureHandle adapted{};
  graph::TextureHandle table{};
  graph::TextureHandle target{};
  rhi::Extent2 size{};
  float bloom_intensity{};
  float vignette{};
  std::uint32_t frame_index{};
};

class ToneMapping {
public:
  ToneMapping();
  ToneMapping(const ToneMapping&) = delete;
  ToneMapping& operator=(const ToneMapping&) = delete;
  ToneMapping(ToneMapping&&) noexcept;
  ToneMapping& operator=(ToneMapping&&) noexcept;
  ~ToneMapping();

  // Contract 5.
  [[nodiscard]] ToneMappingFailure record_failure() const;

private:
  friend struct ToneMappingAccess;
  struct State;
  std::unique_ptr<State> state_;
};

[[nodiscard]] ToneMappingFailure create_tone_mapping(rhi::Device& device,
                                                     const std::filesystem::path& shader_directory,
                                                     rhi::Format target_format, ToneMapping& out);

// Precondition: `tone_mapping` was made by a create_tone_mapping that
// returned no failure.
[[nodiscard]] ToneMappingFailure declare_tone_mapping(ToneMapping& tone_mapping,
                                                      rhi::Device& device, graph::FrameGraph& graph,
                                                      const ToneMappingInputs& inputs);

} // namespace bw::postprocess

#endif
