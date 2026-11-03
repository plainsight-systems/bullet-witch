#include "bw/platform/tasks/task_dispatch.hpp"
#include "bw/platform/os/macos/platform_time.hpp"

#include <cstdio>

namespace bw::platform {
// Diagnostic, then a 1 ms platform sleep.
// stderr is the current headless diagnostic adapter; no new logger is invented.
void pause_after_task_tag_overflow() {
  std::fputs("Task queue ABA counter wrapped\n", stderr);
  macos::PlatformProcess{}.sleep(0.001F);
}
} // namespace bw::platform
