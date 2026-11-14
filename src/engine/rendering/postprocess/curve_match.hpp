#ifndef BW_RENDERING_POSTPROCESS_CURVE_MATCH_HPP
#define BW_RENDERING_POSTPROCESS_CURVE_MATCH_HPP

#include "bw/rendering/postprocess/grade.hpp"

// Responsibility: the film curve's two match points, computed once for
// both their users: film_curve (grade_bake.cpp; grade_bake.hpp, contract
// 7), which blends its segments over the distance between them, and
// validate_grade (grade.cpp; grade.hpp, contract 2), which names
// CurveInvalid when they compare equal, so a grade that validates never
// reaches a blend over a zero distance. Not a public header: nothing
// outside the postprocess module's directory includes it.
namespace bw::postprocess::detail {

// In log10 of the curve's input.
struct CurveMatch {
  float toe{};
  float shoulder{};
};

// Precondition: slope above 0, 1 + black_clip - toe above 0, and with toe
// at most 0.8, (0.18 + black_clip) / (1 + black_clip - toe) below 2.
[[nodiscard]] CurveMatch curve_match(const FilmCurve& curve) noexcept;

} // namespace bw::postprocess::detail

#endif
