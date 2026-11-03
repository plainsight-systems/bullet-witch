#ifndef BW_PLATFORM_TASK_DISPATCH_HPP
#define BW_PLATFORM_TASK_DISPATCH_HPP

namespace bw::platform {
// Log the extremely rare ABA counter wrap and sleep 1 ms as a precaution.
void pause_after_task_tag_overflow();
} // namespace bw::platform
#endif
