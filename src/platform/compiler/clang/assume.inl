#ifndef BW_PLATFORM_COMPILER_CLANG_ASSUME_INL
#define BW_PLATFORM_COMPILER_CLANG_ASSUME_INL

// Unreal f45729c0b6f097038e72b52d366a62baeca3275b, CoreMiscDefines.h:125.
// Ordinary compiler: unevaluated bool conversion, not a runtime argument or
// compiler-unreachable assumption. C++ casts spell the same source conversions.
// Read GEN.6: preserve zero runtime work without introducing an inline wrapper.
#define BW_ANALYSIS_ASSUME(expression) static_cast<void>(sizeof(static_cast<bool>(expression)))

// ClangPlatformCodeAnalysis.h:43–50,83 maps analyzer-only false paths to a declared
// analyzer_noreturn marker. That tooling mode remains future support, not a fake
// runtime marker, reserved compiler flag simulation or callback injection.

#endif // BW_PLATFORM_COMPILER_CLANG_ASSUME_INL
