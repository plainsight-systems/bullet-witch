#ifndef BW_PLATFORM_REGISTER_INL
#define BW_PLATFORM_REGISTER_INL

// Unreal f45729c0b6f097038e72b52d366a62baeca3275b, VectorRegister.h:11–19.
// Select silicon definitions at compile time; shared callers never branch on ISA.
#if defined(__aarch64__)
#include "platform/arm64/register.inl"
#include "platform/arm64/register_composition.inl"
#else
#error "Register implementation requires an inspected target backend."
#endif
#endif // BW_PLATFORM_REGISTER_INL
