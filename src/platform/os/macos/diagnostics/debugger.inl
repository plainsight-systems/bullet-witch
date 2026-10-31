#ifndef BW_PLATFORM_OS_MACOS_DIAGNOSTICS_DEBUGGER_INL
#define BW_PLATFORM_OS_MACOS_DIAGNOSTICS_DEBUGGER_INL

// Unreal f45729c0b6f097038e72b52d366a62baeca3275b, ApplePlatform.h:32.
// Raw caller-site breakpoint; do not condition on debugger state, replace with
// abort or mark noreturn. A debugger can resume at the following instruction.
// This Apple platform selection does not imply ARM64 only runs macOS.
#define BW_PLATFORM_BREAK() __builtin_debugtrap()

#endif // BW_PLATFORM_OS_MACOS_DIAGNOSTICS_DEBUGGER_INL
