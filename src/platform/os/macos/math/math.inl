#ifndef BW_PLATFORM_MACOS_MATH_INL
#define BW_PLATFORM_MACOS_MATH_INL
// The tail of bw/platform/math/math.hpp. Opened alone it includes that header,
// which leaves this file out while the guard above is defined.
#include "bw/platform/math/math.hpp"

// ARM Mac selects the generic scalar math. This is compile-time selection only:
// no forwarding function, ISA runtime switch or duplicated scalar implementation.
// Read SIMD.1/3: native NEON register kernels are a separate future ARM64 target
// boundary; do not place them here merely because the first OS target is Mac.
#ifndef BW_PLATFORM_GENERIC_MATH_INL
#include "../../../generic/math/math.inl"
#endif
#endif // BW_PLATFORM_MACOS_MATH_INL
