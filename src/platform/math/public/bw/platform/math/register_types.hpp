#ifndef BW_PLATFORM_REGISTER_TYPES_HPP
#define BW_PLATFORM_REGISTER_TYPES_HPP

// Unreal f45729c0b6f097038e72b52d366a62baeca3275b, VectorRegister.h:11–19 selects
// the native register backend at compile time. ARM64 is silicon, not an OS.
#if defined(__aarch64__)
#include "../../../../../cpu/arm64/math/simd/register_types.hpp"
namespace bw::platform {
using Register4f = arm64::Register4f;
using Register4d = arm64::Register4d;
} // namespace bw::platform
#else
#error "Register backend must be selected from inspected target source."
#endif
#endif // BW_PLATFORM_REGISTER_TYPES_HPP
