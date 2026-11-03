#ifndef BW_PLATFORM_MEMORY_FAILURE_HPP
#define BW_PLATFORM_MEMORY_FAILURE_HPP
#include "bw/memory/memory_failure.hpp"

namespace bw::platform {
// Native statistics/diagnostics and unguarded critical exit with code 3. The
// current headless host has no engine logger, registered delegates or interactive
// reporter.
[[noreturn]] void report_out_of_memory(const memory::OutOfMemoryRecord& record);

// Sleeps forever, in the selected BSD backend.
// Other targets provide their own primitive; shared memory code has no OS calls.
[[noreturn]] void wait_after_memory_failure();

// A failed check follows the fatal assertion route, not the out-of-memory route.
// The expression/location are reported distinctly from allocation failures.
[[noreturn]] void memory_check_failed(const char* expression, const char* file, int line);
} // namespace bw::platform
#endif
