// Unit test: the bake's reference functions (bw/rendering/postprocess/
// grade_bake.hpp), the grade's validation (grade.hpp) and the vignette
// (tone_mapping.hpp), one scenario per contract, each a table of rows.
#include "bw/rendering/postprocess/grade.hpp"
#include "bw/rendering/postprocess/grade_bake.hpp"
#include "bw/rendering/postprocess/tone_mapping.hpp"

#include "bw/testing/harness.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <limits>

namespace {
namespace pp = bw::postprocess;
using bw::testing::require;
using pp::Rgb;
using Scenario = bw::testing::Scenario<void>;

bool near(float a, float b, float tolerance) { return std::fabs(a - b) <= tolerance; }
bool near(Rgb a, Rgb b, float tolerance) {
  return near(a.r, b.r, tolerance) && near(a.g, b.g, tolerance) && near(a.b, b.b, tolerance);
}
constexpr Rgb kGrey{0.18F, 0.18F, 0.18F};
constexpr float kY0 = 0.2722287168F; // ACEScg luminance weights (grade_bake.hpp)
constexpr float kY1 = 0.6740817658F;
constexpr float kY2 = 0.0536895174F;

// (grade_bake.hpp, contract 1): 0 decodes to 0, the grey code to 0.18 less
// the offset, 1 to the top of the 14 stops, and encode inverts decode.
void shaper() {
  require(pp::shaper_decode(0.0F) == 0.0F, "0 decodes to 0");
  require(near(pp::shaper_decode(pp::kShaperGreyCode), 0.17733228F, 1e-6F), "the grey code");
  require(near(pp::shaper_decode(1.0F), 43.705242F, 1e-3F), "the top of the range");
  for (const float e : {0.05F, 0.4F, 0.9F}) {
    require(near(pp::shaper_encode(pp::shaper_decode(e)), e, 1e-5F), "encode inverts decode");
  }
}

// (grade_bake.hpp, contract 3): 6500 K with no tint is D65 (near the
// identity), and a 3000 K white is balanced by moving white toward blue.
void white_balance() {
  const pp::ColorMatrix neutral = pp::white_balance_matrix(6500.0F, 0.0F);
  require(near(pp::transform(neutral, Rgb{1.0F, 0.0F, 0.0F}), Rgb{1.0F, 0.0F, 0.0F}, 2e-3F) &&
              near(pp::transform(neutral, Rgb{0.0F, 0.0F, 1.0F}), Rgb{0.0F, 0.0F, 1.0F}, 2e-3F),
          "6500 K is near the identity");
  const Rgb warm = pp::transform(pp::white_balance_matrix(3000.0F, 0.0F), Rgb{1.0F, 1.0F, 1.0F});
  require(warm.b > warm.g && warm.g > warm.r, "a warm white is cooled toward D65");
}

// (grade_bake.hpp, contracts 4, 6 and 8): every fixed step leaves a neutral
// colour neutral, and one saturated colour through expand_gamut,
// correct_color at the default terms, blue_correct, film_tone_map and
// blue_uncorrect gives the value computed by hand from the steps' formulas
// (a short script, saturated_colour.py, in the slice's research; it shows
// the expansion, the glow and the red modifier each moved it).
void fixed_steps() {
  const pp::Grade grade = pp::default_grade();
  for (const Rgb neutral : {kGrey, Rgb{2.0F, 2.0F, 2.0F}}) {
    require(near(pp::rec709_to_acescg(neutral), neutral, 1e-4F), "to ACEScg keeps white");
    require(near(pp::acescg_to_rec709(neutral), neutral, 1e-4F), "to Rec. 709 keeps white");
    require(near(pp::expand_gamut(neutral), neutral, 1e-5F), "expansion");
    require(near(pp::blue_correct(neutral), neutral, 1e-4F), "blue correction");
    require(near(pp::blue_uncorrect(neutral), neutral, 1e-4F), "inverse blue correction");
    const Rgb toned = pp::film_tone_map(neutral, grade.curve);
    require(near(toned.r, toned.g, 1e-5F) && near(toned.g, toned.b, 1e-5F), "film tone map");
  }
  Rgb c{0.18F, 0.035F, 0.025F};
  c = pp::blue_correct(pp::correct_color(pp::expand_gamut(c), grade));
  c = pp::blue_uncorrect(pp::film_tone_map(c, grade.curve));
  require(near(c, Rgb{0.141426337F, 0.017777801F, 0.011571587F}, 1e-6F),
          "the saturated colour through the fixed steps");
}

// (grade_bake.hpp, contract 5): the default terms are the identity,
// saturation 0 gives luminance, contrast pivots on 0.18, gain and offset
// scale and add, the ranges' weights sum to 1, and a shadow term leaves a
// colour above shadows_max alone.
void correction() {
  const Rgb mid{0.3F, 0.2F, 0.1F}; // Y about 0.222: midtones only
  const float y = kY0 * mid.r + kY1 * mid.g + kY2 * mid.b;
  pp::Grade grade = pp::default_grade();
  require(near(pp::correct_color(mid, grade), mid, 1e-5F), "defaults are the identity");
  grade.whole.saturation = Rgb{};
  require(near(pp::correct_color(mid, grade), Rgb{y, y, y}, 1e-5F), "saturation 0");
  grade = pp::default_grade();
  grade.whole.contrast = Rgb{2.0F, 2.0F, 2.0F};
  require(near(pp::correct_color(kGrey, grade), kGrey, 1e-5F), "contrast pivots on 0.18");
  grade = pp::default_grade();
  grade.whole.gain = Rgb{2.0F, 2.0F, 2.0F};
  grade.whole.offset = Rgb{0.1F, 0.1F, 0.1F};
  require(near(pp::correct_color(mid, grade), Rgb{0.7F, 0.5F, 0.3F}, 1e-5F), "gain, offset");
  grade = pp::default_grade();
  grade.shadows.offset = grade.midtones.offset = grade.highlights.offset = Rgb{0.05F, 0.05F, 0.05F};
  for (const Rgb c : {Rgb{0.02F, 0.03F, 0.01F}, Rgb{0.08F, 0.1F, 0.06F}, Rgb{0.7F, 0.6F, 0.5F}}) {
    require(near(pp::correct_color(c, grade), Rgb{c.r + 0.05F, c.g + 0.05F, c.b + 0.05F}, 1e-5F),
            "the ranges' weights sum to 1");
  }
  grade = pp::default_grade();
  grade.shadows.gain = Rgb{3.0F, 3.0F, 3.0F};
  require(near(pp::correct_color(mid, grade), mid, 1e-5F), "above shadows_max: unchanged");
  const Rgb dark{0.02F, 0.03F, 0.01F};
  require(!near(pp::correct_color(dark, grade), dark, 1e-3F), "below it: changed");
}

// (grade_bake.hpp, contract 7) under the default curve: -black_clip at 0,
// 0.18 at 0.18, and toward 1 + white_clip; the values at the toe and
// shoulder match points were computed by hand from the curve's formulas.
void curve() {
  const pp::FilmCurve d{};
  pp::FilmCurve clipped{};
  clipped.black_clip = 0.1F;
  require(pp::film_curve(0.0F, d) == 0.0F && pp::film_curve(0.0F, clipped) == -0.1F,
          "-black_clip at 0");
  require(near(pp::film_curve(0.18F, d), 0.18F, 1e-5F), "0.18 at 0.18");
  require(near(pp::film_curve(0.407120309F, d), 0.446329156F, 1e-6F), "at the toe match");
  require(near(pp::film_curve(0.247635941F, d), 0.270539603F, 1e-6F), "at the shoulder match");
  require(near(pp::film_curve(1e6F, d), 1.04F, 1e-4F), "toward 1 + white_clip");
}

// (grade_bake.hpp, contracts 9 and 10): the encode's two pieces, and the
// whole chain at the default grade takes middle grey to its encode.
void encode_and_whole() {
  require(near(pp::srgb_encode(0.003F), 0.03876F, 1e-6F), "linear below the break");
  require(near(pp::srgb_encode(0.18F), 0.461356F, 1e-5F) &&
              near(pp::srgb_encode(1.0F), 1.0F, 1e-6F),
          "the power above it");
  const float grey = pp::srgb_encode(0.18F);
  require(near(pp::grade_display(kGrey, pp::default_grade()), Rgb{grey, grey, grey}, 1e-4F),
          "middle grey through the default grade");
  require(pp::bake_texel(Rgb{}, pp::default_grade()) == Rgb{}, "black stores 0");
}

// (grade.hpp, contracts 1 and 2): the default grade validates, and each
// rule broken alone is named.
void grade_rules() {
  require(pp::validate_grade(pp::default_grade()) == pp::GradeError::None, "default validates");
  struct Row {
    void (*breaks)(pp::Grade&);
    pp::GradeError error;
  };
  using E = pp::GradeError;
  const std::array rows{
      Row{[](pp::Grade& g) { g.vignette = std::numeric_limits<float>::quiet_NaN(); }, E::NotFinite},
      Row{[](pp::Grade& g) { g.exposure_min_ev100 = 21.0F; }, E::ExposureRangeInvalid},
      Row{[](pp::Grade& g) { g.white_temperature = 900.0F; }, E::WhiteBalanceOutOfRange},
      Row{[](pp::Grade& g) { g.midtones.gamma.g = 0.0F; }, E::CorrectionInvalid},
      Row{[](pp::Grade& g) { g.highlights_min = 1.0F; }, E::RangeBoundsInvalid},
      Row{[](pp::Grade& g) { g.curve.slope = 0.0F; }, E::CurveInvalid},
      Row{[](pp::Grade& g) { g.bloom_intensity = -0.1F; }, E::AmountNegative},
      Row{[](pp::Grade& g) { g.grain = 0.5F; }, E::GrainUnavailable},
  };
  for (std::size_t i = 0; i < rows.size(); ++i) {
    pp::Grade grade = pp::default_grade();
    rows[i].breaks(grade);
    require(pp::validate_grade(grade) == rows[i].error, i, "named rule");
  }
}

// (tone_mapping.hpp, contract 3): 1 at the centre, the same at the corners
// whatever the aspect, and 1 everywhere at intensity 0.
void vignette() {
  const float corner = pp::vignette_mask(0.0F, 0.0F, 1.0F, 0.4F);
  require(corner < 1.0F, "the corner is darker");
  for (const float aspect : {0.5625F, 1.0F, 2.0F}) {
    require(pp::vignette_mask(0.5F, 0.5F, aspect, 0.4F) == 1.0F, "1 at the centre");
    require(near(pp::vignette_mask(1.0F, 0.0F, aspect, 0.4F), corner, 1e-6F), "corners agree");
    require(pp::vignette_mask(1.0F, 1.0F, aspect, 0.0F) == 1.0F, "intensity 0");
  }
}

constexpr std::array scenarios{
    Scenario{"shaper", shaper},
    Scenario{"white_balance", white_balance},
    Scenario{"fixed_steps", fixed_steps},
    Scenario{"correction", correction},
    Scenario{"curve", curve},
    Scenario{"encode_and_whole", encode_and_whole},
    Scenario{"grade_rules", grade_rules},
    Scenario{"vignette", vignette},
};
} // namespace

int main(int argc, char** argv) { return bw::testing::run_scenario(argc, argv, scenarios); }
