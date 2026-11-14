#include "bw/rendering/postprocess/grade_bake.hpp"

#include "bw/rendering/postprocess/grade.hpp"
#include "curve_match.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <numbers>

// Responsibility: the bake's reference functions
// (bw/rendering/postprocess/grade_bake.hpp, contracts 1 and 3 to 10). Every
// fixed matrix is composed once, in double precision at compile time, from
// its stated digits, and applied in single precision as the kernel applies
// it. The GradeBake object and its declaration are
// grade_bake_pass.cpp's, its kernel post_grade_bake.hlsl.
namespace bw::postprocess {

namespace {

// Row-major, applied to a colour as a column.
using Matrix = std::array<std::array<double, 3>, 3>;

[[nodiscard]] constexpr Matrix multiply(const Matrix& a, const Matrix& b) noexcept {
  Matrix out{};
  for (std::size_t i = 0; i < 3; ++i) {
    for (std::size_t j = 0; j < 3; ++j) {
      out[i][j] = a[i][0] * b[0][j] + a[i][1] * b[1][j] + a[i][2] * b[2][j];
    }
  }
  return out;
}

[[nodiscard]] constexpr ColorMatrix to_color_matrix(const Matrix& m) noexcept {
  ColorMatrix out{};
  for (std::size_t i = 0; i < 3; ++i) {
    out.rows[i] =
        Rgb{static_cast<float>(m[i][0]), static_cast<float>(m[i][1]), static_cast<float>(m[i][2])};
  }
  return out;
}

// The primaries' matrices and the two Bradford adaptations between D65 and
// the ACES white (D60).
constexpr Matrix kAp0ToXyz{{{0.9525523959, 0.0000000000, 0.0000936786},
                            {0.3439664498, 0.7281660966, -0.0721325464},
                            {0.0000000000, 0.0000000000, 1.0088251844}}};
constexpr Matrix kXyzToAp0{{{1.0498110175, 0.0000000000, -0.0000974845},
                            {-0.4959030231, 1.3733130458, 0.0982400361},
                            {0.0000000000, 0.0000000000, 0.9912520182}}};
constexpr Matrix kAp1ToXyz{{{0.6624541811, 0.1340042065, 0.1561876870},
                            {0.2722287168, 0.6740817658, 0.0536895174},
                            {-0.0055746495, 0.0040607335, 1.0103391003}}};
constexpr Matrix kXyzToAp1{{{1.6410233797, -0.3248032942, -0.2364246952},
                            {-0.6636628587, 1.6153315917, 0.0167563477},
                            {0.0117218943, -0.0082844420, 0.9883948585}}};
constexpr Matrix kAp0ToAp1Literal{{{1.4514393161, -0.2365107469, -0.2149285693},
                                   {-0.0765537734, 1.1762296998, -0.0996759264},
                                   {0.0083161484, -0.0060324498, 0.9977163014}}};
constexpr Matrix kXyzToRec709{{{3.2409699419, -1.5373831776, -0.4986107603},
                               {-0.9692436363, 1.8759675015, 0.0415550574},
                               {0.0556300797, -0.2039769589, 1.0569715142}}};
constexpr Matrix kRec709ToXyz{{{0.4123907993, 0.3575843394, 0.1804807884},
                               {0.2126390059, 0.7151686788, 0.0721923154},
                               {0.0193308187, 0.1191947798, 0.9505321522}}};
constexpr Matrix kD65ToD60{{{1.0130349146, 0.0061052578, -0.0149709436},
                            {0.0076982301, 0.9981633521, -0.0050320385},
                            {-0.0028413174, 0.0046851567, 0.9245061375}}};
constexpr Matrix kD60ToD65{{{0.9872240087, -0.0061132286, 0.0159532883},
                            {-0.0075983718, 1.0018614847, 0.0053300358},
                            {0.0030725771, -0.0050959615, 1.0816806031}}};
constexpr Rgb kAp1Luminance{0.2722287168F, 0.6740817658F, 0.0536895174F};

// The wide primaries' matrix to XYZ.
constexpr Matrix kWideToXyz{{{0.5441691, 0.2395926, 0.1666943},
                             {0.2394656, 0.7021530, 0.0583814},
                             {-0.0023439, 0.0361834, 1.0552183}}};

// The blue correction and its inverse, both in AP0.
constexpr Matrix kBlueCorrect{{{0.9404372683, -0.0183068787, 0.0778696104},
                               {0.0083786969, 0.8286599939, 0.1629613092},
                               {0.0005471261, -0.0008833746, 1.0003362486}}};
constexpr Matrix kBlueCorrectInverse{{{1.06318, 0.0233956, -0.0865726},
                                      {-0.0106337, 1.20632, -0.19569},
                                      {-0.000590887, 0.00105248, 0.999538}}};

// Rec. 709 to AP1 and back, each through the D65 and D60 adaptation, and
// the two compositions between AP0 and AP1.
constexpr Matrix kAp1ToAp0 = multiply(kXyzToAp0, kAp1ToXyz);
constexpr Matrix kAp0ToAp1 = multiply(kXyzToAp1, kAp0ToXyz);
constexpr Matrix kAp1ToRec709 = multiply(kXyzToRec709, multiply(kD60ToD65, kAp1ToXyz));
constexpr ColorMatrix kRec709ToAp1Matrix =
    to_color_matrix(multiply(kXyzToAp1, multiply(kD65ToD60, kRec709ToXyz)));
constexpr ColorMatrix kAp1ToRec709Matrix = to_color_matrix(kAp1ToRec709);
constexpr ColorMatrix kAp1ToAp0Matrix = to_color_matrix(kAp1ToAp0);
constexpr ColorMatrix kAp0ToAp1LiteralMatrix = to_color_matrix(kAp0ToAp1Literal);

// The blue matrices moved into AP1, and the expansion as wide to AP1 times
// AP1 to Rec. 709.
constexpr ColorMatrix kBlueCorrectAp1 =
    to_color_matrix(multiply(kAp0ToAp1, multiply(kBlueCorrect, kAp1ToAp0)));
constexpr ColorMatrix kBlueCorrectInverseAp1 =
    to_color_matrix(multiply(kAp0ToAp1, multiply(kBlueCorrectInverse, kAp1ToAp0)));
constexpr ColorMatrix kExpandMatrix =
    to_color_matrix(multiply(multiply(kXyzToAp1, kWideToXyz), kAp1ToRec709));

constexpr float kGlowGain = 0.05F;
constexpr float kGlowMid = 0.08F;
constexpr float kRedScale = 0.82F;
constexpr float kRedPivot = 0.03F;
constexpr float kRedHue = 0.0F;
constexpr float kRedWidth = 135.0F;
constexpr float kYcRadiusWeight = 1.75F;
constexpr float kNeutralTemperature = 6500.0F;
constexpr float kNeutralTint = 0.0F;
constexpr float kWhiteBalanceEpsilon = 1e-8F;
constexpr float kCurveMatch = 0.18F;

[[nodiscard]] constexpr Rgb operator+(Rgb a, Rgb b) noexcept {
  return Rgb{a.r + b.r, a.g + b.g, a.b + b.b};
}
[[nodiscard]] constexpr Rgb operator-(Rgb a, Rgb b) noexcept {
  return Rgb{a.r - b.r, a.g - b.g, a.b - b.b};
}
[[nodiscard]] constexpr Rgb operator*(Rgb a, Rgb b) noexcept {
  return Rgb{a.r * b.r, a.g * b.g, a.b * b.b};
}
[[nodiscard]] constexpr Rgb operator*(Rgb a, float s) noexcept {
  return Rgb{a.r * s, a.g * s, a.b * s};
}
[[nodiscard]] constexpr Rgb splat(float v) noexcept { return Rgb{v, v, v}; }
[[nodiscard]] constexpr float dot(Rgb a, Rgb b) noexcept {
  return a.r * b.r + a.g * b.g + a.b * b.b;
}
[[nodiscard]] constexpr Rgb lerp(Rgb a, Rgb b, float t) noexcept { return a + (b - a) * t; }
[[nodiscard]] Rgb max0(Rgb c) noexcept {
  return Rgb{std::max(c.r, 0.0F), std::max(c.g, 0.0F), std::max(c.b, 0.0F)};
}
template <class F> [[nodiscard]] Rgb per_channel(Rgb c, F f) { return Rgb{f(c.r), f(c.g), f(c.b)}; }
[[nodiscard]] float saturate(float x) noexcept { return std::clamp(x, 0.0F, 1.0F); }
[[nodiscard]] float smoothstep(float a, float b, float x) noexcept {
  const float t = saturate((x - a) / (b - a));
  return t * t * (3.0F - 2.0F * t);
}
[[nodiscard]] float luminance(Rgb acescg) noexcept { return dot(acescg, kAp1Luminance); }

// The log shaper's decode, before the offset that makes 0 decode to 0.
[[nodiscard]] float log_to_linear(float encoded) noexcept {
  return std::exp2((encoded - kShaperGreyCode) * kShaperStops) * kShaperGrey;
}

// ---- White balance --------------------------------------------------------------

struct Chromaticity {
  double x{};
  double y{};
};

// The Planckian locus fit.
[[nodiscard]] Chromaticity planckian_locus(double t) noexcept {
  const double u = (0.860117757 + 1.54118254e-4 * t + 1.28641212e-7 * t * t) /
                   (1.0 + 8.42420235e-4 * t + 7.08145163e-7 * t * t);
  const double v = (0.317398726 + 4.22806245e-5 * t + 4.20481691e-8 * t * t) /
                   (1.0 - 2.89741816e-5 * t + 1.61456053e-7 * t * t);
  const double d = 2.0 * u - 8.0 * v + 4.0;
  return Chromaticity{3.0 * u / d, 2.0 * v / d};
}

// The daylight locus fit, corrected so that 6500 is D65.
[[nodiscard]] Chromaticity daylight_locus(double t) noexcept {
  const double corrected = t * (1.4388 / 1.438);
  const double inv = 1.0 / corrected;
  const double x = corrected <= 7000.0
                       ? 0.244063 + (0.09911e3 + (2.9678e6 - 4.6070e9 * inv) * inv) * inv
                       : 0.237040 + (0.24748e3 + (1.9018e6 - 2.0064e9 * inv) * inv) * inv;
  return Chromaticity{x, -3.0 * x * x + 2.87 * x - 0.275};
}

// The locus point moved along the isotherm by 0.05 times the tint.
// The temperature, then the tint.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
[[nodiscard]] Chromaticity planckian_isothermal(double t, double tint) noexcept {
  double u = (0.860117757 + 1.54118254e-4 * t + 1.28641212e-7 * t * t) /
             (1.0 + 8.42420235e-4 * t + 7.08145163e-7 * t * t);
  double v = (0.317398726 + 4.22806245e-5 * t + 4.20481691e-8 * t * t) /
             (1.0 - 2.89741816e-5 * t + 1.61456053e-7 * t * t);
  const double ud_denominator = 1.41213984e6 + 1189.62 * t + t * t;
  const double vd_denominator = 6.19363586e6 - 179.456 * t + t * t;
  const double ud =
      (-1.13758118e9 - 1.91615621e6 * t - 1.53177 * t * t) / (ud_denominator * ud_denominator);
  const double vd =
      (1.97471536e9 - 705674.0 * t - 308.607 * t * t) / (vd_denominator * vd_denominator);
  const double length = std::sqrt(ud * ud + vd * vd);
  u += (vd / length) * tint * 0.05;
  v += -(ud / length) * tint * 0.05;
  const double d = 2.0 * u - 8.0 * v + 4.0;
  return Chromaticity{3.0 * u / d, 2.0 * v / d};
}

// xyY to XYZ at Y = 1, then the Bradford cone response and its inverse.
// The white adapted from, then the white adapted to.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
[[nodiscard]] Matrix bradford(Chromaticity source, Chromaticity destination) noexcept {
  constexpr Matrix kCone{
      {{0.8951, 0.2664, -0.1614}, {-0.7502, 1.7135, 0.0367}, {0.0389, -0.0685, 1.0296}}};
  constexpr Matrix kInverseCone{{{0.9869929, -0.1470543, 0.1599627},
                                 {0.4323053, 0.5183603, 0.0492912},
                                 {-0.0085287, 0.0400428, 0.9684867}}};
  const auto cone_response = [&kCone](Chromaticity c) {
    const double y = std::max(c.y, 1e-10);
    const std::array<double, 3> xyz{c.x / y, 1.0, (1.0 - c.x - c.y) / y};
    std::array<double, 3> out{};
    for (std::size_t i = 0; i < 3; ++i) {
      out[i] = kCone[i][0] * xyz[0] + kCone[i][1] * xyz[1] + kCone[i][2] * xyz[2];
    }
    return out;
  };
  const std::array<double, 3> src = cone_response(source);
  const std::array<double, 3> dst = cone_response(destination);
  const Matrix von_kries{
      {{dst[0] / src[0], 0.0, 0.0}, {0.0, dst[1] / src[1], 0.0}, {0.0, 0.0, dst[2] / src[2]}}};
  return multiply(kInverseCone, multiply(von_kries, kCone));
}

// ---- The film tone map's helpers ------------------------------------------------

[[nodiscard]] float rgb_saturation(Rgb c) noexcept {
  const float lo = std::min({c.r, c.g, c.b});
  const float hi = std::max({c.r, c.g, c.b});
  return (std::max(hi, 1e-10F) - std::max(lo, 1e-10F)) / std::max(hi, 1e-2F);
}

// 0 for a neutral colour.
[[nodiscard]] float rgb_hue(Rgb c) noexcept {
  float hue = 0.0F;
  if (!(c.r == c.g && c.g == c.b)) {
    hue = (180.0F / std::numbers::pi_v<float>)*std::atan2(
        std::numbers::sqrt3_v<float> * (c.g - c.b), 2.0F * c.r - c.g - c.b);
  }
  if (hue < 0.0F) {
    hue += 360.0F;
  }
  return std::clamp(hue, 0.0F, 360.0F);
}

[[nodiscard]] float center_hue(float hue, float centre) noexcept {
  float centred = hue - centre;
  if (centred < -180.0F) {
    centred += 360.0F;
  } else if (centred > 180.0F) {
    centred -= 360.0F;
  }
  return centred;
}

// The luminance proxy YC with the radius weight 1.75.
// The radicand is half the sum of the squared channel differences, so it is
// never negative; the clamp at 0 only removes a rounding below it.
[[nodiscard]] float rgb_yc(Rgb c) noexcept {
  const float radicand = c.b * (c.b - c.g) + c.g * (c.g - c.r) + c.r * (c.r - c.b);
  const float chroma = std::sqrt(std::max(radicand, 0.0F));
  return (c.b + c.g + c.r + kYcRadiusWeight * chroma) / 3.0F;
}

[[nodiscard]] float sigmoid_shaper(float x) noexcept {
  const float t = std::max(1.0F - std::fabs(0.5F * x), 0.0F);
  const float sign = x > 0.0F ? 1.0F : (x < 0.0F ? -1.0F : 0.0F);
  return 0.5F * (1.0F + sign * (1.0F - t * t));
}

[[nodiscard]] float glow_forward(float yc, float gain, float mid) noexcept {
  if (yc <= 2.0F / 3.0F * mid) {
    return gain;
  }
  if (yc >= 2.0F * mid) {
    return 0.0F;
  }
  return gain * (mid / yc - 0.5F);
}

// A range's terms times the whole range's, the offsets added.
[[nodiscard]] ColorCorrection combine(const ColorCorrection& range,
                                      const ColorCorrection& whole) noexcept {
  return ColorCorrection{range.saturation * whole.saturation, range.contrast * whole.contrast,
                         range.gamma * whole.gamma, range.gain * whole.gain,
                         range.offset + whole.offset};
}

// One range's correction: saturation about luminance, contrast about 0.18,
// gamma, gain, offset.
[[nodiscard]] Rgb correct_range(Rgb c, const ColorCorrection& terms) noexcept {
  const float luma = luminance(c);
  c = max0(splat(luma) + (c - splat(luma)) * terms.saturation);
  c = Rgb{std::pow(c.r / 0.18F, terms.contrast.r), std::pow(c.g / 0.18F, terms.contrast.g),
          std::pow(c.b / 0.18F, terms.contrast.b)} *
      0.18F;
  c = Rgb{std::pow(c.r, 1.0F / terms.gamma.r), std::pow(c.g, 1.0F / terms.gamma.g),
          std::pow(c.b, 1.0F / terms.gamma.b)};
  return c * terms.gain + terms.offset;
}

} // namespace

// The decode of e less the decode of 0, so that 0 decodes to 0.
float shaper_decode(float encoded) noexcept { return log_to_linear(encoded) - log_to_linear(0.0F); }

// The log shaper's encode; the decode's offset is added back before it.
float shaper_encode(float linear) noexcept {
  const float offset = log_to_linear(0.0F);
  return saturate(std::log2(linear + offset) / kShaperStops -
                  std::log2(kShaperGrey) / kShaperStops + kShaperGreyCode);
}

// The white adapted from comes from the Planckian fit below 4000 K and the
// daylight fit from there, moved along the isotherm, and is adapted to D65
// between the working space's conversions to and from XYZ. The grade names
// the white the image is balanced from (grade.hpp), so the two whites are
// never swapped.
// The two scalars in the order the header declares them.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
ColorMatrix white_balance_matrix(float temperature, float tint) noexcept {
  const double t = temperature;
  const Chromaticity planckian = planckian_locus(t);
  Chromaticity source = t < 4000.0 ? planckian : daylight_locus(t);
  const Chromaticity isothermal = planckian_isothermal(t, tint);
  source.x += isothermal.x - planckian.x;
  source.y += isothermal.y - planckian.y;
  constexpr Chromaticity kD65{0.31270, 0.32900};
  return to_color_matrix(multiply(kXyzToRec709, multiply(bradford(source, kD65), kRec709ToXyz)));
}

Rgb rec709_to_acescg(Rgb color) noexcept { return transform(kRec709ToAp1Matrix, color); }

Rgb acescg_to_rec709(Rgb color) noexcept { return transform(kAp1ToRec709Matrix, color); }

// The amount comes from the chroma distance and the luminance, at full
// expansion, and lerps toward the expansion matrix.
// A colour with no luminance is returned as it is: its chroma, the colour
// divided by its luminance, is undefined.
Rgb expand_gamut(Rgb acescg) noexcept {
  const float luma = luminance(acescg);
  if (luma == 0.0F) {
    return acescg;
  }
  const Rgb chroma = acescg * (1.0F / luma);
  const Rgb from_neutral = chroma - splat(1.0F);
  const float distance = dot(from_neutral, from_neutral);
  const float amount =
      (1.0F - std::exp2(-4.0F * distance)) * (1.0F - std::exp2(-4.0F * luma * luma));
  return lerp(acescg, transform(kExpandMatrix, acescg), amount);
}

// The three ranges, each with the whole range's terms, weighted by
// luminance.
Rgb correct_color(Rgb acescg, const Grade& grade) noexcept {
  const float luma = luminance(acescg);
  const Rgb shadows = correct_range(acescg, combine(grade.shadows, grade.whole));
  const float shadows_weight = 1.0F - smoothstep(0.0F, grade.shadows_max, luma);
  const Rgb highlights = correct_range(acescg, combine(grade.highlights, grade.whole));
  const float highlights_weight = smoothstep(grade.highlights_min, grade.highlights_max, luma);
  const Rgb midtones = correct_range(acescg, combine(grade.midtones, grade.whole));
  const float midtones_weight = 1.0F - shadows_weight - highlights_weight;
  return shadows * shadows_weight + midtones * midtones_weight + highlights * highlights_weight;
}

Rgb blue_correct(Rgb acescg) noexcept {
  return lerp(acescg, transform(kBlueCorrectAp1, acescg), kBlueCorrection);
}

Rgb blue_uncorrect(Rgb acescg) noexcept {
  return lerp(acescg, transform(kBlueCorrectInverseAp1, acescg), kBlueCorrection);
}

// The toe match, by the linear form past a toe of 0.8 and the inverse
// hyperbolic tangent below, and the shoulder match from the straight
// segment's, both matching 0.18 in to 0.18 out.
detail::CurveMatch detail::curve_match(const FilmCurve& curve) noexcept {
  const float toe_scale = 1.0F + curve.black_clip - curve.toe;
  float toe_match = 0.0F;
  if (curve.toe > 0.8F) {
    toe_match = (1.0F - curve.toe - kCurveMatch) / curve.slope + std::log10(kCurveMatch);
  } else {
    const float bt = (kCurveMatch + curve.black_clip) / toe_scale - 1.0F;
    toe_match = std::log10(kCurveMatch) -
                0.5F * std::log((1.0F + bt) / (1.0F - bt)) * (toe_scale / curve.slope);
  }
  const float straight_match = (1.0F - curve.toe) / curve.slope - toe_match;
  return CurveMatch{toe_match, curve.shoulder / curve.slope - straight_match};
}

// The scales, the match points (0.18 in to 0.18 out), the three segments,
// their selection and the smoothed blend.
// At 0 the logarithm is minus infinity and the blend would multiply it by a
// weight of 0; the curve's limit there, -black_clip, is returned instead.
float film_curve(float value, const FilmCurve& curve) noexcept {
  if (value <= 0.0F) {
    return -curve.black_clip;
  }
  const float toe_scale = 1.0F + curve.black_clip - curve.toe;
  const float shoulder_scale = 1.0F + curve.white_clip - curve.shoulder;
  const detail::CurveMatch match = detail::curve_match(curve);
  const float toe_match = match.toe;
  const float shoulder_match = match.shoulder;
  const float straight_match = (1.0F - curve.toe) / curve.slope - toe_match;

  const float log_value = std::log10(value);
  const float straight = curve.slope * (log_value + straight_match);
  float toe = -curve.black_clip +
              (2.0F * toe_scale) /
                  (1.0F + std::exp((-2.0F * curve.slope / toe_scale) * (log_value - toe_match)));
  float shoulder =
      (1.0F + curve.white_clip) -
      (2.0F * shoulder_scale) /
          (1.0F + std::exp((2.0F * curve.slope / shoulder_scale) * (log_value - shoulder_match)));
  toe = log_value < toe_match ? toe : straight;
  shoulder = log_value > shoulder_match ? shoulder : straight;
  float t = saturate((log_value - toe_match) / (shoulder_match - toe_match));
  t = shoulder_match < toe_match ? 1.0F - t : t;
  t = (3.0F - 2.0F * t) * t * t;
  return toe + (shoulder - toe) * t;
}

// To AP0, the glow with gain 0.05 and mid 0.08 driven by the sigmoid of the
// saturation, the red modifier with scale 0.82, pivot 0.03, hue 0 and width
// 135, back to AP1 by the literal AP0 to AP1 matrix and clamped, the
// desaturation by 0.96, the curve, the desaturation by 0.93, the clamp.
// The saturation is taken before the glow and the hue after it.
Rgb film_tone_map(Rgb acescg, const FilmCurve& curve) noexcept {
  Rgb ap0 = transform(kAp1ToAp0Matrix, acescg);
  const float saturation = rgb_saturation(ap0);
  const float yc = rgb_yc(ap0);
  const float s = sigmoid_shaper((saturation - 0.4F) / 0.2F);
  ap0 = ap0 * (1.0F + glow_forward(yc, kGlowGain * s, kGlowMid));

  const float centred = center_hue(rgb_hue(ap0), kRedHue);
  const float edge = smoothstep(0.0F, 1.0F, 1.0F - std::fabs(2.0F * centred / kRedWidth));
  const float hue_weight = edge * edge;
  ap0.r += hue_weight * saturation * (kRedPivot - ap0.r) * (1.0F - kRedScale);

  Rgb working = max0(transform(kAp0ToAp1LiteralMatrix, ap0));
  working = lerp(splat(luminance(working)), working, kPreDesaturation);
  Rgb tone = per_channel(working, [&curve](float x) { return film_curve(x, curve); });
  tone = lerp(splat(luminance(tone)), tone, kPostDesaturation);
  return max0(tone);
}

float srgb_encode(float linear) noexcept {
  if (linear < 0.00313067F) {
    return linear * 12.92F;
  }
  return std::pow(linear, 1.0F / 2.4F) * 1.055F - 0.055F;
}

// White balance only away from 6500 K and tint 0, each by 1e-8; to AP1,
// the expansion, the correction; the film path, back to the working space
// clamped at 0; the sRGB encode. The encode is always the sRGB curve, never
// a plain 1 / 2.4 power.
Rgb grade_display(Rgb exposed, const Grade& grade) noexcept {
  Rgb balanced = exposed;
  if (std::fabs(grade.white_temperature - kNeutralTemperature) > kWhiteBalanceEpsilon ||
      std::fabs(grade.white_tint - kNeutralTint) > kWhiteBalanceEpsilon) {
    balanced = transform(white_balance_matrix(grade.white_temperature, grade.white_tint), exposed);
  }
  Rgb ap1 = rec709_to_acescg(balanced);
  ap1 = expand_gamut(ap1);
  ap1 = correct_color(ap1, grade);
  ap1 = blue_correct(ap1);
  ap1 = film_tone_map(ap1, grade.curve);
  ap1 = blue_uncorrect(ap1);
  const Rgb display = max0(acescg_to_rec709(ap1));
  return per_channel(display, srgb_encode);
}

// The display value is divided by the table's maximum.
Rgb bake_texel(Rgb encoded, const Grade& grade) noexcept {
  const Rgb exposed = per_channel(encoded, shaper_decode);
  const Rgb display = grade_display(exposed, grade) * (1.0F / kLookupScale);
  return per_channel(display, saturate);
}

} // namespace bw::postprocess
