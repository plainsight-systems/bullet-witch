#include "core/memory/allocator.hpp"
#include "core/memory/binned_thread_cache.hpp"
#include "core/tasks/named_thread_dispatch.hpp"
#include "engine/runtime/engine_loop.hpp"
#include "platform/macos/platform_time.hpp"

#include <cstdio>
#include <cstdlib>

namespace {
void require(bool condition, const char* message) {
  if (!condition) {
    std::fprintf(stderr, "%s\n", message);
    std::exit(1);
  }
}
class ObservingEngine final : public bw::engine::Engine {
public:
  ObservingEngine(bw::platform::PlatformTime& time, bw::platform::PlatformProcess& process)
      : Engine(time, process) {}
  void init(bw::engine::EngineLoop&) override {
    require(bw::memory::BinnedThreadCache::current_if_owned() != nullptr,
            "PreInit owns game cache before Engine Init");
    require(bw::tasks::NamedThreadDispatch::instance().running(),
            "PreInit starts named dispatch before Engine Init");
  }
  void start() override {}
  void tick(float, bool) override {}
  void pre_exit() override {
    require(bw::tasks::NamedThreadDispatch::instance().running(),
            "dependent engine PreExit happens before dispatch shutdown");
  }
};
} // namespace

int main() {
  // These source forwarding calls are safe before first allocation; unlike setup
  // they must not create a cache. No fake allocator or heap injection is used.
  bw::memory::clear_current_thread_cache();
  bw::memory::mark_current_thread_cache_used();
  bw::memory::mark_current_thread_cache_unused();
  require(bw::memory::BinnedThreadCache::current_if_owned() == nullptr,
          "inactive forwarding must not create a TLS cache");
  bw::memory::setup_current_thread_cache();
  auto* cache = bw::memory::BinnedThreadCache::current_if_owned();
  require(cache != nullptr, "facade setup bootstraps and owns native cache");
  bw::memory::mark_current_thread_cache_unused();
  require(bw::memory::BinnedThreadCache::current_if_owned() == nullptr,
          "unused relinquishes cache ownership");
  bw::memory::mark_current_thread_cache_used();
  require(bw::memory::BinnedThreadCache::current_if_owned() == cache,
          "used reacquires the same cache");
  bw::memory::clear_current_thread_cache();
  require(bw::memory::BinnedThreadCache::current_if_owned() == nullptr,
          "explicit thread teardown clears cache");

  bw::platform::macos::PlatformTime time;
  bw::platform::macos::PlatformProcess process;
  ObservingEngine engine(time, process);
  bw::engine::EngineLoop loop(engine);
  require(loop.pre_init() == 0, "native PreInit succeeds");
  require(loop.init() == 0, "native Init succeeds");
  cache = bw::memory::BinnedThreadCache::current_if_owned();
  // A queued callback verifies Tick did not invent a game-owner queue pump.
  bool processed = false;
  auto& dispatch = bw::tasks::NamedThreadDispatch::instance();
  dispatch.enqueue(
      bw::tasks::NamedThread::game,
      {[](void* context, bw::tasks::NamedThread) noexcept { *static_cast<bool*>(context) = true; },
       &processed});
  loop.tick();
  require(!processed, "frame loop must not add an untraced task queue pump");
  require(bw::memory::BinnedThreadCache::current_if_owned() == cache,
          "frame pacing does not churn game cache");
  loop.exit();
  require(!dispatch.running(), "Exit shuts down accepted named dispatch");
  require(processed, "accepted dispatch shutdown drains its game-owner queue");
  require(bw::memory::BinnedThreadCache::current_if_owned() == cache,
          "game cache remains process-lived after engine exit");
  void* late = bw::memory::allocate(27);
  require(bw::memory::allocation_size(late) >= 27,
          "allocator remains available for late process teardown");
  bw::memory::release(late);
  std::puts("allocator facade and game-owner lifecycle pass");
}
