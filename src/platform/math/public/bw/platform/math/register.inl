#ifndef BW_PLATFORM_REGISTER_INL
#define BW_PLATFORM_REGISTER_INL

// Select silicon definitions at compile time; shared callers never branch on ISA.
#if defined(__aarch64__)
#include "../../../../../cpu/arm64/math/simd/register.inl"
#include "../../../../../cpu/arm64/math/simd/register_composition.inl"
#else
#error "Register implementation requires an inspected target backend."
#endif
#endif // BW_PLATFORM_REGISTER_INL
