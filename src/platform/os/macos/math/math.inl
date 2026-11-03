#ifndef BW_PLATFORM_MACOS_MATH_INL
#define BW_PLATFORM_MACOS_MATH_INL

// ARM Mac selects the generic scalar math. This is compile-time selection only:
// no forwarding function, ISA runtime switch or duplicated scalar implementation.
// Read SIMD.1/3: native NEON register kernels are a separate future ARM64 target
// boundary; do not place them here merely because the first OS target is Mac.
#include "../../../generic/math/math.inl"

#endif // BW_PLATFORM_MACOS_MATH_INL
