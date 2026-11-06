#ifndef BW_PLATFORM_REGISTER_INL
#define BW_PLATFORM_REGISTER_INL
// The tail of bw/math/simd/register.hpp. Opened alone it includes that header,
// which leaves this file out while the guard above is defined.
#include "bw/math/simd/register.hpp"

// Select silicon definitions at compile time; shared callers never branch on ISA.
#if defined(__aarch64__)
#ifndef BW_PLATFORM_ARM64_REGISTER_INL
#include "../../../../../cpu/arm64/math/simd/register.inl"
#endif
#ifndef BW_PLATFORM_ARM64_REGISTER_COMPOSITION_INL
#include "../../../../../cpu/arm64/math/simd/register_composition.inl"
#endif
#else
#error "Register implementation requires an inspected target backend."
#endif
#endif // BW_PLATFORM_REGISTER_INL
