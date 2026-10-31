#ifndef BW_PLATFORM_DIAGNOSTICS_DEBUGGER_HPP
#define BW_PLATFORM_DIAGNOSTICS_DEBUGGER_HPP

// Read I.5/E.26/GEN.6: state native observation and failure boundaries honestly;
// do not substitute a new fatal policy. The trap belongs at its caller, not in
// an out-of-line numeric/runtime operation or a test callback.
namespace bw::platform {
// Unreal f45729c0b6f097038e72b52d366a62baeca3275b,
// ApplePlatformMisc.cpp:32–48, selected non-shipping GIgnoreDebugger=false.
// Query this process on every call; report P_TRACED from successful native sysctl.
// Owns no process handle, retains no state, and does not attach a debugger.
// Source reads uninitialized output if sysctl fails. Approved departure: failed
// native query returns false without reading its output. Runtime ignore policy
// belongs to later diagnostics configuration; it is not exposed as a fake test debugger override
// here.
[[nodiscard]] bool is_debugger_present();
} // namespace bw::platform

// BW_PLATFORM_BREAK() contract, ApplePlatform.h:32:
// unconditional caller-site __builtin_debugtrap; independent of debugger query.
// Not [[noreturn]]: a real debugger may resume execution. No flag/ship suppression
// belongs to this raw primitive. Target macro expands directly at the caller.
#if defined(__APPLE__)
#include "../../../../../os/macos/diagnostics/debugger.inl"
#else
#error Unsupported debugger platform
#endif
#endif // BW_PLATFORM_DIAGNOSTICS_DEBUGGER_HPP
