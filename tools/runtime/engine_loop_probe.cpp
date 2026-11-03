#include "bw/platform/os/macos/platform_time.hpp"
#include "bw/runtime/engine_loop.hpp"
#include <algorithm>
#include <array>
#include <charconv>
#include <cstddef>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <string_view>

// MEM.9: bounded sample storage is allocated before dispatch; no vector growth or
// logging on every frame. This observation tool does not implement a second loop.
namespace {
// Observation of run_engine_loop and the engine's timing update: retain completed
// frame time, elapsed/idle/overshoot, and request ordinary exit after121samples.
class ProbeEngine final : public bw::engine::Engine {
public:
  using Engine::Engine;
  std::array<bw::engine::ApplicationTime, 121> samples{};
  std::size_t sample_count{};

  // Retain host solely for normal exit request.
  void init(bw::engine::EngineLoop& loop) override { loop_ = &loop; }
  // No world startup in this observation tool.
  void start() override {}
  // Observe the published frame, never pace it.
  void tick(float, bool) override {
    samples[sample_count++] = application_time();
    if (sample_count == samples.size()) {
      // Finish this frame, leave the outer loop normally.
      loop_->request_exit("Native timing sample complete");
    }
  }
  // Fixed observation storage has no world cleanup.
  void pre_exit() override {}

private:
  bw::engine::EngineLoop* loop_{};
};
} // namespace

// Native timing observation.
int main(int argc, char** argv) {
  // Tool input, not a runtime default: require an explicit cap for every probe.
  // It feeds the maximum FPS setting through Engine::set_max_fps; no FPS floor exists.
  if (argc != 3 || std::string_view(argv[1]) != "--max-fps") {
    std::cerr << "Usage: engine_loop_probe --max-fps VALUE\n";
    return 2;
  }
  float requested_cap{};
  const std::string_view value(argv[2]);
  const auto parsed = std::from_chars(value.data(), value.data() + value.size(), requested_cap);
  if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size()) {
    std::cerr << "Invalid maximum FPS value\n";
    return 2;
  }

  // Native initialization then normal lifecycle, as in the host. Observe
  // thread-policy return codes rather than silently substituting another policy.
  bw::platform::macos::PlatformTime time;
  bw::platform::macos::PlatformProcess process;
  const auto scheduling = bw::platform::macos::initialize_game_thread();
  ProbeEngine engine(time, process);
  engine.set_max_fps(requested_cap);
  bw::engine::EngineLoop loop(engine);

  // Observation only: measure process CPU and wall time surrounding the same
  // run_engine_loop. These measurements do not alter the pacing algorithm.
  const std::clock_t cpu_start = std::clock();
  const double wall_start = time.seconds();
  const int result = bw::engine::run_engine_loop(loop);
  const double wall_seconds = time.seconds() - wall_start;
  const double cpu_seconds = static_cast<double>(std::clock() - cpu_start) / CLOCKS_PER_SEC;

  // Observation only:120 inter-frame intervals exclude the first timestamp's
  // initialization offset. Report actual cadence, range and maximum overshoot.
  double minimum_interval = engine.samples[1].current_time - engine.samples[0].current_time;
  double maximum_interval = minimum_interval;
  double maximum_overshoot = 0.0;
  for (std::size_t i = 1; i < engine.sample_count; ++i) {
    const double interval = engine.samples[i].current_time - engine.samples[i - 1].current_time;
    minimum_interval = std::min(minimum_interval, interval);
    maximum_interval = std::max(maximum_interval, interval);
    maximum_overshoot = std::max(maximum_overshoot, engine.samples[i].idle_time_overshoot);
  }
  const double elapsed = engine.samples.back().current_time - engine.samples.front().current_time;
  const double intervals = static_cast<double>(engine.samples.size() - 1);
  const bool priority_applied = scheduling.read_error == 0 && scheduling.set_error == 0 &&
                                scheduling.verify_error == 0 && scheduling.observed_priority == 45;
  std::cout << std::fixed << std::setprecision(6) << "maximum_fps_test_input=" << requested_cap
            << " intervals=" << intervals << '\n'
            << "observed_fps=" << intervals / elapsed << " min_ms=" << minimum_interval * 1000.0
            << " mean_ms=" << elapsed / intervals * 1000.0
            << " max_ms=" << maximum_interval * 1000.0
            << " max_overshoot_ms=" << maximum_overshoot * 1000.0 << '\n'
            << "wall_seconds=" << wall_seconds << " process_cpu_seconds=" << cpu_seconds
            << " cpu_percent_one_core=" << cpu_seconds / wall_seconds * 100.0 << '\n'
            << "scheduler_read=" << scheduling.read_error
            << " scheduler_set=" << scheduling.set_error
            << " scheduler_verify=" << scheduling.verify_error << " policy=" << scheduling.policy
            << " observed_priority=" << scheduling.observed_priority
            << " requested_priority_applied=" << priority_applied << '\n';
  return result;
}
