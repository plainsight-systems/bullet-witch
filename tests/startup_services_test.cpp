// V1: in-process startup-session contracts. Controlled media checks
// registration, activation side effects, lifecycle wait completion, retry and
// failure; these tests do not claim native playback, rendering or original-root
// execution. Fixture media outlives each session. Algorithm: observe public
// progress while advancing explicit frames, then verify Wait and unsupported
// boundaries. Bounded fixture work/storage. Design/implementation MCP:
// cpp-guidelines I.1
// (#ri-explicit) uses explicit media observations; cpp-performance MEM.9
// (guidelines/memory/MEM.9-embedded-allocate-at-init.md) keeps control state
// inline and bounds frame iterations, without claiming the scheduler allocates
// nothing.
#include "game/startup_services.hpp"

#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using bw::game::FlowStatus;
using bw::game::Screen;
void require(bool condition, const char* message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}
template <class F>
void rejects(F operation, const char* message) {
  bool rejected = false;
  try {
    operation();
  } catch (const std::exception&) {
    rejected = true;
  }
  require(rejected, message);
}
class Media final : public bw::game::LogoMedia {
 public:
  bool open_movie(std::string_view path) override {
    paths.emplace_back(path);
    return opens;
  }
  bool movie_complete() const override { return ended; }
  void stop_movie() noexcept override { ++stops; }
  void begin_fade(bw::game::FadeDirection, float) override {}
  bool fade_pending() const override { return fading; }
  bool opens{true}, ended{}, fading{};
  unsigned stops{};
  std::vector<std::string> paths;
};
void session_lifecycle() {
  Media media;
  bw::game::StartupSession session(media);
  require(!session.window_close_enabled(), "Close inhibited before activation");
  rejects([&] { (void)session.pending(Screen::company_logo); },
          "Wait without Exec must fail");
  session.execute(Screen::company_logo);
  require(session.pending(Screen::company_logo),
          "Newly requested task is pending before activation");
  require(!session.window_close_enabled(), "Exec does not activate inline");
  rejects([&] { session.execute(Screen::company_logo); },
          "Duplicate Exec must fail");
  (void)session.advance({});
  require(session.window_close_enabled(), "Activation permits window close");
  (void)session.advance({});
  require(media.paths.size() == 1 &&
              media.paths.front() == "data/movie/marvelousLogo.ssu",
          "Recovered first logo path");
  for (int i = 0; i < 3; ++i) {
    (void)session.advance({});
  }
  require(session.pending(Screen::company_logo),
          "Movie completion is a real gate");
  media.ended = true;
  for (int i = 0; i < 20 && session.progress(Screen::company_logo).status !=
                                FlowStatus::complete;
       ++i) {
    (void)session.advance({});
  }
  require(
      session.progress(Screen::company_logo).status == FlowStatus::complete &&
          session.status(Screen::company_logo) == 1 && media.stops == 2,
      "Both logos completed and released playback");
  require(media.paths.size() == 2 &&
              media.paths.back() == "data/movie/xeedLogo.ssu",
          "Recovered second logo path");
  require(session.pending(Screen::company_logo),
          "Result alone does not bypass pending lifecycle transition");
  (void)session.advance({});
  require(!session.pending(Screen::company_logo),
          "Wait observes idle and requests destruction");
  (void)session.advance({});
  session.execute(Screen::memory_card);
  for (int i = 0; i < 7; ++i) {
    (void)session.advance({});
  }
  require(
      session.progress(Screen::memory_card).status == FlowStatus::complete &&
          session.pending(Screen::memory_card),
      "Startup check completion also waits for idle lifecycle");
  (void)session.advance({});
  require(!session.pending(Screen::memory_card),
          "Startup check Wait completes");
  (void)session.advance({});
  session.execute(Screen::memory_card);
  require(session.pending(Screen::memory_card),
          "Re-execution must not reuse stale result");
}
void session_failures_and_globals() {
  Media media;
  bw::game::StartupSession session(media);
  require(session.global(1) == 0,
          "Cold startup initializes title return request");
  session.set_global(1, 1);
  require(session.global(1) == 1, "Slot one maps to title return request");
  session.set_global(1, 0);
  require(session.global(1) == 0, "Root can consume title return request");
  rejects(
      [&] { session.set_global(1, std::numeric_limits<double>::infinity()); },
      "Nonfinite session value rejected");
  rejects([&] { (void)session.global(0); },
          "Unimplemented stage owner rejected");
  rejects([&] { session.execute(Screen::advertise); },
          "Unimplemented screen must not auto-complete");
  rejects([&] { (void)session.latest_stage(); }, "No fabricated saved stage");
  rejects([&] { (void)session.advance({-1, 0}); }, "Invalid delta rejected");
  media.opens = false;
  session.execute(Screen::company_logo);
  (void)session.advance({});
  (void)session.advance({});
  require(session.progress(Screen::company_logo).status == FlowStatus::failed &&
              media.stops == 1,
          "Movie-open failure releases owned media and records failure");
  rejects([&] { (void)session.pending(Screen::company_logo); },
          "Failed movie cannot satisfy a successful wait");
  (void)session.advance({});
  rejects([&] { (void)session.pending(Screen::company_logo); },
          "Idle lifecycle cannot conceal flow failure");
}
}  // namespace
void startup_session_contracts() {
  session_lifecycle();
  session_failures_and_globals();
}
