#ifndef BW_ENGINE_RUNTIME_ENGINE_HPP
#define BW_ENGINE_RUNTIME_ENGINE_HPP
#include "bw/platform/time/platform_time.hpp"
// I.1: engine owns application timing; readers get const state. Platform services are borrowed
// and outlive engine. MEM.9: timing holds fixed-size state only.
namespace bw::engine {
class EngineLoop;
// Unreal f45729c0b6f097038e72b52d366a62baeca3275b:
// Engine/Source/Runtime/Core/Public/Misc/AppTime.h:80-103.
// Defaults and units follow FAppTime. No world-clock or task-context claim yet.
struct ApplicationTime {
  double current_time{};
  double last_time{};
  double delta_time{1.0 / 30.0};
  double idle_time{};
  double idle_time_overshoot{};
  double game_time{};
};
// Wait requested after a frame that took real_delta seconds under a positive
// maximum frame rate; zero when uncapped or already over budget.
[[nodiscard]] float requested_frame_wait(float maximum_fps, float real_delta) noexcept;
class Engine {
public:
  Engine(platform::PlatformTime& time, platform::PlatformProcess& process);
  virtual ~Engine() = default;
  // Same pin, Engine/Source/Runtime/Launch/Private/LaunchEngineLoop.cpp:
  // 4818-4830 InitTime -> Engine::Init; 4885-4890 Start; 5065 PreExit.
  virtual void init(EngineLoop& loop) = 0;
  virtual void start() = 0;
  virtual void pre_exit() = 0;
  // Same pin, Engine/Source/Runtime/Launch/Private/LaunchEngineLoop.cpp:5858-5861. Second argument
  // is an idle-mode boolean, not an idle duration. World/render/input work is absent this slice.
  virtual void tick(float delta_seconds, bool idle_mode) = 0;
  // Same pin, Engine/Source/Runtime/Engine/Private/UnrealEngine.cpp:2980-3220.
  // Selected mode: ordinary runtime client, real time, no smoothing/fixed time,
  // no plugin handler. First previous-real reading is seconds()-0.0001.
  // wait=max(1/maxfps-real_elapsed,0); >5ms sleeps wait-2ms once; yield to
  // deadline; publish actual elapsed/idle/overshoot; rebase and add game time.
  void update_time_and_handle_max_tick_rate();
  // Same pin, Engine/Source/Runtime/Engine/Private/UnrealEngine.cpp:12136-12250. Positive setting
  // caps the selected unsmoothed mode; <=0 uncaps. The source default is zero, with no project
  // override.
  void set_max_fps(float maximum_fps);
  [[nodiscard]] float max_fps() const;
  // Same pin, Engine/Source/Runtime/Core/Public/Misc/AppTime.h:80-103.
  // Only this owner writes timing; callers observe a const view on the game thread.
  [[nodiscard]] const ApplicationTime& application_time() const;

private:
  friend class EngineLoop;
  // Same pin, Engine/Source/Runtime/Launch/Private/LaunchEngineLoop.cpp:4698-4705.
  // InitTime records current platform seconds; it does not synthesize a frame delta.
  void initialize_time();
  platform::PlatformTime& time_;
  platform::PlatformProcess& process_;
  ApplicationTime application_time_{};
  float maximum_fps_{};
  double last_real_time_{};
  bool real_time_initialized_{};
};
} // namespace bw::engine
#endif
