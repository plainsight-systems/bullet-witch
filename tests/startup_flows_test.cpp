// V1: in-process startup flow contracts with controlled media observations.
// No playback, rendering, native timing or original-root equivalence is
// claimed. Algorithm: exercise visibility/result ordering, independent
// fade/movie gates, playlist advance, pressed masks, missing capabilities and
// cleanup. Fixtures outlive borrowed flows. Bounded work/storage; no real
// file/process boundary. MCP design/implementation: cpp-guidelines I.1
// (#ri-explicit) exposes observations rather than hidden completion counters.
// cpp-performance COPY.5
// (guidelines/copy-move/COPY.5-resource-owning-types-are-move-only.md) tests
// that one flow cleans up its owned operation exactly once, including
// exceptions.
#include "game/startup_flows.hpp"

#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using bw::game::CompanyLogoFlow;
using bw::game::FadeDirection;
using bw::game::FlowStatus;
void require(bool ok, const char* message) {
  if (!ok) {
    throw std::runtime_error(message);
  }
}
class Media final : public bw::game::LogoMedia {
 public:
  bool open_movie(std::string_view path) override {
    if (unavailable) {
      throw std::runtime_error("Movie capability unavailable");
    }
    paths.emplace_back(path);
    return open_ok;
  }
  bool movie_complete() const override { return ended; }
  void stop_movie() noexcept override { ++stops; }
  void begin_fade(FadeDirection direction, float duration) override {
    require(duration == 0.25F, "Recovered fade duration");
    fades.push_back(direction);
  }
  bool fade_pending() const override { return fading; }
  std::vector<std::string> paths;
  std::vector<FadeDirection> fades;
  bool open_ok{true}, ended{}, fading{true}, unavailable{};
  unsigned stops{};
};
void startup_check() {
  bw::game::StartupCheckFlow flow;
  require(flow.progress().status == FlowStatus::idle, "Check initially idle");
  flow.start();
  flow.update(0.5F);
  require(flow.progress().visible && flow.ui_elapsed() == 0.5F &&
              flow.ui_phase() == bw::game::CheckUiPhase::reveal,
          "Show resets secondary phase then adds delta");
  flow.update(0.25F);
  require(flow.ui_elapsed() == 0.25F &&
              flow.ui_phase() == bw::game::CheckUiPhase::hold,
          "Hold resets secondary clock without waiting for time");
  flow.update(0);
  flow.update(0.5F);
  require(flow.ui_phase() == bw::game::CheckUiPhase::close &&
              flow.ui_elapsed() == 0.5F,
          "Close UI phase reset");
  flow.update(0.25F);
  require(flow.progress().visible, "Closing phase stays visible");
  flow.update(0.25F);
  require(!flow.progress().visible && flow.progress().result == 0 &&
              flow.progress().status == FlowStatus::pending,
          "Hide precedes completion");
  flow.update(0);
  require(flow.progress().status == FlowStatus::complete &&
              flow.progress().result == 1,
          "Seventh update requests removal with native result");
  flow.start();
  bool rejected = false;
  try {
    flow.update(std::numeric_limits<float>::quiet_NaN());
  } catch (const std::invalid_argument&) {
    rejected = true;
  }
  require(rejected && !flow.progress().visible,
          "Invalid delta rejected before state mutation");
}
void logos() {
  Media media;
  CompanyLogoFlow flow(media, {"first.ssu", "second.ssu"});
  flow.start();
  flow.update({0, 0});
  flow.update({0.1F, 0});
  require(flow.progress().visible && media.paths.size() == 1,
          "Open first movie and reveal");
  media.ended = true;
  flow.update({0.1F, 0x1010});
  require(
      media.fades.size() == 1 && flow.progress().status == FlowStatus::pending,
      "Movie end and input do not bypass reveal gate");
  media.fading = false;
  media.ended = false;
  flow.update({0.1F, 0});
  flow.update({0.1F, 0x4});
  require(media.fades.size() == 1, "Unrelated input cannot skip movie");
  flow.update({0.1F, 0x2020});
  flow.update({0.1F, 0});
  media.fading = true;
  flow.update({0.1F, 0});
  require(media.paths.size() == 1, "Conceal gate holds playlist advance");
  media.fading = false;
  flow.update({0.1F, 0});
  flow.update({0.1F, 0});
  require(media.paths == std::vector<std::string>{"first.ssu", "second.ssu"} &&
              media.stops == 1,
          "Advance playlist only after conceal, release previous movie");
  flow.update({0.1F, 0});
  media.ended = true;
  flow.update({0.1F, 0});
  flow.update({0.1F, 0});
  flow.update({0.1F, 0});
  flow.update({0.1F, 0});
  require(!flow.progress().visible && flow.progress().result == 0,
          "Final hide precedes result publication");
  flow.update({0.1F, 0});
  require(flow.progress().status == FlowStatus::complete &&
              flow.progress().result == 1 && media.stops == 2 &&
              flow.phase_elapsed() == 0.1F,
          "Normal logo completion releases final movie");
}
void failures_and_cleanup() {
  Media unavailable;
  unavailable.unavailable = true;
  CompanyLogoFlow unsupported(unavailable, {"movie.ssu"});
  unsupported.start();
  unsupported.update({0, 0});
  unsupported.update({0, 0});
  require(unsupported.progress().status == FlowStatus::failed &&
              unsupported.progress().result == 0 &&
              unsupported.diagnostic() == "Movie capability unavailable",
          "Missing capability fails explicitly");
  Media failed;
  failed.open_ok = false;
  CompanyLogoFlow rejected(failed, {"movie.ssu"});
  rejected.start();
  rejected.update({0, 0});
  rejected.update({0, 0});
  require(rejected.progress().status == FlowStatus::failed && failed.stops == 1,
          "Unresolved open-failure continuation cannot fabricate completion");
  Media abandoned;
  {
    CompanyLogoFlow active(abandoned, {"movie.ssu"});
    active.start();
    active.update({0, 0});
    active.update({0, 0});
  }
  require(abandoned.stops == 1, "Destruction releases active movie");
}
}  // namespace
void startup_flow_contracts() {
  startup_check();
  logos();
  failures_and_cleanup();
}
