#ifndef BW_ENGINE_RUNTIME_ENGINE_LOOP_HPP
#define BW_ENGINE_RUNTIME_ENGINE_LOOP_HPP
#include "engine/runtime/engine.hpp"
#include <cstdint>
#include <string_view>
// I.1: explicit engine ownership and lifecycle. R.1: outer launch cleanup is
// scope-bound. MEM.9: fixed loop accounting; no per-frame dynamic allocation.
namespace bw::engine {
// Unreal f45729c0b6f097038e72b52d366a62baeca3275b:
// Engine/Source/Runtime/Launch/Private/LaunchEngineLoop.cpp:4698-4740,
// 4747-4998,5575-6140. EngineLoop drives one engine tick per host frame.
// Caller owns Engine/platform lifetime. Use one startup/run/exit lifecycle.
class EngineLoop final {
public:
  explicit EngineLoop(Engine& engine, double maximum_tick_time = 0.0);
  // Lifecycle: LaunchEngineLoop.cpp:1790,
  // 2592-2593 sets up this game owner's cache, starts named dispatch/attachment.
  // Module/config/input work remains absent; native time/thread precedes PreInit.
  [[nodiscard]] int pre_init();
  // Same pin, Engine/Source/Runtime/Launch/Private/LaunchEngineLoop.cpp:4818-4998.
  // Initialize application time -> engine.init(*this) -> engine.start -> running.
  [[nodiscard]] int init();
  // Same pin, Engine/Source/Runtime/Launch/Private/LaunchEngineLoop.cpp:
  // 5695-5710,5858-5861,6073-6076,6132-6135. Check seconds limit, update timing,
  // tick engine(delta,false), account elapsed after frame5, increment frame once.
  // The source seconds-limit request still permits that current frame to finish.
  void tick();
  // Same pin, Engine/Source/Runtime/Launch/Private/LaunchEngineLoop.cpp:5010-5065.
  // Clear running and call the created engine's PreExit; host requests exit first.
  // Source:5215 shuts dispatch down after dependent owners stop.
  // Game cache/allocator remain process-lived; Exit does not clear or destroy them.
  void exit();
  // Same pin, Engine/Source/Runtime/Core/Private/Misc/CoreGlobals.cpp:325-341.
  // Request exit on this game thread; finish the current tick before leaving
  // the outer loop. No cross-thread cancellation or resumable-run API.
  void request_exit(std::string_view reason);
  // Same pin, Engine/Source/Runtime/Core/Public/CoreGlobals.h:395-400.
  [[nodiscard]] bool exit_requested() const;
  // Same pin, Engine/Source/Runtime/Launch/Private/LaunchEngineLoop.cpp:4930-4934,
  // 5010-5015: running spans initialized engine activity until Exit.
  [[nodiscard]] bool is_running() const;
  // Same pin, Engine/Source/Runtime/Core/Private/Misc/CoreGlobals.cpp:408-410;
  // Engine/Source/Runtime/Launch/Private/LaunchEngineLoop.cpp:6132-6135.
  [[nodiscard]] std::uint64_t frame_counter() const;

private:
  Engine& engine_;
  double maximum_tick_time_{};
  double total_tick_time_{};
  std::uint64_t frame_counter_{};
  bool exit_requested_{};
  bool running_{};
  bool initialized_{};
};
// Same pin, Engine/Source/Runtime/Launch/Private/Launch.cpp:123-194:
// scope-exit cleanup; PreInit -> check result/exit -> Init -> while !exit Tick.
[[nodiscard]] int run_engine_loop(EngineLoop& loop);
} // namespace bw::engine
#endif
