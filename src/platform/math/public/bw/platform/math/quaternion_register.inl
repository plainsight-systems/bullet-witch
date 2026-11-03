#ifndef BW_PLATFORM_MATH_QUATERNION_REGISTER_INL
#define BW_PLATFORM_MATH_QUATERNION_REGISTER_INL

// Platform owns compile-time silicon selection, as for the accepted registers.
#if defined(__aarch64__)
#include "../../../../../cpu/arm64/math/simd/quaternion_register.inl"
#else
#error "Quaternion register implementation requires an inspected target backend."
#endif
#endif // BW_PLATFORM_MATH_QUATERNION_REGISTER_INL
