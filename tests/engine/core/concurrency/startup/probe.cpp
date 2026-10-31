#include "fixture.hpp"

#include "error_history.hpp"

namespace bw::tests::mutex_startup {
namespace {
struct StartupProbe {
  StartupProbe() {
    observed_unlocked = !probe_mutex.is_locked();
    acquired_before_main = probe_mutex.try_lock();
    // Retain ownership until main; no later constructor may reset the lock word.
    {
      diagnostics::detail::ErrorHistoryWriter writer;
      writer.append(u"history from another translation unit before main");
    }
    startup_finished = true;
  }
};
const StartupProbe startup_probe;
} // namespace
} // namespace bw::tests::mutex_startup
