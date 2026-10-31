#include "fixture.hpp"

#include "error_history.hpp"

#include <cstdio>
#include <stdexcept>
#include <string_view>

namespace {
void require(bool condition, const char* message) {
  if (!condition) {
    throw std::runtime_error{message};
  }
}
} // namespace

int main() {
  namespace fixture = bw::tests::mutex_startup;
  namespace history = bw::diagnostics::detail;
  try {
    require(fixture::startup_finished, "separate translation unit constructor ran before main");
    require(fixture::observed_unlocked && fixture::acquired_before_main,
            "real production mutex was initially unlocked and usable before main");
    require(fixture::probe_mutex.is_locked(), "startup lock state survived until main");
    const bool acquired_again = fixture::probe_mutex.try_lock();
    if (acquired_again) {
      fixture::probe_mutex.unlock();
    }
    require(!acquired_again, "retained startup ownership rejects another acquisition");
    fixture::probe_mutex.unlock();
    require(!fixture::probe_mutex.is_locked(), "normal unlock works after startup use");
    const bool acquired_after_release = fixture::probe_mutex.try_lock();
    if (acquired_after_release) {
      fixture::probe_mutex.unlock();
    }
    require(acquired_after_release, "normal acquisition works after startup release");

    const bool history_released = history::error_history_mutex.try_lock();
    if (history_released) {
      history::error_history_mutex.unlock();
    }
    require(history_released, "real history writer released its mutex before main");
    require(std::u16string_view{history::error_history.data()} ==
                u"history from another translation unit before main",
            "real history contents survive startup without a later reset");
    {
      history::ErrorHistoryWriter writer;
      writer.append(u"history from main");
    }
    require(std::u16string_view{history::error_history.data()} == u"history from main",
            "ordinary history transaction follows startup transaction");
    std::puts("constant-initialized mutex startup contracts passed");
    return 0;
  } catch (const std::exception& failure) {
    std::fprintf(stderr, "%s\n", failure.what());
    return 1;
  }
}
