// G2: recovered opening-cinematic policy, separate from media transport.
// Input: finite nonnegative game delta and pressed action edges. Output:
// visibility and delayed result. Exclusively borrows LogoMedia until
// destruction; single-threaded identity owns exactly one operation. Ordinary
// open rejection follows the recovered three-second fallback; exceptions stop
// and fail. Algorithm: start movie; reveal; wait for completion/input/fallback;
// conceal; hide; stop and publish result. One branch executes per update, then
// delta is added to the resettable phase clock. O(1) state/update work, no
// normal update allocations. See
// research/engine/subsystems/startup-screen-behavior.md. Design/implementation
// MCP: cpp-guidelines I.1 (#ri-explicit) exposes media observations;
// cpp-performance COPY.5
// (guidelines/copy-move/COPY.5-resource-owning-types-are-move-only.md) prevents
// duplicate operation cleanup. Moves are disabled to preserve borrowed
// identity.
#pragma once
#include "startup_flows.hpp"

namespace bw::game {
class AdvertiseFlow {
 public:
  explicit AdvertiseFlow(LogoMedia& media) : media_(media) {}
  ~AdvertiseFlow();
  AdvertiseFlow(const AdvertiseFlow&) = delete;
  AdvertiseFlow& operator=(const AdvertiseFlow&) = delete;
  AdvertiseFlow(AdvertiseFlow&&) = delete;
  AdvertiseFlow& operator=(AdvertiseFlow&&) = delete;
  void start();
  void update(LogoUpdate input);
  ScreenProgress progress() const { return progress_; }
  const std::string& diagnostic() const { return diagnostic_; }

 private:
  enum class Phase : std::uint8_t {
    reveal,
    revealing,
    playing,
    conceal,
    concealing,
    hide,
    finish
  };
  void transition(Phase phase);
  void stop() noexcept;
  std::reference_wrapper<LogoMedia> media_;
  Phase phase_{Phase::reveal};
  ScreenProgress progress_;
  float elapsed_{};
  bool owns_movie_{};
  bool movie_available_{};
  std::string diagnostic_;
};
}  // namespace bw::game
