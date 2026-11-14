// The grade's bake (bw/rendering/postprocess/grade_bake.hpp, contracts 10
// and 11; post_shaders.hpp, PostGradeBake). 4 by 4 by 4 threads a group
// (grade_bake.cpp, kBakeGroupSize), each thread one texel of the table.
//
// Registry entry (src/engine/rendering/shaders/permutation.cpp,
// PostGradeBakeShader): compute, entry main, per-draw block PostGradeBake,
// no static block, no buffer block.
//
// Texel (x, y, z) stores bake_texel of the encoded coordinate
// (x, y, z) / (kLookupSize - 1): the shaper decode, the white balance when
// apply_white_balance is set, to ACEScg, gamut expansion, the correction by
// tonal range, the blue correction, the film tone map, the inverse blue
// correction, back to Rec. 709 clamped at 0, the sRGB encode, and the
// division by kLookupScale, saturated. The texel is packed as the
// R10G10B10A2Unorm word, red in bits 0 to 9, green 10 to 19, blue 20 to 29
// and alpha 30 to 31, each the nearest level, alpha 1; and written to the
// storage buffer PostGradeBake.table at z * BW_LOOKUP_SLICE_PITCH + y *
// BW_LOOKUP_ROW_PITCH + x * 4, the layout grade_bake.cpp copies into the
// table (a 3D texture has no storage array; grade_bake.hpp, Note).
//
// The bake has no tone curve amount, mapping polynomial, display gamma power
// or output gamut conversion: each would be the identity for an sRGB target,
// and no grade sets one. The table serves an sRGB surface only, and its
// encode is the sRGB curve, not a 1 / 2.4 power.
#include "bw/bindless.hlsli"
#include "bw/generated/post_grade_bake.hlsli"
#include "bw/postprocess/grade.hlsli"

// grade_bake.cpp, kTableRowPitch and kTableSlicePitch: a row of 32 words at
// a 256-byte pitch, a slice of 32 rows.
#define BW_LOOKUP_ROW_PITCH 256u
#define BW_LOOKUP_SLICE_PITCH (BW_LOOKUP_ROW_PITCH * BW_LOOKUP_SIZE)

BwCorrection bw_shadows() {
  BwCorrection c;
  c.saturation = PostGradeBake.shadows_saturation;
  c.contrast = PostGradeBake.shadows_contrast;
  c.gamma = PostGradeBake.shadows_gamma;
  c.gain = PostGradeBake.shadows_gain;
  c.offset = PostGradeBake.shadows_offset;
  return c;
}

BwCorrection bw_midtones() {
  BwCorrection c;
  c.saturation = PostGradeBake.midtones_saturation;
  c.contrast = PostGradeBake.midtones_contrast;
  c.gamma = PostGradeBake.midtones_gamma;
  c.gain = PostGradeBake.midtones_gain;
  c.offset = PostGradeBake.midtones_offset;
  return c;
}

BwCorrection bw_highlights() {
  BwCorrection c;
  c.saturation = PostGradeBake.highlights_saturation;
  c.contrast = PostGradeBake.highlights_contrast;
  c.gamma = PostGradeBake.highlights_gamma;
  c.gain = PostGradeBake.highlights_gain;
  c.offset = PostGradeBake.highlights_offset;
  return c;
}

BwFilmCurve bw_curve() {
  BwFilmCurve curve;
  curve.slope = PostGradeBake.slope;
  curve.toe = PostGradeBake.toe;
  curve.shoulder = PostGradeBake.shoulder;
  curve.black_clip = PostGradeBake.black_clip;
  curve.white_clip = PostGradeBake.white_clip;
  return curve;
}

// grade_bake.hpp, grade_display.
float3 bw_grade_display(float3 exposed) {
  float3 balanced = exposed;
  if (PostGradeBake.apply_white_balance != 0u) {
    balanced = float3(dot(PostGradeBake.white_balance_row0, exposed),
                      dot(PostGradeBake.white_balance_row1, exposed),
                      dot(PostGradeBake.white_balance_row2, exposed));
  }
  float3 ap1 = mul(BW_REC709_TO_AP1, balanced);
  ap1 = bw_expand_gamut(ap1);
  ap1 =
      bw_correct_color(ap1, bw_shadows(), bw_midtones(), bw_highlights(), PostGradeBake.shadows_max,
                       PostGradeBake.highlights_min, PostGradeBake.highlights_max);
  ap1 = bw_blue_correct(ap1);
  ap1 = bw_film_tone_map(ap1, bw_curve());
  ap1 = bw_blue_uncorrect(ap1);
  const float3 display = max(mul(BW_AP1_TO_REC709, ap1), 0.0);
  return float3(bw_srgb_encode(display.r), bw_srgb_encode(display.g), bw_srgb_encode(display.b));
}

uint bw_pack_unorm10(float value) { return uint(round(saturate(value) * 1023.0)); }

[numthreads(4, 4, 4)] void main(uint3 texel : SV_DispatchThreadID) {
  if (any(texel >= BW_LOOKUP_SIZE)) {
    return;
  }
  const float3 encoded = float3(texel) / float(BW_LOOKUP_SIZE - 1u);
  const float3 stored =
      saturate(bw_grade_display(bw_shaper_decode(encoded)) * (1.0 / BW_LOOKUP_SCALE));
  const uint word = bw_pack_unorm10(stored.r) | (bw_pack_unorm10(stored.g) << 10) |
                    (bw_pack_unorm10(stored.b) << 20) | (3u << 30);
  BW_STORAGE_BUFFER(PostGradeBake.table)
      .Store(texel.z * BW_LOOKUP_SLICE_PITCH + texel.y * BW_LOOKUP_ROW_PITCH + texel.x * 4u, word);
}
