#include "bw/platform/memory/memory_failure.hpp"

#include <array>
#include <cinttypes>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mach/mach.h>
#include <os/log.h>
#include <sys/sysctl.h>
#include <unistd.h>

// E.26/E.28: record native failures without recovery or later errno ambiguity.
// MEM.7: native memory accounting stays in the target implementation.
// All references: Unreal f45729c0b6f097038e72b52d366a62baeca3275b,
// Engine/Source/Runtime/Core/Private unless a public path is named.
namespace bw::platform {
namespace {
// Own extraction of the fields reported by GenericPlatformMemory.cpp:277-290.
// This snapshot is only collected on the fatal winner, not shared mutable state.
struct NativeMemoryStats {
  std::uint64_t available_physical{};
  std::uint64_t available_virtual{};
  std::uint64_t used_physical{};
  std::uint64_t used_virtual{};
  // Diagnostic-only additions: -1 means the query has not run, not success.
  int host_result{-1};
  int swap_result{-1};
  int task_result{-1};
};

// Apple/ApplePlatformMisc.cpp LocalPrint:74-84 uses native os_log.
// Misc/OutputDeviceStdOut.cpp:33-36 writes/flushes the selected stdout device.
// Own bounded diagnostic extraction: no full structured-log/crash-UI subsystem.
void output_diagnostic(const char* message) {
#ifndef NDEBUG
  os_log(OS_LOG_DEFAULT, "[Bullet Witch] %{public}s", message);
#else
  os_log(OS_LOG_DEFAULT, "[Bullet Witch] %s", message);
#endif
#if BW_HEADLESS_DIAGNOSTICS
  // Explicit headless target selection; not a global default for future hosts.
  (void)std::fwrite(message, 1, std::strlen(message), stdout);
  (void)std::fflush(stdout);
#endif
}

// Mac/MacPlatformMemory.cpp GetStats:178-225: host free/inactive pages + swap,
// then process phys_footprint and virtual_size. No resident-size substitution.
NativeMemoryStats native_memory_stats() {
  NativeMemoryStats result;
  vm_statistics64_data_t host_stats{};
  mach_msg_type_number_t host_count = HOST_VM_INFO64_COUNT;
  result.host_result = host_statistics64(mach_host_self(), HOST_VM_INFO64,
                                         reinterpret_cast<host_info_t>(&host_stats), &host_count);
  if (result.host_result != KERN_SUCCESS) {
    // Source:187-190 warns and returns the available snapshot on query failure.
    output_diagnostic("MemoryStats: host_statistics64 failed\n");
    return result;
  }
  result.available_physical =
      (static_cast<std::uint64_t>(host_stats.free_count) + host_stats.inactive_count) *
      vm_page_size;
  xsw_usage swap{};
  std::size_t swap_size = sizeof(swap);
  result.swap_result = sysctlbyname("vm.swapusage", &swap, &swap_size, nullptr, 0);
  result.available_virtual = result.available_physical + swap.xsu_avail;

  // Source:201-222 queries process memory. There is no earlier stats sampler in
  // this leaf, so the reported observed peaks equal this sole fatal snapshot.
  task_vm_info_data_t task_stats{};
  mach_msg_type_number_t task_count = TASK_VM_INFO_COUNT;
  result.task_result = task_info(mach_task_self(), TASK_VM_INFO,
                                 reinterpret_cast<task_info_t>(&task_stats), &task_count);
  if (result.task_result != KERN_SUCCESS) {
    output_diagnostic("MemoryStats: task_info failed\n");
    return result;
  }
  result.used_physical = task_stats.phys_footprint;
  result.used_virtual = task_stats.virtual_size;
  return result;
}

// Mac/MacPlatformMisc.cpp RequestExit:725-735 selects immediate critical exit3.
// The current host is unguarded: MacErrorOutputDevice.cpp Serialize:69-74.
[[noreturn]] void exit_critical() { std::_Exit(3); }
} // namespace

// GenericPlatformMemory.cpp OnOutOfMemory:262-300 + selected native dependencies.
[[noreturn]] void report_out_of_memory(const memory::OutOfMemoryRecord& record) {
  // ApplePlatformMisc GetSystemErrorMessage:87-104 uses strerror_r. This fixed
  // buffer passes its actual bound; no TCHAR conversion subsystem is required.
  std::array<char, 256> system_error{};
  if (strerror_r(record.system_error, system_error.data(), system_error.size()) != 0) {
    system_error[0] = '\0';
  }
  const NativeMemoryStats stats = native_memory_stats();
  // GenericPlatformMemory:267-295 reserve/dump branches are absent because Mac
  // inherits GetBackMemoryPoolSize()==0. No fake reserve or allocator is created.
  std::array<char, 2048> message{};
  (void)std::snprintf(
      message.data(), message.size(),
      "MemoryStats: available_physical=%" PRIu64 " available_virtual=%" PRIu64
      " used_physical=%" PRIu64 " peak_used_physical=%" PRIu64 " used_virtual=%" PRIu64
      " peak_used_virtual=%" PRIu64 " host_result=%d swap_result=%d task_result=%d\n"
      "Out of memory: size=%" PRIu64 " alignment=%" PRIu32 " system_error=%d (%s)\n",
      stats.available_physical, stats.available_virtual, stats.used_physical, stats.used_physical,
      stats.used_virtual, stats.used_virtual, stats.host_result, stats.swap_result,
      stats.task_result, record.allocation_size, record.allocation_alignment, record.system_error,
      system_error.data());
  // GenericPlatformMemory:296-300: no listeners installed; emit fatal diagnosis.
  output_diagnostic(message.data());
  // MacErrorOutputDevice HandleError:77-107 has no installed logger/delegates/UI
  // in this headless host. Preserve its critical termination, not ordinary exit.
  exit_critical();
}

// AssertionMacros.cpp:175-192 → MacErrorOutputDevice.cpp:69-74. Keep assertion
// provenance distinct from allocation failure while selecting native fatal exit.
[[noreturn]] void memory_check_failed(const char* expression, const char* file, int line) {
  std::array<char, 2048> message{};
  (void)std::snprintf(message.data(), message.size(), "Memory assertion failed: %s [%s:%d]\n",
                      expression, file, line);
  output_diagnostic(message.data());
  exit_critical();
}

// GenericPlatform/GenericPlatformProcess.cpp SleepInfinite:427-434: stop this
// thread indefinitely. This fallback is outside successful allocation/game ticks.
[[noreturn]] void wait_after_memory_failure() {
  for (;;) {
    (void)pause();
  }
}
} // namespace bw::platform
