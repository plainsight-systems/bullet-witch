#include "platform/task_dispatch.hpp"
#include "platform/macos/platform_time.hpp"

#include <cstdio>

namespace bw::platform {
// LockFreeTagCounterHasOverflowed, LockFreeList.cpp:42-46 at pinned
// f45729c0b6f097038e72b52d366a62baeca3275b: diagnostic then1ms platform sleep.
// stderr is the current headless diagnostic adapter; no new logger is invented.
void pause_after_task_tag_overflow() {
  std::fputs("Task queue ABA counter wrapped\n", stderr);
  macos::PlatformProcess{}.sleep(0.001F);
}
} // namespace bw::platform
