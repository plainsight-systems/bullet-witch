// V1: in-process advertise policy checks, not native movie acceptance.
// Controlled media outlives its exclusively borrowed flow. Exercises
// independent fade/end gates, pressed skip, recovered open-failure fallback and
// cleanup. O(1) bounded updates; no assets, subprocesses or frame rendering.
// Design and implementation MCP: cpp-guidelines I.1 (#ri-explicit) uses
// explicit observed gates; cpp-performance COPY.5
// (guidelines/copy-move/COPY.5-resource-owning-types-are-move-only.md) verifies
// acquired operation cleanup without duplicate ownership.
#include "game/advertise_flow.hpp"

#include <stdexcept>
#include <string>

namespace {
void require(bool condition, const char* message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
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
    require(duration == 0.25F, "Advertise recovered fade duration");
    last_fade = direction;
    ++fades;
  }
  bool fade_pending() const override { return fading; }
  std::string opened;
  bw::game::FadeDirection last_fade{bw::game::FadeDirection::reveal};
  unsigned stops{}, fades{};
  bool open_ok{true}, fading{true}, ended{};
};
void natural_and_skip(bool skip) {
  Media media;
  bw::game::AdvertiseFlow flow(media);
  flow.start();
  require(
      media.opened == "data/movie/advertise.ssu" && !flow.progress().visible,
      "Advertise opens before becoming visible");
  flow.update({0, 0});
  media.ended = true;
  flow.update({1, 0x1010});
  require(media.fades == 1, "Reveal gate cannot be skipped by end or input");
  media.ended = !skip;
  media.fading = false;
  flow.update({0, 0});
  flow.update({0, skip ? 0x2020U : 0U});
  flow.update({0, 0});
  require(media.last_fade == bw::game::FadeDirection::conceal,
          "End or pressed skip starts conceal");
  media.fading = true;
  flow.update({10, 0});
  require(flow.progress().visible && media.stops == 0,
          "Conceal completion is independent of elapsed game time");
  media.fading = false;
  flow.update({0, 0});
  flow.update({0, 0});
  require(!flow.progress().visible && flow.progress().result == 0,
          "Hide precedes result");
  flow.update({0, 0});
  require(flow.progress().status == bw::game::FlowStatus::complete &&
              flow.progress().result == 1 && media.stops == 1,
          "Completion releases movie and publishes result");
}
void failed_open_fallback() {
  Media media;
  media.open_ok = false;
  media.fading = false;
  bw::game::AdvertiseFlow flow(media);
  flow.start();
  require(media.stops == 1 && !flow.diagnostic().empty(),
          "Failed open releases partial resources and records fallback");
  flow.update({0, 0});
  flow.update({0, 0});
  flow.update({2.9F, 0});
  flow.update({0, 0});
  require(media.fades == 1, "Unavailable movie waits three phase seconds");
  flow.update({0.2F, 0});
  flow.update({0, 0});
  flow.update({0, 0});
  require(media.fades == 2, "Recovered fallback initiates conceal");
}
}  // namespace
void advertise_flow_contracts() {
  natural_and_skip(false);
  natural_and_skip(true);
  failed_open_fallback();
}
