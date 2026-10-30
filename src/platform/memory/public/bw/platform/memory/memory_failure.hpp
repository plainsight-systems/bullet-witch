#ifndef BW_PLATFORM_MEMORY_FAILURE_HPP
#define BW_PLATFORM_MEMORY_FAILURE_HPP
#include "bw/memory/memory_failure.hpp"

namespace bw::platform {
// Unreal f45729c0b6f097038e72b52d366a62baeca3275b:
// MacPlatformMemory.cpp:178-225, ApplePlatformMisc.cpp:74-104,
// MacErrorOutputDevice.cpp:69-107, MacPlatformMisc.cpp:725-735.
// Native statistics/diagnostics and unguarded critical exit3. Current headless
// extraction has no engine logger, registered delegates or interactive reporter.
[[noreturn]] void report_out_of_memory(const memory::OutOfMemoryRecord& record);

// GenericPlatformProcess::SleepInfinite:427-434 in the selected BSD backend.
// Other targets provide their own primitive; shared memory code has no OS calls.
[[noreturn]] void wait_after_memory_failure();

// Source check() follows the fatal assertion route, not OnOutOfMemory. The
// source expression/location are reported distinctly from allocation failures.
[[noreturn]] void memory_check_failed(const char* expression, const char* file, int line);
} // namespace bw::platform
#endif
