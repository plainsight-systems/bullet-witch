#include "bw/platform/os/macos/platform_time.hpp"
#include "bw/runtime/engine_loop.hpp"
#include <charconv>
#include <iostream>
#include <string_view>

// E.6/MEM.9: platform, engine and loop lifetimes are lexical; startup parsing and
// reporting precede frame dispatch. Executable global operators route C++ storage
// through the process allocator; no world is introduced.
namespace {
// Future engine work seam. This timing-only engine intentionally has no world,
// assets, window, rendering, input or simulation. These empty methods say exactly
// what is absent, rather than standing in for implemented gameplay systems.
class HeadlessEngine final : public bw::engine::Engine {
public:
  using Engine::Engine;
  // Initialization seam, no modules/world yet.
  void init(bw::engine::EngineLoop&) override {}
  // Startup seam, no world to start yet.
  void start() override {}
  // Engine tick seam, no world work in this slice.
  void tick(float, bool) override {}
  // Cleanup seam, no world resources yet.
  void pre_exit() override {}
};
} // namespace

// Host entry point.
int main(int argc, char** argv) {
  // Establish native timing before engine startup, then the game-thread
  // scheduling operation. Actual API results are reported, never presumed.
  bw::platform::macos::PlatformTime time;
  bw::platform::macos::PlatformProcess process;
  const auto scheduling = bw::platform::macos::initialize_game_thread();
  std::cout << "scheduler read=" << scheduling.read_error << " set=" << scheduling.set_error
            << " verify=" << scheduling.verify_error << " policy=" << scheduling.policy
            << " observed_priority=" << scheduling.observed_priority << '\n';

  // Headless CLI spelling only: --seconds sets the loop's seconds limit.
  // Omitting it keeps 0 (no time limit).
  // No cap override is supplied: Engine keeps its maximum FPS default of 0.
  double seconds = 0.0;
  if (argc != 1) {
    if (argc != 3 || std::string_view(argv[1]) != "--seconds") {
      std::cerr << "Usage: engine_host [--seconds VALUE]\n";
      return 2;
    }
    const std::string_view value(argv[2]);
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), seconds);
    if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size()) {
      std::cerr << "Invalid seconds value\n";
      return 2;
    }
  }

  // Native services outlive engine, which outlives the loop; run the
  // pre_init/init/frame-loop/exit lifecycle, with explicit time limit.
  HeadlessEngine engine(time, process);
  bw::engine::EngineLoop loop(engine, seconds);
  const int result = bw::engine::run_engine_loop(loop);
  std::cout << "frames=" << loop.frame_counter() << " maximum_fps=" << engine.max_fps() << '\n';
  return result;
}

// Platform scope: this headless timing host uses its current thread; Cocoa
// main/game-thread and window/event integration remain a later slice. No Cocoa
// host equivalence is claimed.
