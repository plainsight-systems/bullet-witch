#include "engine/runtime/engine_loop.hpp"
#include "platform/macos/platform_time.hpp"
#include <charconv>
#include <iostream>
#include <string_view>

// E.6/MEM.9: platform, engine and loop lifetimes are lexical; startup parsing and
// reporting precede frame dispatch. Executable global operators route C++ storage
// through the process allocator; no world is introduced.
// Unreal reference: f45729c0b6f097038e72b52d366a62baeca3275b.
namespace {
// Future engine work seam: Engine/Source/Runtime/Launch/Private/
// LaunchEngineLoop.cpp:5858-5861; Engine/Source/Runtime/Engine/Private/
// GameEngine.cpp:1768 onward. This timing-only engine intentionally has no world,
// assets, window, rendering, input or simulation. These empty methods say exactly
// what is absent, rather than standing in for implemented gameplay systems.
class HeadlessEngine final : public bw::engine::Engine {
public:
  using Engine::Engine;
  // LaunchEngineLoop.cpp:4826-4830: initialization seam, no modules/world yet.
  void init(bw::engine::EngineLoop&) override {}
  // LaunchEngineLoop.cpp:4885-4890: startup seam, no world to start yet.
  void start() override {}
  // LaunchEngineLoop.cpp:5858-5861: engine tick seam, no world work in this slice.
  void tick(float, bool) override {}
  // LaunchEngineLoop.cpp:5055-5065: cleanup seam, no world resources yet.
  void pre_exit() override {}
};
} // namespace

// Host entry point. References: Engine/Source/Runtime/Core/Private/Misc/
// CoreGlobals.cpp:399-400; Engine/Source/Runtime/Core/Private/Mac/CocoaThread.cpp:
// 358-369; Engine/Source/Runtime/Launch/Private/Launch.cpp:123-194.
int main(int argc, char** argv) {
  // Establish native timing before engine startup, then the reference game-thread
  // scheduling operation. Actual API results are reported, never presumed.
  bw::platform::macos::PlatformTime time;
  bw::platform::macos::PlatformProcess process;
  const auto scheduling = bw::platform::macos::initialize_game_thread();
  std::cout << "scheduler read=" << scheduling.read_error << " set=" << scheduling.set_error
            << " verify=" << scheduling.verify_error << " policy=" << scheduling.policy
            << " observed_priority=" << scheduling.observed_priority << '\n';

  // Headless CLI spelling only: --seconds maps to the source SECONDS setting,
  // LaunchEngineLoop.cpp:4707-4740. Omission retains source0 (no time limit).
  // No cap override is supplied: Engine retains source t.MaxFPS default0.
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

  // Launch.cpp:123-194: native services outlive engine, which outlives the loop;
  // run the source PreInit/Init/frame-loop/Exit lifecycle, with explicit time limit.
  HeadlessEngine engine(time, process);
  bw::engine::EngineLoop loop(engine, seconds);
  const int result = bw::engine::run_engine_loop(loop);
  std::cout << "frames=" << loop.frame_counter() << " maximum_fps=" << engine.max_fps() << '\n';
  return result;
}

// Platform scope: Engine/Source/Runtime/Launch/Private/Mac/LaunchMac.cpp:250-294,454;
// Engine/Source/Runtime/Core/Private/Mac/CocoaThread.cpp:497-520. This headless
// timing host uses its current thread; Cocoa main/game-thread and window/event
// integration remain a later slice. No Cocoa host equivalence is claimed.
