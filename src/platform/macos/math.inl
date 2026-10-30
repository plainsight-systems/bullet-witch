#ifndef BW_PLATFORM_MACOS_MATH_INL
#define BW_PLATFORM_MACOS_MATH_INL

// Unreal f45729c0b6f097038e72b52d366a62baeca3275b:
// MacPlatformMath.h:27–29 -> ClangPlatformMath.h:11–19 selects the inherited
// generic scalar math on ARM Mac. This is compile-time selection only: no
// forwarding function, ISA runtime switch or duplicated scalar implementation.
// Read SIMD.1/3: native NEON register kernels are a separate future ARM64 target
// boundary; do not place them here merely because the first OS target is Mac.
#include "platform/generic/math.inl"

#endif // BW_PLATFORM_MACOS_MATH_INL
