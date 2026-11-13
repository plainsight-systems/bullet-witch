// A depth in [0, 1] packed to the code of a 16-bit float by integer
// arithmetic on the float's bits, so the code is the same on every target
// whatever a platform's own float-to-half conversion rounds by. The depth
// hierarchy's build stores its texels through it
// (bw/rendering/scene/depth_hierarchy.hpp, contract 3), and the culling
// dispatch's hierarchy test steps a box's nearest depth down through it
// (bw/rendering/scene/culling.hpp, contract 3).
//
// The code is that of the representable 16-bit float nearest the depth,
// ties to the even code, with 16-bit subnormals for depths below 2^-14 and 0
// for a depth below half the smallest of them. A depth at or below 0 gives
// code 0 and a depth at or above 1 the code of 1. The function is
// non-decreasing in its input: rounding to nearest never reorders two
// inputs. The value of a code is f16tof32, which is exact on every target.
#ifndef BW_SCENE_HALF_DEPTH_HLSLI
#define BW_SCENE_HALF_DEPTH_HLSLI

uint bw_depth_to_half(float depth) {
  if (!(depth > 0.0)) {
    return 0u;
  }
  if (depth >= 1.0) {
    return 0x3C00u;
  }
  const uint bits = asuint(depth);
  const uint exponent = (bits >> 23) & 0xFFu;
  const uint significand = (bits & 0x7FFFFFu) | 0x800000u;
  // Bits of the 24-bit significand that fall below the code's last place:
  // 13 for a normal 16-bit float (exponent 113 and up), more below.
  const uint dropped = exponent >= 113u ? 13u : 126u - exponent;
  if (dropped >= 25u) {
    // Below half the smallest subnormal: nearest is 0.
    return 0u;
  }
  uint code = significand >> dropped;
  if (exponent >= 113u) {
    // The implicit bit is replaced by the biased exponent.
    code = ((exponent - 112u) << 10) | (code & 0x3FFu);
  }
  const uint rest = significand & ((1u << dropped) - 1u);
  const uint half_place = 1u << (dropped - 1u);
  if (rest > half_place || (rest == half_place && (code & 1u) != 0u)) {
    // A carry out of the mantissa moves to the next exponent, which is the
    // next representable value.
    code += 1u;
  }
  return code;
}

#endif
