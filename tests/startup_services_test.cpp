// V1: in-process startup-session lifecycle and title readiness contracts.
// Controlled media/surface owners outlive each session. Inputs are explicit
// frame deltas, pressed actions and resource readiness; observe visibility,
// retained-resource retirement, status and Wait. No native media, file I/O,
// rendering or original-root execution. Algorithm: hold readiness despite
// elapsed time/input; complete the accepted route through scheduler idle and
// destruction; separately abandon an active title to check owner cleanup.
// Bounded fixture work/storage. Design/implementation MCP: cpp-guidelines I.1
// (#ri-explicit) makes media/readiness explicit; cpp-performance COPY.5
// (guidelines/copy-move/COPY.5-resource-owning-types-are-move-only.md) verifies
// exclusive operation retirement without asserting incidental cleanup counts.
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
    active = opens;
    return opens;
  }
  bool movie_complete() const override { return ended; }
  void stop_movie() noexcept override {
    active = false;
    ++stops;
  }
  void begin_fade(bw::game::FadeDirection, float) override {}
  bool fade_pending() const override { return fading; }
  bool opens{true}, ended{}, fading{}, active{};
  unsigned stops{};
  std::vector<std::string> paths;
};
class Surface final : public bw::game::TitleSurface {
 public:
  void begin() override {
    active = true;
    visible = false;
    begun = true;
  }
  bool ready() override {
    require(active, "Readiness requires retained title resources");
    return resources_ready;
  }
  void show(bool show_title,
            bw::game::TitlePresentation presentation) override {
    require(active, "Drawing requires retained title resources");
    visible = show_title;
    current = presentation;
  }
  void end() noexcept override {
    visible = false;
    active = false;
    current = {};
  }
  bw::game::TitlePresentation current;
  bool resources_ready{}, active{}, visible{}, begun{};
};
void title_readiness_and_lifecycle() {
  Media media;
  Surface surface;
  {
    bw::game::StartupSession session(media, &surface);
    session.execute(Screen::title);
    require(session.pending(Screen::title) && !surface.begun,
            "Title Exec registers without synchronous activation");
    media.ended = true;
    for (unsigned frame = 0; frame < 3; ++frame) {
      (void)session.advance({100, 0x1010});
    }
    require(surface.active && !surface.visible && media.active &&
                session.pending(Screen::title) &&
                session.progress(Screen::title).status == FlowStatus::pending &&
                session.status(Screen::title) == 0,
            "Missing readiness cannot fabricate completion from input, time or "
            "movie end");
    media.ended = false;
    surface.resources_ready = true;
    for (unsigned frame = 0; frame < 3; ++frame) {
      (void)session.advance({});
    }
    require(
        surface.visible && surface.active && session.status(Screen::title) == 0,
        "Ready title becomes visible without inheriting blocked input or time");
    (void)session.advance({0, 0x1010});
    for (unsigned frame = 0;
         frame < 20 &&
         session.progress(Screen::title).status != FlowStatus::complete;
         ++frame) {
      (void)session.advance({0.25F, 0});
    }
    require(session.progress(Screen::title).status == FlowStatus::complete &&
                session.status(Screen::title) == 1 && !surface.active &&
                !surface.visible && !media.active,
            "Accepted title publishes outcome one and retires both surface and "
            "movie");
    require(session.pending(Screen::title),
            "Title result does not bypass the scheduler idle transition");
    (void)session.advance({});
    require(!session.pending(Screen::title),
            "Title Wait observes idle and requests destruction");
    (void)session.advance({});
    require(!surface.active && !media.active,
            "Task destruction preserves retired presentation state");
    session.execute(Screen::title);
    (void)session.advance({});
    require(
        surface.active && media.active && session.status(Screen::title) == 0,
        "Re-execution starts fresh resources and clears published outcome");
  }
  require(!surface.active && !surface.visible && !media.active,
          "Session destruction retires an active title and its movie");
}
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
  rejects([&] { session.execute(Screen::title); },
          "Title requires an explicit presentation capability");
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
  title_readiness_and_lifecycle();
}
