#include "bw/memory/memory_failure.hpp"
#include "bw/platform/memory/memory_failure.hpp"
#include <cerrno>

// E.26: unrecoverable native failure follows fatal exit, not invented recovery.
// E.28: errno is captured before diagnostic calls can overwrite it.
// MEM.7: platform failure handling stays behind shared declarations.
namespace bw::memory {
[[noreturn]] void out_of_memory(std::uint64_t size, std::uint32_t alignment) {
  // Record the fatal request before gathering diagnostics. No
  // logger is installed in the headless host. The const record is confined to
  // the fatal thread; it is not published as a racing global state accessor.
  const auto handle_failure = [&] {
    const OutOfMemoryRecord record{size, alignment, errno};
    // Gather stats, report and terminate. Mac holds no reserve pool, so nothing
    // is freed before reporting. No engine logger,
    // allocator dump or OOM delegate listeners have been installed in this host.
    platform::report_out_of_memory(record);
  };
  // Run once through a function-local static initializer. Concurrent callers
  // await initialization; no custom lock/atomic election or recursive-entry
  // recovery is introduced.
  [[maybe_unused]] static const int handled = (handle_failure(), 1);
  // Fallback if fatal reporting unexpectedly returns. The selected target
  // primitive implements the infinite sleep; shared code contains no OS API.
  platform::wait_after_memory_failure();
}
} // namespace bw::memory
