#ifndef BW_RENDERING_POSTPROCESS_GRADE_HPP
#define BW_RENDERING_POSTPROCESS_GRADE_HPP

#include <cstdint>

// Responsibility: the grade as a value (docs/architecture/post-processing.md,
// invariant 7): a stage's authored look, the parameters the bake turns into
// one lookup table (grade_bake.hpp) and the three the frame reads directly
// (the compensation, exposure.hpp; bloom intensity and vignette,
// tone_mapping.hpp). default_grade() is the grade a stage has when none is
// authored. Nothing here reads content: the grade payload and its reader are
// not in this module yet.
//
// Contract.
//   1. Fields and defaults. exposure_compensation is in stops, default 1.
//      exposure_min_ev100 and exposure_max_ev100, defaults -10 and 20, are
//      the range the exposure may follow the meter in (exposure.hpp,
//      contract 5); a stage sets them to its light, and equal ends pin
//      the exposure.
//      white_temperature is in kelvin, default 6500, with white_tint
//      default 0: the white the image is balanced from, toward D65.
//      Colour correction is five terms, saturation, contrast, gamma, gain
//      and offset, each one value per channel, given once for the whole
//      range and once each for shadows, midtones and highlights; the first
//      four default to 1 and the offset to 0. shadows_max, default 0.09,
//      and highlights_min and highlights_max, defaults 0.5 and 1, are the
//      luminances that divide the three ranges. The curve is slope 0.88,
//      toe 0.55, shoulder 0.26, black_clip 0 and white_clip 0.04.
//      bloom_intensity defaults to 0.0894, the share of a uniform field
//      the bloom adds (bloom.hpp, contract 2), vignette to 0.4 and grain
//      to 0.
//      default_grade() is a Grade of exactly these defaults.
//   2. validate_grade names a rule a grade breaks, None when it breaks
//      none:
//        NotFinite              a field is NaN or infinite
//        ExposureRangeInvalid   exposure_min_ev100 above
//                               exposure_max_ev100
//        WhiteBalanceOutOfRange white_temperature outside 1000 to 15000,
//                               or white_tint outside -1 to 1
//        CorrectionInvalid      a saturation, contrast or gain below 0 or
//                               a gamma not above 0, in any of the four
//                               sets, on any channel
//        RangeBoundsInvalid     shadows_max not above 0, or highlights_min
//                               not below highlights_max
//        CurveInvalid           slope not above 0; toe, shoulder,
//                               black_clip or white_clip outside 0 to 1;
//                               1 + black_clip - toe or 1 + white_clip -
//                               shoulder not above 0; with toe at most
//                               0.8, (0.18 + black_clip) / (1 + black_clip
//                               - toe) not below 2; or the curve's toe
//                               match and shoulder match, as film_curve
//                               computes them in 32-bit floats
//                               (grade_bake.hpp, contract 7), equal, which
//                               toe + shoulder at or within rounding of 1
//                               gives
//        AmountNegative         bloom_intensity or vignette below 0
//        GrainUnavailable       grain other than 0
//      Which is named when a grade breaks several is not part of the
//      contract. A grade that validates is one every function of
//      grade_bake.hpp is defined for.
//
// Notes. Each correction term is one value per channel, with no separate
// master value. Film grain fails by name: this module has no grain
// texture, so a grade that asks for grain is refused rather than drawn
// without it. The bake's fixed steps (blue correction, gamut expansion)
// are constants in grade_bake.hpp, not grade fields: invariant 7 lists no
// parameter for them, and it gives the vignette one value.
//
// Core guidelines read: C.131 (plain data), Con.5, ES.45 (every default is
// a member initializer), E.27 (a named error per rule).
namespace bw::postprocess {

// A colour or a per-channel term, linear, in the space its user states.
struct Rgb {
  float r{};
  float g{};
  float b{};
  [[nodiscard]] friend constexpr bool operator==(const Rgb&, const Rgb&) noexcept = default;
};

struct ColorCorrection {
  Rgb saturation{1.0F, 1.0F, 1.0F};
  Rgb contrast{1.0F, 1.0F, 1.0F};
  Rgb gamma{1.0F, 1.0F, 1.0F};
  Rgb gain{1.0F, 1.0F, 1.0F};
  Rgb offset{0.0F, 0.0F, 0.0F};
  [[nodiscard]] friend constexpr bool operator==(const ColorCorrection&,
                                                 const ColorCorrection&) noexcept = default;
};

struct FilmCurve {
  float slope{0.88F};
  float toe{0.55F};
  float shoulder{0.26F};
  float black_clip{0.0F};
  float white_clip{0.04F};
  [[nodiscard]] friend constexpr bool operator==(const FilmCurve&,
                                                 const FilmCurve&) noexcept = default;
};

struct Grade {
  float exposure_compensation{1.0F};
  float exposure_min_ev100{-10.0F};
  float exposure_max_ev100{20.0F};
  float white_temperature{6500.0F};
  float white_tint{0.0F};
  ColorCorrection whole{};
  ColorCorrection shadows{};
  ColorCorrection midtones{};
  ColorCorrection highlights{};
  float shadows_max{0.09F};
  float highlights_min{0.5F};
  float highlights_max{1.0F};
  FilmCurve curve{};
  float bloom_intensity{0.0894F};
  float vignette{0.4F};
  float grain{0.0F};
  [[nodiscard]] friend constexpr bool operator==(const Grade&, const Grade&) noexcept = default;
};

[[nodiscard]] constexpr Grade default_grade() noexcept { return Grade{}; }

enum class GradeError : std::uint8_t {
  None,
  NotFinite,
  ExposureRangeInvalid,
  WhiteBalanceOutOfRange,
  CorrectionInvalid,
  RangeBoundsInvalid,
  CurveInvalid,
  AmountNegative,
  GrainUnavailable,
};

[[nodiscard]] GradeError validate_grade(const Grade& grade) noexcept;

} // namespace bw::postprocess

#endif
