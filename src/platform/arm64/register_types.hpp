#ifndef BW_PLATFORM_ARM64_REGISTER_TYPES_HPP
#define BW_PLATFORM_ARM64_REGISTER_TYPES_HPP

#include <arm_neon.h>

// Read F.20/I.1 and SIMD.3/GEN.6: registers own values; native layout stays at
// the silicon boundary. No heap, dispatch table or portable scalar fallback.
// Unreal f45729c0b6f097038e72b52d366a62baeca3275b, UnrealMathNeon.h:115–120,144–169:
// float uses one 128-bit register; double uses aligned XY then ZW register pairs.
namespace bw::platform::arm64 {
using Register4f = float32x4_t;
struct alignas(16) Register4d {
  float64x2_t xy;
  float64x2_t zw;
};
// Aggregate declaration only: no numerical initialization or conversion body.
} // namespace bw::platform::arm64
#endif // BW_PLATFORM_ARM64_REGISTER_TYPES_HPP
