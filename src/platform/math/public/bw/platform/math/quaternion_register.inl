#ifndef BW_PLATFORM_MATH_QUATERNION_REGISTER_INL
#define BW_PLATFORM_MATH_QUATERNION_REGISTER_INL
// The tail of bw/math/rotation/quaternion_register.hpp. Opened alone it includes that header,
// which leaves this file out while the guard above is defined.
#include "bw/math/rotation/quaternion_register.hpp"

// Platform owns compile-time silicon selection, as for the accepted registers.
#if defined(__aarch64__)
#ifndef BW_PLATFORM_ARM64_QUATERNION_REGISTER_INL
#include "../../../../../cpu/arm64/math/simd/quaternion_register.inl"
#endif
#else
#error "Quaternion register implementation requires an inspected target backend."
#endif
#endif // BW_PLATFORM_MATH_QUATERNION_REGISTER_INL
