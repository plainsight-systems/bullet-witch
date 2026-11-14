#ifndef BW_RENDERING_POSTPROCESS_GRADE_BAKE_HPP
#define BW_RENDERING_POSTPROCESS_GRADE_BAKE_HPP

#include "bw/rendering/graph/builder.hpp"
#include "bw/rendering/graph/declaration.hpp"
#include "bw/rendering/postprocess/grade.hpp"
#include "bw/rendering/rhi/device.hpp"
#include "bw/rendering/rhi/format.hpp"
#include "bw/rendering/rhi/viewport.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

// Responsibility: the grade bake (docs/architecture/post-processing.md,
// invariants 7 and 8, "The grade and the lookup table"): every step from
// an exposed scene colour to an encoded display colour as a pure function,
// the lookup table those steps are baked into, and the declaration that
// rebakes it when the grade or the display changes. The functions are the
// reference the compute kernel is held to: the kernel states the same
// steps in HLSL and a test compares the two at table coordinates.
//
// Contract. Colours are linear unless a step says encoded. Rec. 709 means
// those primaries with a D65 white; ACEScg means the AP1 primaries with
// the ACES white, reached from Rec. 709 through XYZ and the Bradford
// adaptation from D65; Y is the ACEScg luminance (0.2722287168,
// 0.6740817658, 0.0536895174) of a colour.
//   1. The shaper. shaper_decode(e) is 2^((e - kShaperGreyCode) *
//      kShaperStops) * kShaperGrey minus the same at e = 0, so 0 decodes
//      to 0 and the table spans kShaperStops stops with kShaperGrey near
//      kShaperGreyCode. shaper_encode(x) is saturate(log2(x + d0) /
//      kShaperStops - log2(kShaperGrey) / kShaperStops + kShaperGreyCode),
//      d0 being 2^(-kShaperGreyCode * kShaperStops) * kShaperGrey; it is
//      shaper_decode's inverse on 0 to 1.
//   2. The table is kLookupSize cubed texels of kLookupFormat. Texel index
//      i on an axis holds the encoded coordinate table_coordinate(i) = i /
//      (kLookupSize - 1), and a reader samples the encoded value e at
//      lookup_coordinate(e) = e * (kLookupSize - 1) / kLookupSize + 0.5 /
//      kLookupSize, so the ends fall on texel centres. A texel stores a
//      display colour divided by kLookupScale, since the curve's white
//      clip passes 1; a reader multiplies it back.
//   3. White balance. white_balance_matrix(t, n) maps Rec. 709 to Rec. 709
//      so that the white of temperature t and tint n becomes D65: the
//      white's chromaticity is the daylight locus fit at t from 4000 K up
//      and the Planckian locus fit below, moved along the isotherm of t by
//      0.05 * n; the matrix is the Bradford adaptation from it to D65
//      between the conversions to and from XYZ. The bake applies it unless
//      t is 6500 and n is 0 (each within 1e-8), when the colour passes
//      unchanged.
//   4. Gamut expansion. expand_gamut(c), c in ACEScg: with chroma = c / Y
//      and d = dot(chroma - 1, chroma - 1), the amount is (1 - 2^(-4 * d))
//      * (1 - 2^(-4 * Y^2)), and the result is c moved by that amount
//      toward the colour that reading c's Rec. 709 coordinates in a wider
//      set of primaries would give. A colour with Y of 0 is returned as it
//      is, and a neutral colour is unchanged.
//   5. Correction. correct_color(c, grade), c in ACEScg: one tonal range's
//      result is, in this order, saturation about Y (lerp from Y to c,
//      then the larger of that and 0), contrast as a power about middle
//      grey ((c / 0.18)^contrast * 0.18), gamma (c^(1 / gamma)),
//      then c * gain + offset, with that range's terms times the whole
//      range's (the offsets added). The result is the three ranges' sum by
//      weights from c's own Y: shadows 1 - smoothstep(0, shadows_max, Y),
//      highlights smoothstep(highlights_min, highlights_max, Y), midtones
//      the rest; the weights sum to 1. At the default terms it is the
//      identity on colours with no negative channel.
//   6. Blue correction. blue_correct(c) and blue_uncorrect(c), c in ACEScg,
//      each move c by kBlueCorrection toward a fixed matrix times c; the
//      two matrices are a pair of inverses, applied in AP0. Each leaves a
//      neutral colour unchanged. blue_uncorrect is not blue_correct's
//      inverse: each is a blend, and the bake runs the curve between them.
//   7. The curve. film_curve(x, curve), one channel, x >= 0, with L =
//      log10(x), toe_scale = 1 + black_clip - toe, shoulder_scale = 1 +
//      white_clip - shoulder, and the match points: toe_match is, when toe
//      > 0.8, (1 - toe - 0.18) / slope + log10(0.18), and otherwise
//      log10(0.18) - 0.5 * ln((1 + bt) / (1 - bt)) * (toe_scale / slope)
//      with bt = (0.18 + black_clip) / toe_scale - 1; straight_match = (1
//      - toe) / slope - toe_match; shoulder_match = shoulder / slope -
//      straight_match. The straight segment is slope * (L +
//      straight_match); the toe is -black_clip + 2 * toe_scale / (1 +
//      exp(-2 * slope / toe_scale * (L - toe_match))), used where L <
//      toe_match and the straight segment elsewhere; the shoulder is 1 +
//      white_clip - 2 * shoulder_scale / (1 + exp(2 * slope /
//      shoulder_scale * (L - shoulder_match))), used where L >
//      shoulder_match and the straight segment elsewhere. With t =
//      saturate((L - toe_match) / (shoulder_match - toe_match)), replaced
//      by 1 - t when shoulder_match < toe_match, and then smoothed as (3 -
//      2t) * t^2, the result is lerp(toe, shoulder, t). It is -black_clip
//      at 0 and tends to 1 + white_clip; under the default curve
//      film_curve(0.18) is 0.18.
//   8. The film tone map. film_tone_map(c, curve), c in ACEScg, in this
//      order: to AP0; the glow, AP0 times 1 + g, g being the glow gain
//      0.05 * s where the colour's luminance proxy is at most two thirds
//      of 0.08, 0 from twice 0.08 up and 0.05 * s * (0.08 / proxy - 0.5)
//      between, s a sigmoid of (saturation - 0.4) / 0.2; the red modifier,
//      red += w * saturation * (0.03 - red) * (1 - 0.82), w the squared
//      smoothstep of 1 - abs(2 * hue / 135) for the hue's angle from red
//      in degrees; back to ACEScg and the larger of that and 0; a
//      desaturation toward Y keeping kPreDesaturation; film_curve per
//      channel; a desaturation toward Y keeping kPostDesaturation; the
//      larger of that and 0. A neutral colour stays neutral.
//   9. The encode. srgb_encode(x) is 12.92 * x below 0.00313067 and 1.055
//      * x^(1 / 2.4) - 0.055 from there up.
//   10. The whole. grade_display(x, grade), x an exposed Rec. 709 colour
//      with no negative channel, is, in order: the white balance of
//      contract 3; to ACEScg; expand_gamut; correct_color; blue_correct;
//      film_tone_map; blue_uncorrect; to Rec. 709 and the larger of that
//      and 0; srgb_encode per channel. It is the encoded display colour
//      for an sRGB display whose white is 1, and may pass 1.
//      bake_texel(e, grade) is saturate(grade_display(shaper_decode of
//      each channel of e, grade) / kLookupScale): what the table's texel
//      at encoded coordinate e stores.
//      Precondition of contracts 3 to 10: validate_grade(grade) is None.
//   11. The table and its bake. A GradeBake owns the table.
//      declare_grade_bake(bake, device, graph, grade, display, table)
//      imports the table into the frame and gives its handle; when no bake
//      has been declared yet, or `grade` or `display` differs from the pair
//      of the last bake declared, it first declares the compute work after
//      which every texel (x, y, z) stores bake_texel({table_coordinate(x),
//      table_coordinate(y), table_coordinate(z)}, grade) rounded to the
//      format's nearest level, as the device computes it in 32-bit floats;
//      otherwise it declares no work. The number and kind of passes are
//      not part of the contract. A grade that does not validate is
//      GradeInvalid with the rule in `grade`; a display whose encoding is
//      not Srgb is DisplayEncodingUnsupported; either way nothing is
//      declared and the pair is unchanged. A frame whose graph the caller
//      does not execute is followed by frame_dropped(), after which the
//      next declare bakes whatever the pair.
//   12. create_grade_bake reads the kernel's artifact from
//      `shader_directory` and makes the table, the pipeline and what the
//      bake writes through; a failure is returned by name with a detail
//      text and `out` is left empty.
//
// Note. The kernel does not write the table through a storage view:
// the root layout has no storage array for a 3D texture
// (bw/rendering/shaders/root_layout.hpp, DescriptorArray) and an unordered
// access view names a 2D texture (bw/rendering/rhi/resources.hpp,
// validate_texture_view), so the bake reaches the table another way, as
// the depth hierarchy's build does, and contract 11 promises the texels
// and not the passes.
//
// Core guidelines read: I.1 (the grade and the display are arguments),
// I.5 (preconditions), E.27, C.21, Con.5, ES.45. Performance guidelines
// read: GPU.9 (the table is made once and rebaked in place), GPU.1 (the
// grade goes to the device as one block; nothing is read back).
namespace bw::postprocess {

inline constexpr std::uint32_t kLookupSize = 32;
inline constexpr rhi::Format kLookupFormat = rhi::Format::R10G10B10A2Unorm;
inline constexpr float kLookupScale = 1.05F;

inline constexpr float kShaperStops = 14.0F;
inline constexpr float kShaperGrey = 0.18F;
inline constexpr float kShaperGreyCode = 444.0F / 1023.0F;

// The bake's fixed steps: the blend of the blue correction and the two
// desaturations around the curve.
inline constexpr float kBlueCorrection = 0.6F;
inline constexpr float kPreDesaturation = 0.96F;
inline constexpr float kPostDesaturation = 0.93F;

// Rows of a three by three matrix applied to a colour as a column.
struct ColorMatrix {
  std::array<Rgb, 3> rows{};
};
[[nodiscard]] constexpr Rgb transform(const ColorMatrix& matrix, Rgb color) noexcept {
  return Rgb{matrix.rows[0].r * color.r + matrix.rows[0].g * color.g + matrix.rows[0].b * color.b,
             matrix.rows[1].r * color.r + matrix.rows[1].g * color.g + matrix.rows[1].b * color.b,
             matrix.rows[2].r * color.r + matrix.rows[2].g * color.g + matrix.rows[2].b * color.b};
}

[[nodiscard]] float shaper_decode(float encoded) noexcept;
[[nodiscard]] float shaper_encode(float linear) noexcept;
[[nodiscard]] constexpr float table_coordinate(std::uint32_t texel) noexcept {
  return static_cast<float>(texel) / static_cast<float>(kLookupSize - 1U);
}
[[nodiscard]] constexpr float lookup_coordinate(float encoded) noexcept {
  return encoded * (static_cast<float>(kLookupSize - 1U) / static_cast<float>(kLookupSize)) +
         0.5F / static_cast<float>(kLookupSize);
}

[[nodiscard]] ColorMatrix white_balance_matrix(float temperature, float tint) noexcept;
[[nodiscard]] Rgb rec709_to_acescg(Rgb color) noexcept;
[[nodiscard]] Rgb acescg_to_rec709(Rgb color) noexcept;
[[nodiscard]] Rgb expand_gamut(Rgb acescg) noexcept;
[[nodiscard]] Rgb correct_color(Rgb acescg, const Grade& grade) noexcept;
[[nodiscard]] Rgb blue_correct(Rgb acescg) noexcept;
[[nodiscard]] Rgb blue_uncorrect(Rgb acescg) noexcept;
[[nodiscard]] float film_curve(float value, const FilmCurve& curve) noexcept;
[[nodiscard]] Rgb film_tone_map(Rgb acescg, const FilmCurve& curve) noexcept;
[[nodiscard]] float srgb_encode(float linear) noexcept;

[[nodiscard]] Rgb grade_display(Rgb exposed, const Grade& grade) noexcept;
[[nodiscard]] Rgb bake_texel(Rgb encoded, const Grade& grade) noexcept;

enum class GradeBakeError : std::uint8_t {
  None,
  ArtifactFailed,
  PipelineFailed,
  TextureFailed,
  BufferFailed,
  ViewFailed,
  GradeInvalid,
  DisplayEncodingUnsupported,
};

struct GradeBakeFailure {
  GradeBakeError error{GradeBakeError::None};
  // The rule broken, for GradeInvalid; otherwise None.
  GradeError grade{GradeError::None};
  std::string detail{};
  [[nodiscard]] bool ok() const noexcept { return error == GradeBakeError::None; }
};

class GradeBake {
public:
  GradeBake();
  GradeBake(const GradeBake&) = delete;
  GradeBake& operator=(const GradeBake&) = delete;
  GradeBake(GradeBake&&) noexcept;
  GradeBake& operator=(GradeBake&&) noexcept;
  ~GradeBake();

  // Contract 11.
  void frame_dropped() noexcept;

private:
  friend struct GradeBakeAccess;
  struct State;
  std::unique_ptr<State> state_;
};

[[nodiscard]] GradeBakeFailure create_grade_bake(rhi::Device& device,
                                                 const std::filesystem::path& shader_directory,
                                                 GradeBake& out);

// Precondition: `bake` was made by a create_grade_bake that returned no
// failure.
[[nodiscard]] GradeBakeFailure declare_grade_bake(GradeBake& bake, rhi::Device& device,
                                                  graph::FrameGraph& graph, const Grade& grade,
                                                  const rhi::DisplayReport& display,
                                                  graph::TextureHandle& table);

} // namespace bw::postprocess

#endif
