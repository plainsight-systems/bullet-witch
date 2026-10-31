#include "bw/platform/diagnostics/debugger.hpp"

#include <sys/sysctl.h>
#include <unistd.h>

#include <array>
#include <cstddef>

// Read I.5/E.28/GEN.6: check the native result directly; no errno side channel,
// cached debugger state or callback. This syscall is not a successful-tick hook.
namespace bw::platform {
// Unreal f45729c0b6f097038e72b52d366a62baeca3275b,
// ApplePlatformMisc.cpp:32–48, FApplePlatformMisc::IsDebuggerPresent.
// Both current build configurations select non-shipping GIgnoreDebugger=false
// (CoreGlobals.cpp:478); later configuration owns any runtime override.
bool is_debugger_present() {
  // Source MIB observes only this process. No retained handle or debugger attach.
  std::array<int, 4> query{CTL_KERN, KERN_PROC, KERN_PROC_PID, getpid()};
  // The successful native call initializes output; failed output is never read.
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-member-init)
  kinfo_proc information;
  std::size_t size = sizeof(information);
  // Approved departure: the source ignores sysctl failure and can read
  // uninitialized output. A failed query returns false without reading output.
  if (sysctl(query.data(), static_cast<u_int>(query.size()), &information, &size, nullptr, 0) !=
      0) {
    return false;
  }
  // Source successful-query interpretation: the kernel's traced-process flag.
  return (information.kp_proc.p_flag & P_TRACED) != 0;
}
} // namespace bw::platform
