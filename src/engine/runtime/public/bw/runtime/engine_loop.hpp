#ifndef BW_ENGINE_RUNTIME_ENGINE_LOOP_HPP
#define BW_ENGINE_RUNTIME_ENGINE_LOOP_HPP
#include "bw/runtime/engine.hpp"
#include <cstdint>
#include <string_view>
// I.1: explicit engine ownership and lifecycle. R.1: outer launch cleanup is
// scope-bound. MEM.9: fixed loop accounting; no per-frame dynamic allocation.
namespace bw::engine {
// EngineLoop drives one engine tick per host frame.
// Caller owns Engine/platform lifetime. Use one startup/run/exit lifecycle.
class EngineLoop final {
public:
  explicit EngineLoop(Engine& engine, double maximum_tick_time = 0.0);
  // Sets up this game owner's cache, starts named dispatch/attachment.
  // Module/config/input work remains absent; native time/thread precedes pre_init.
  [[nodiscard]] int pre_init();
  // Initialize application time -> engine.init(*this) -> engine.start -> running.
  [[nodiscard]] int init();
  // Check seconds limit, update timing, tick engine(delta,false), account elapsed
  // after frame5, increment frame once.
  // The seconds-limit request still permits that current frame to finish.
  void tick();
  // Clear running and call the created engine's pre_exit; host requests exit first.
  // Shuts dispatch down after dependent owners stop.
  // Game cache/allocator remain process-lived; exit does not clear or destroy them.
  void exit();
  // Request exit on this game thread; finish the current tick before leaving
  // the outer loop. No cross-thread cancellation or resumable-run API.
  void request_exit(std::string_view reason);
  [[nodiscard]] bool exit_requested() const;
  // Running spans initialized engine activity until exit.
  [[nodiscard]] bool is_running() const;
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
// Scope-exit cleanup; pre_init -> check result/exit -> init -> while !exit tick.
[[nodiscard]] int run_engine_loop(EngineLoop& loop);
} // namespace bw::engine
#endif
