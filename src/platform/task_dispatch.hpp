#ifndef BW_PLATFORM_TASK_DISPATCH_HPP
#define BW_PLATFORM_TASK_DISPATCH_HPP

namespace bw::platform {
// Unreal f45729c0b6f097038e72b52d366a62baeca3275b LockFreeList.cpp:42-46:
// log the extremely rare ABA counter wrap and sleep1ms as source precaution.
void pause_after_task_tag_overflow();
} // namespace bw::platform
#endif
