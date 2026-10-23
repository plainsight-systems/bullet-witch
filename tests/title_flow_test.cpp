// V1: in-process title readiness, timing, animation and outcome contracts.
// Controlled observations outlive the exclusively borrowed flow; no asset,
// native media or renderer execution. Algorithm: cross public state boundaries
// with exact recovered thresholds, including independent movie/fade gates and
// cleanup-before-removal. Fixed bounded work. MCP design/implementation:
// cpp-guidelines I.1 (#ri-explicit) exposes dependencies; cpp-performance
// COPY.5 (guidelines/copy-move/COPY.5-resource-owning-types-are-move-only.md)
// exercises ownership cleanup. Passing does not establish native title
// fidelity.
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>

#include "game/title_flows.hpp"

namespace {
void require(bool condition, const char* message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}
bool near(float first, float second) {
  return std::abs(first - second) < 0.00001F;
}
class Media final : public bw::game::LogoMedia {
 public:
  bool open_movie(std::string_view path) override {
    opened = path;
    return open_ok;
  }
  bool movie_complete() const override { return ended; }
  void stop_movie() noexcept override { ++stops; }
  void begin_fade(bw::game::FadeDirection direction, float duration) override {
    last_fade = direction;
    last_duration = duration;
    ++fades;
  }
  bool fade_pending() const override { return fading; }
  std::string opened;
  bw::game::FadeDirection last_fade{bw::game::FadeDirection::reveal};
  float last_duration{};
  unsigned stops{}, fades{};
  bool open_ok{true}, ended{}, fading{};
};
void enter_waiting(bw::game::TitleFlow& flow) {
  flow.update({0, 0}, true);
  flow.update({0, 0}, true);
  flow.update({0, 0}, true);
}
void readiness_and_animation() {
  Media media;
  bw::game::TitleFlow flow(media);
  flow.start();
  require(media.opened == "data/movie/witchLOGO.ssu",
          "Title opens movie before resource readiness");
  flow.update({100, 0x1010}, false);
  require(!flow.progress().visible && media.fades == 0 &&
              near(flow.presentation().version_opacity, 0),
          "Readiness wait does not advance visibility, phase or animation");
  media.fading = true;
  flow.update({0, 0}, true);
  flow.update({0.5F, 0}, true);
  require(flow.progress().visible && media.last_duration == 1,
          "Title uses recovered one-second reveal");
  flow.update({0.5F, 0x1010}, true);
  require(near(flow.presentation().sprite_opacity[0], 0) &&
              near(flow.presentation().version_opacity, 0),
          "Prompt and details stay paused through initial fade");
  media.fading = false;
  flow.update({0.325F, 0}, true);
  require(near(flow.presentation().sprite_opacity[0], 0) &&
              near(flow.presentation().version_opacity, 0.325F),
          "Pulse evaluates prior phase; detail ramp consumes current delta");
  flow.update({0.325F, 0}, true);
  require(near(flow.presentation().sprite_opacity[0], 0.5F) &&
              near(flow.presentation().sprite_opacity[1], 0.5F) &&
              near(flow.presentation().version_opacity, 0.65F),
          "Recovered pulse phase wraps at 1.3 seconds");
  flow.update({0, 0}, true);
  require(near(flow.presentation().sprite_opacity[0], 1),
          "Recovered pulse reaches full opacity at quarter-cycle");
  bool rejected = false;
  try {
    flow.update({std::numeric_limits<float>::quiet_NaN(), 0}, true);
  } catch (const std::invalid_argument&) {
    rejected = true;
  }
  require(rejected, "Invalid delta rejected explicitly");
}
void timeout(bool movie_available) {
  Media media;
  media.open_ok = movie_available;
  bw::game::TitleFlow flow(media);
  flow.start();
  enter_waiting(flow);
  media.ended = true;
  flow.update({0, 0}, true);
  require(media.fades == 1, "Early movie end cannot bypass title dwell");
  media.ended = false;
  flow.update({movie_available ? 16.5F : 8.0F, 0}, true);
  flow.update({0, 0}, true);
  flow.update({0, 0}, true);
  flow.update({0, 0}, true);
  if (movie_available) {
    require(media.fades == 1,
            "Timeout still waits for movie terminal completion");
    media.ended = true;
    flow.update({0, 0}, true);
  }
  require(media.fades == 2 &&
              media.last_duration == (movie_available ? 0.0F : 0.5F),
          "Natural movie end conceals immediately; missing movie uses "
          "half-second fade");
  media.fading = true;
  flow.update({1, 0}, true);
  require(flow.progress().result == 0, "Fade gates result publication");
  media.fading = false;
  flow.update({0, 0}, true);
  flow.update({0, 0}, true);
  require(!flow.progress().visible && flow.progress().result == 2 &&
              flow.progress().status == bw::game::FlowStatus::pending &&
              media.stops == 1,
          "Cleanup publishes timeout outcome before task removal");
  flow.update({0, 0}, true);
  require(flow.progress().status == bw::game::FlowStatus::complete,
          "Following update requests removal");
}
void accepted() {
  Media media;
  bw::game::TitleFlow flow(media);
  flow.start();
  enter_waiting(flow);
  flow.update({0, 0x2020}, true);
  flow.update({0, 0}, true);
  require(media.fades == 1, "Title ignores advertise-only skip mask");
  flow.update({0, 0x1010}, true);
  flow.update({0, 0}, true);
  require(near(flow.presentation().sprite_opacity[0], 1) &&
              near(flow.presentation().sprite_opacity[1], 0.5F),
          "Acceptance holds highlight and restarts prompt pulse");
  flow.update({0.6F, 0}, true);
  flow.update({0.3F, 0}, true);
  require(near(flow.presentation().sprite_opacity[0], 0.4F),
          "Acceptance dims common opacity after 0.6 seconds at two units per "
          "second");
  flow.update({0, 0}, true);
  require(media.fades == 2 && media.last_duration == 0.5F,
          "Acceptance starts conceal at 0.9 seconds");
  for (unsigned update = 0; update < 5; ++update) {
    flow.update({0, 0}, true);
  }
  require(
      flow.progress().result == 1 && media.stops == 1 &&
          flow.progress().status == bw::game::FlowStatus::pending,
      "Accept cleanup publishes result after recovered intermediate phases");
  flow.update({0, 0}, true);
  require(flow.progress().status == bw::game::FlowStatus::complete,
          "Accepted title removes task next update");
}
}  // namespace
void title_flow_contracts() {
  readiness_and_animation();
  timeout(true);
  timeout(false);
  accepted();
}
