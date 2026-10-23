// Profile-load lifecycle contracts with in-memory outcomes. No files/native UI.
// Inputs control readiness, failed reads and explicit retry/continue actions;
// outputs must retain pending state and distinguish disabled writes from load
// success. Owners outlive flow. Bounded frames/data. Design MCP I.30
// (#ri-encapsulate), implementation E.6 (#re-raii), cpp-performance MEM.9
// (guidelines/memory/MEM.9-embedded-allocate-at-init.md) isolate fixture work.
#include "game/profile_load_flow.hpp"

#include <stdexcept>

namespace {
void require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}
class Storage final : public bw::platform::SaveStorage {
 public:
  bw::platform::SaveRead read(std::string_view, std::size_t) override {
    return {broken ? bw::platform::SaveStatus::io_error
                   : bw::platform::SaveStatus::missing,
            {}};
  }
  bw::platform::SaveStatus write(std::string_view,
                                 std::span<const std::byte>) override {
    throw std::logic_error("Load must not write");
  }
  bool broken{true};
};
class Media final : public bw::game::LogoMedia {
 public:
  bool open_movie(std::string_view) override { return false; }
  bool movie_complete() const override { return false; }
  void stop_movie() noexcept override {}
  void begin_fade(bw::game::FadeDirection, float) override {}
  bool fade_pending() const override { return fading; }
  bool fading{};
};
void load_error_gate() {
  Storage storage;
  Media media;
  bw::game::Profile profile;
  bw::game::ProfileLoadFlow flow(profile, storage, media);
  flow.start();
  flow.update({100, 0x1010}, false);
  require(!flow.progress().visible && !flow.error_pending(),
          "Readiness gates loading and input");
  for (unsigned i = 0; i < 4; ++i) flow.update({}, true);
  require(flow.error_pending() &&
              flow.progress().status == bw::game::FlowStatus::pending,
          "Read failure requires explicit decision");
  storage.broken = false;
  flow.update({0, 0x1010}, true);
  for (unsigned i = 0; i < 6; ++i) flow.update({}, true);
  require(flow.progress().status == bw::game::FlowStatus::complete &&
              flow.saving_enabled(),
          "Successful retry enables later explicit save");
  storage.broken = true;
  flow.start();
  for (unsigned i = 0; i < 4; ++i) flow.update({}, true);
  flow.update({0, 0x02000000}, true);
  flow.update({0, 0x1010}, true);
  media.fading = true;
  for (unsigned i = 0; i < 5; ++i) flow.update({}, true);
  require(flow.progress().status == bw::game::FlowStatus::pending,
          "Explicit continuation still waits for closing fade");
  media.fading = false;
  for (unsigned i = 0; i < 3; ++i) flow.update({}, true);
  require(flow.progress().status == bw::game::FlowStatus::complete &&
              !flow.saving_enabled(),
          "Continue after load failure forbids later overwrite");
}
}  // namespace
void profile_load_flow_contracts() { load_error_gate(); }
