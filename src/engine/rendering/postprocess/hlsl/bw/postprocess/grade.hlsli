// The grade's bake steps as the bake kernel computes them
// (bw/rendering/postprocess/grade_bake.hpp, contracts 1 and 4 to 10), and
// the shaper encode and lookup coordinate the tone mapping pass reads the
// table with (contract 2). The reference functions of grade_bake.cpp state
// every step and digit; each function here follows one of them in the same
// order and in 32-bit floats. The white balance and the per-range terms are
// resolved on the CPU into the kernel's block (post_shaders.hpp,
// PostGradeBake), so they are arguments here.
//
// The fixed matrices are grade_bake.cpp's, composed there in double
// precision from their stated digits and rounded once to 32-bit floats;
// the digits below are those floats.
#ifndef BW_POSTPROCESS_GRADE_HLSLI
#define BW_POSTPROCESS_GRADE_HLSLI

// grade_bake.hpp: kLookupSize, kLookupScale, kShaperStops, kShaperGrey,
// kShaperGreyCode, kBlueCorrection, kPreDesaturation, kPostDesaturation.
#define BW_LOOKUP_SIZE 32u
static const float BW_LOOKUP_SCALE = 1.05;
static const float BW_SHAPER_STOPS = 14.0;
static const float BW_SHAPER_GREY = 0.18;
static const float BW_SHAPER_GREY_CODE = 444.0 / 1023.0;
static const float BW_BLUE_CORRECTION = 0.6;
static const float BW_PRE_DESATURATION = 0.96;
static const float BW_POST_DESATURATION = 0.93;

// AP1's luminance; Rec. 709 to AP1 and back, each through the Bradford
// adaptation between D65 and the ACES white; AP1 to AP0; the blue
// correction and its inverse, moved from AP0 into AP1; the expansion as
// wide to AP1 times AP1 to Rec. 709.
static const float3 BW_AP1_LUMINANCE = float3(0.2722287168, 0.6740817658, 0.0536895174);
static const float3x3 BW_REC709_TO_AP1 =
    float3x3(0.6130974, 0.33952314, 0.047379453, 0.07019372, 0.9163539, 0.013452399, 0.020615593,
             0.10956977, 0.86981463);
static const float3x3 BW_AP1_TO_REC709 =
    float3x3(1.705051, -0.62179214, -0.083258875, -0.13025641, 1.1408048, -0.010548319,
             -0.024003357, -0.12896897, 1.1529723);
static const float3x3 BW_AP1_TO_AP0 =
    float3x3(0.6954522, 0.1406787, 0.16386907, 0.044794563, 0.8596711, 0.09553432, -0.0055258824,
             0.00402521, 1.0015007);
// The literal AP0 to AP1 matrix of the film tone map.
static const float3x3 BW_AP0_TO_AP1 =
    float3x3(1.4514393, -0.23651075, -0.21492857, -0.07655378, 1.1762297, -0.09967592, 0.008316148,
             -0.0060324497, 0.9977163);
static const float3x3 BW_BLUE_CORRECT =
    float3x3(0.9386394, 1.02359565e-10, 0.061360624, 8.3600855e-11, 0.83079416, 0.16920587,
             2.1318737e-12, -5.633072e-12, 1.0);
static const float3x3 BW_BLUE_UNCORRECT =
    float3x3(1.0653749, 1.4467851e-06, -0.06537101, -3.455256e-07, 1.2036635, -0.20366772,
             1.9865448e-08, 2.1207958e-08, 0.9999996);
static const float3x3 BW_EXPAND_GAMUT =
    float3x3(1.3704123, -0.32929218, -0.06368312, -0.083433494, 1.0970927, -0.010861379,
             -0.025793321, -0.0986258, 1.2036949);

// The glow's gain 0.05 and mid 0.08, the red modifier's scale 0.82, pivot
// 0.03, hue 0 and width 135, and YC's radius weight 1.75.
static const float BW_GLOW_GAIN = 0.05;
static const float BW_GLOW_MID = 0.08;
static const float BW_RED_SCALE = 0.82;
static const float BW_RED_PIVOT = 0.03;
static const float BW_RED_HUE = 0.0;
static const float BW_RED_WIDTH = 135.0;
static const float BW_YC_RADIUS_WEIGHT = 1.75;
static const float BW_CURVE_MATCH = 0.18;
static const float BW_PI = 3.14159265358979;

float bw_luminance(float3 acescg) { return dot(acescg, BW_AP1_LUMINANCE); }

// The log shaper's decode, before the offset that makes 0 decode to 0.
float bw_log_to_linear(float encoded) {
  return exp2((encoded - BW_SHAPER_GREY_CODE) * BW_SHAPER_STOPS) * BW_SHAPER_GREY;
}

// The decode of e less the decode of 0, so that 0 decodes to 0.
float3 bw_shaper_decode(float3 encoded) {
  const float offset = bw_log_to_linear(0.0);
  return float3(bw_log_to_linear(encoded.r) - offset, bw_log_to_linear(encoded.g) - offset,
                bw_log_to_linear(encoded.b) - offset);
}

// The log shaper's encode; the decode's offset is added back before it.
// bw_lookup_coordinate then applies the coordinate's scale and offset.
float3 bw_shaper_encode(float3 linear_color) {
  const float offset = bw_log_to_linear(0.0);
  return saturate(log2(linear_color + offset) / BW_SHAPER_STOPS -
                  log2(BW_SHAPER_GREY) / BW_SHAPER_STOPS + BW_SHAPER_GREY_CODE);
}

float3 bw_lookup_coordinate(float3 encoded) {
  return encoded * (float(BW_LOOKUP_SIZE - 1u) / float(BW_LOOKUP_SIZE)) +
         0.5 / float(BW_LOOKUP_SIZE);
}

// The amount comes from the chroma distance and the luminance, at full
// expansion, and lerps toward the expansion matrix.
// A colour with no luminance is returned as it is: its chroma is undefined.
float3 bw_expand_gamut(float3 acescg) {
  const float luma = bw_luminance(acescg);
  if (luma == 0.0) {
    return acescg;
  }
  const float3 from_neutral = acescg * (1.0 / luma) - 1.0;
  const float distance = dot(from_neutral, from_neutral);
  const float amount = (1.0 - exp2(-4.0 * distance)) * (1.0 - exp2(-4.0 * luma * luma));
  return lerp(acescg, mul(BW_EXPAND_GAMUT, acescg), amount);
}

// One tonal range's terms, already the range's times the whole range's.
struct BwCorrection {
  float3 saturation;
  float3 contrast;
  float3 gamma;
  float3 gain;
  float3 offset;
};

// One range's correction: saturation about luminance, contrast about 0.18,
// gamma, gain, offset.
float3 bw_correct_range(float3 c, BwCorrection terms) {
  const float luma = bw_luminance(c);
  c = max(luma + (c - luma) * terms.saturation, 0.0);
  c = pow(c / 0.18, terms.contrast) * 0.18;
  c = pow(c, 1.0 / terms.gamma);
  return c * terms.gain + terms.offset;
}

// The three ranges weighted by luminance.
float3 bw_correct_color(float3 acescg, BwCorrection shadows, BwCorrection midtones,
                        BwCorrection highlights, float shadows_max, float highlights_min,
                        float highlights_max) {
  const float luma = bw_luminance(acescg);
  const float3 shadowed = bw_correct_range(acescg, shadows);
  const float shadows_weight = 1.0 - smoothstep(0.0, shadows_max, luma);
  const float3 highlighted = bw_correct_range(acescg, highlights);
  const float highlights_weight = smoothstep(highlights_min, highlights_max, luma);
  const float3 middle = bw_correct_range(acescg, midtones);
  const float midtones_weight = 1.0 - shadows_weight - highlights_weight;
  return shadowed * shadows_weight + middle * midtones_weight + highlighted * highlights_weight;
}

// The blue correction at 0.6, applied as a lerp toward each matrix.
float3 bw_blue_correct(float3 acescg) {
  return lerp(acescg, mul(BW_BLUE_CORRECT, acescg), BW_BLUE_CORRECTION);
}
float3 bw_blue_uncorrect(float3 acescg) {
  return lerp(acescg, mul(BW_BLUE_UNCORRECT, acescg), BW_BLUE_CORRECTION);
}

// The film curve's five parameters (grade.hpp, FilmCurve).
struct BwFilmCurve {
  float slope;
  float toe;
  float shoulder;
  float black_clip;
  float white_clip;
};

// The scales, the match points (0.18 in to 0.18 out), the three segments,
// their selection and the smoothed blend.
// At 0 the logarithm is minus infinity; the curve's limit there,
// -black_clip, is returned instead.
float bw_film_curve(float value, BwFilmCurve curve) {
  if (value <= 0.0) {
    return -curve.black_clip;
  }
  const float toe_scale = 1.0 + curve.black_clip - curve.toe;
  const float shoulder_scale = 1.0 + curve.white_clip - curve.shoulder;
  float toe_match;
  if (curve.toe > 0.8) {
    toe_match = (1.0 - curve.toe - BW_CURVE_MATCH) / curve.slope + log10(BW_CURVE_MATCH);
  } else {
    const float bt = (BW_CURVE_MATCH + curve.black_clip) / toe_scale - 1.0;
    toe_match =
        log10(BW_CURVE_MATCH) - 0.5 * log((1.0 + bt) / (1.0 - bt)) * (toe_scale / curve.slope);
  }
  const float straight_match = (1.0 - curve.toe) / curve.slope - toe_match;
  const float shoulder_match = curve.shoulder / curve.slope - straight_match;

  const float log_value = log10(value);
  const float straight = curve.slope * (log_value + straight_match);
  float toe =
      -curve.black_clip +
      (2.0 * toe_scale) / (1.0 + exp((-2.0 * curve.slope / toe_scale) * (log_value - toe_match)));
  float shoulder =
      (1.0 + curve.white_clip) -
      (2.0 * shoulder_scale) /
          (1.0 + exp((2.0 * curve.slope / shoulder_scale) * (log_value - shoulder_match)));
  toe = log_value < toe_match ? toe : straight;
  shoulder = log_value > shoulder_match ? shoulder : straight;
  float t = saturate((log_value - toe_match) / (shoulder_match - toe_match));
  t = shoulder_match < toe_match ? 1.0 - t : t;
  t = (3.0 - 2.0 * t) * t * t;
  return toe + (shoulder - toe) * t;
}

float bw_rgb_saturation(float3 c) {
  const float lo = min(c.r, min(c.g, c.b));
  const float hi = max(c.r, max(c.g, c.b));
  return (max(hi, 1e-10) - max(lo, 1e-10)) / max(hi, 1e-2);
}

// 0 for a neutral colour.
float bw_rgb_hue(float3 c) {
  float hue = 0.0;
  if (!(c.r == c.g && c.g == c.b)) {
    hue = (180.0 / BW_PI) * atan2(sqrt(3.0) * (c.g - c.b), 2.0 * c.r - c.g - c.b);
  }
  if (hue < 0.0) {
    hue += 360.0;
  }
  return clamp(hue, 0.0, 360.0);
}

float bw_center_hue(float hue, float centre) {
  float centred = hue - centre;
  if (centred < -180.0) {
    centred += 360.0;
  } else if (centred > 180.0) {
    centred -= 360.0;
  }
  return centred;
}

// The luminance proxy YC.
// The radicand is half the sum of the squared channel differences; the clamp
// at 0 only removes a rounding below it.
float bw_rgb_yc(float3 c) {
  const float radicand = c.b * (c.b - c.g) + c.g * (c.g - c.r) + c.r * (c.r - c.b);
  const float chroma = sqrt(max(radicand, 0.0));
  return (c.b + c.g + c.r + BW_YC_RADIUS_WEIGHT * chroma) / 3.0;
}

float bw_sigmoid_shaper(float x) {
  const float t = max(1.0 - abs(0.5 * x), 0.0);
  const float s = x > 0.0 ? 1.0 : (x < 0.0 ? -1.0 : 0.0);
  return 0.5 * (1.0 + s * (1.0 - t * t));
}

float bw_glow_forward(float yc, float gain, float mid) {
  if (yc <= 2.0 / 3.0 * mid) {
    return gain;
  }
  if (yc >= 2.0 * mid) {
    return 0.0;
  }
  return gain * (mid / yc - 0.5);
}

// To AP0, the glow driven by the sigmoid of the saturation, the red
// modifier, back to AP1 by the literal AP0 to AP1 matrix and clamped, the
// desaturation by 0.96, the curve, the desaturation by 0.93, the clamp.
// The saturation is taken before the glow and the hue after it.
float3 bw_film_tone_map(float3 acescg, BwFilmCurve curve) {
  float3 ap0 = mul(BW_AP1_TO_AP0, acescg);
  const float saturation = bw_rgb_saturation(ap0);
  const float yc = bw_rgb_yc(ap0);
  const float s = bw_sigmoid_shaper((saturation - 0.4) / 0.2);
  ap0 = ap0 * (1.0 + bw_glow_forward(yc, BW_GLOW_GAIN * s, BW_GLOW_MID));

  const float centred = bw_center_hue(bw_rgb_hue(ap0), BW_RED_HUE);
  const float edge = smoothstep(0.0, 1.0, 1.0 - abs(2.0 * centred / BW_RED_WIDTH));
  const float hue_weight = edge * edge;
  ap0.r += hue_weight * saturation * (BW_RED_PIVOT - ap0.r) * (1.0 - BW_RED_SCALE);

  float3 working = max(mul(BW_AP0_TO_AP1, ap0), 0.0);
  working = lerp(bw_luminance(working), working, BW_PRE_DESATURATION);
  float3 tone = float3(bw_film_curve(working.r, curve), bw_film_curve(working.g, curve),
                       bw_film_curve(working.b, curve));
  tone = lerp(bw_luminance(tone), tone, BW_POST_DESATURATION);
  return max(tone, 0.0);
}

float bw_srgb_encode(float linear_value) {
  if (linear_value < 0.00313067) {
    return linear_value * 12.92;
  }
  return pow(linear_value, 1.0 / 2.4) * 1.055 - 0.055;
}

#endif
