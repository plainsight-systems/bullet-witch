#include "bw/rendering/postprocess/grade.hpp"

#include "curve_match.hpp"

#include <array>
#include <cmath>
#include <initializer_list>

// Responsibility: validate_grade (bw/rendering/postprocess/grade.hpp,
// contract 2). The rules are checked one name at a time; which name is
// returned when several are broken is not part of the contract.
namespace bw::postprocess {

namespace {

[[nodiscard]] bool finite(Rgb value) noexcept {
  return std::isfinite(value.r) && std::isfinite(value.g) && std::isfinite(value.b);
}

[[nodiscard]] bool finite(const ColorCorrection& c) noexcept {
  return finite(c.saturation) && finite(c.contrast) && finite(c.gamma) && finite(c.gain) &&
         finite(c.offset);
}

[[nodiscard]] bool all_finite(const Grade& g) noexcept {
  for (const float value :
       {g.exposure_compensation, g.exposure_min_ev100, g.exposure_max_ev100, g.white_temperature,
        g.white_tint, g.shadows_max, g.highlights_min, g.highlights_max, g.curve.slope, g.curve.toe,
        g.curve.shoulder, g.curve.black_clip, g.curve.white_clip, g.bloom_intensity, g.vignette,
        g.grain}) {
    if (!std::isfinite(value)) {
      return false;
    }
  }
  return finite(g.whole) && finite(g.shadows) && finite(g.midtones) && finite(g.highlights);
}

[[nodiscard]] bool below_zero(Rgb value) noexcept {
  return value.r < 0.0F || value.g < 0.0F || value.b < 0.0F;
}

[[nodiscard]] bool not_above_zero(Rgb value) noexcept {
  return value.r <= 0.0F || value.g <= 0.0F || value.b <= 0.0F;
}

[[nodiscard]] bool correction_valid(const ColorCorrection& c) noexcept {
  return !below_zero(c.saturation) && !below_zero(c.contrast) && !below_zero(c.gain) &&
         !not_above_zero(c.gamma);
}

[[nodiscard]] bool unit_interval(float value) noexcept { return value >= 0.0F && value <= 1.0F; }

// The rule on the toe keeps the toe's match point defined: with the toe at
// most 0.8 the match solves an inverse hyperbolic tangent whose argument,
// (0.18 + black_clip) / toe_scale - 1, must stay below 1 (grade_bake.hpp,
// contract 7). Only then are the match points computed and compared.
[[nodiscard]] bool curve_valid(const FilmCurve& c) noexcept {
  if (c.slope <= 0.0F || !unit_interval(c.toe) || !unit_interval(c.shoulder) ||
      !unit_interval(c.black_clip) || !unit_interval(c.white_clip)) {
    return false;
  }
  const float toe_scale = 1.0F + c.black_clip - c.toe;
  const float shoulder_scale = 1.0F + c.white_clip - c.shoulder;
  if (toe_scale <= 0.0F || shoulder_scale <= 0.0F) {
    return false;
  }
  if (c.toe <= 0.8F && (0.18F + c.black_clip) / toe_scale >= 2.0F) {
    return false;
  }
  // The blend between the segments divides by the distance between the two
  // match points, so equal ones (toe + shoulder at or within rounding of 1)
  // are refused, computed as film_curve computes them.
  const detail::CurveMatch match = detail::curve_match(c);
  return match.toe != match.shoulder;
}

} // namespace

// The white balance fits hold from 1000 K to 15000 K and the tint within
// 0.05 of the locus. The exposure range is minimum and maximum EV100, and
// equal ends force the target.
GradeError validate_grade(const Grade& grade) noexcept {
  if (!all_finite(grade)) {
    return GradeError::NotFinite;
  }
  // Equal ends are a valid range: they pin the exposure (exposure.hpp,
  // contract 5).
  if (grade.exposure_min_ev100 > grade.exposure_max_ev100) {
    return GradeError::ExposureRangeInvalid;
  }
  if (grade.white_temperature < 1000.0F || grade.white_temperature > 15000.0F ||
      grade.white_tint < -1.0F || grade.white_tint > 1.0F) {
    return GradeError::WhiteBalanceOutOfRange;
  }
  for (const ColorCorrection* correction :
       std::array{&grade.whole, &grade.shadows, &grade.midtones, &grade.highlights}) {
    if (!correction_valid(*correction)) {
      return GradeError::CorrectionInvalid;
    }
  }
  if (grade.shadows_max <= 0.0F || grade.highlights_min >= grade.highlights_max) {
    return GradeError::RangeBoundsInvalid;
  }
  if (!curve_valid(grade.curve)) {
    return GradeError::CurveInvalid;
  }
  if (grade.bloom_intensity < 0.0F || grade.vignette < 0.0F) {
    return GradeError::AmountNegative;
  }
  if (grade.grain != 0.0F) {
    return GradeError::GrainUnavailable;
  }
  return GradeError::None;
}

} // namespace bw::postprocess
