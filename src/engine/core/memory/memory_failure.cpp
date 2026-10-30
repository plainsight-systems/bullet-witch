#include "bw/memory/memory_failure.hpp"
#include "bw/platform/memory/memory_failure.hpp"
#include <cerrno>

// E.26: unrecoverable native failure follows fatal exit, not invented recovery.
// E.28: errno is captured before diagnostic calls can overwrite it.
// MEM.7: platform failure handling stays behind shared declarations.
namespace bw::memory {
// Unreal f45729c0b6f097038e72b52d366a62baeca3275b:
// Core/Private/GenericPlatform/GenericPlatformMemory.cpp:247-305, OnOutOfMemory.
[[noreturn]] void out_of_memory(std::uint64_t size, std::uint32_t alignment) {
  // Source:249-264 records the fatal request before gathering diagnostics. No
  // logger is installed in the headless host. The const record is confined to
  // the fatal thread; it is not published as a racing global state accessor.
  const auto handle_failure = [&] {
    const OutOfMemoryRecord record{size, alignment, errno};
    // Source:266-300 gathers stats, reports and terminates. Mac's inherited
    // GetBackMemoryPoolSize is zero; no reserve exists to free. No engine logger,
    // allocator dump or OOM delegate listeners have been installed in this host.
    platform::report_out_of_memory(record);
  };
  // UE_CALL_ONCE (Core/Public/Misc/CoreMiscDefines.h:180-181) is exactly a
  // function-local static initializer. Concurrent callers await initialization;
  // no custom lock/atomic election or recursive-entry recovery is introduced.
  [[maybe_unused]] static const int handled = (handle_failure(), 1);
  // Source:304 fallback if fatal reporting unexpectedly returns. The selected
  // target primitive implements SleepInfinite; shared code contains no OS API.
  platform::wait_after_memory_failure();
}
} // namespace bw::memory
