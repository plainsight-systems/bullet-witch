#ifndef BW_PLATFORM_COMPILER_ASSUME_HPP
#define BW_PLATFORM_COMPILER_ASSUME_HPP

// Read I.5/GEN.6: exact compiler semantics are part of the interface, not a new
// runtime precondition policy or an invitation to assume unreachable behavior.
// Unreal f45729c0b6f097038e72b52d366a62baeca3275b,
// CoreMiscDefines.h:107–126; ClangPlatformCodeAnalysis.h:11–15,43–50,83.
// BW_ANALYSIS_ASSUME(expression), ordinary Clang compilation: expression must be
// valid in bool context but is unevaluated through sizeof(bool(expression)). No
// side effects, runtime branch, compiler unreachable or added validation.
// Analyzer-specific nonreturn marker mapping is documented, not enabled here.
#if defined(__clang__)
#include "../../../../clang/assume.inl"
#else
#error Unsupported compiler assumption platform
#endif
#endif // BW_PLATFORM_COMPILER_ASSUME_HPP
