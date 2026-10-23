// G2: recovered title screen policy and overlay animation.
// Change axis: original game behavior, not media transport.
// Inputs: finite nonnegative delta, pressed action edges and title resource
// readiness. Outputs: visibility, delayed published result, overlay opacity.
// Exclusively borrows LogoMedia until destruction; single-threaded, nonmovable
// identity owns exactly one movie operation. Exceptions fail visibly and stop;
// an ordinary movie-open rejection follows the recovered timed fallback.
// Algorithm: start the movie; execute one original phase branch per update;
// advance the title's four animation values and phase clock after that branch.
// Resource waits advance neither game animation nor phase clock. Media clocks
// remain the media owner's responsibility. Cleanup precedes completion.
// Costs: O(1) state and update work, no successful update allocations;
// diagnostic strings allocate only on failure. See startup-screen-behavior.md.
// Design and implementation MCP: cpp-guidelines I.1 (#ri-explicit) exposes
// readiness and media observations; cpp-performance COPY.5
// (guidelines/copy-move/COPY.5-resource-owning-types-are-move-only.md) prevents
// duplicate movie cleanup; moves are disabled to preserve borrowed identity.
#pragma once
#include <array>

#include "startup_flows.hpp"

namespace bw::game {
struct TitlePresentation {
  // Original submission order: sprite 424, 423, 452; version shares 452
  // opacity.
  std::array<float, 3> sprite_opacity{};
  float version_opacity{};
};
class TitleFlow {
 public:
  explicit TitleFlow(LogoMedia& media) : media_(media) {}
  ~TitleFlow();
  TitleFlow(const TitleFlow&) = delete;
  TitleFlow& operator=(const TitleFlow&) = delete;
  TitleFlow(TitleFlow&&) = delete;
  TitleFlow& operator=(TitleFlow&&) = delete;
  void start();
  void update(LogoUpdate input, bool resources_ready);
  ScreenProgress progress() const { return progress_; }
  TitlePresentation presentation() const;
  const std::string& diagnostic() const { return diagnostic_; }

 private:
  enum class Phase : std::uint8_t {
    initialize,
    reveal,
    revealing,
    waiting,
    accept,
    accepting,
    accept_fade,
    accepted,
    accepted_wait,
    accepted_route,
    timeout,
    timeout_movie,
    timeout_fade,
    cleanup,
    finish
  };
  struct Pulse {
    float elapsed{};
    float duration{1.3F};
    float value{};
    bool paused{true};
    bool constant{};
    void update(float delta);
  };
  void transition(Phase phase);
  void stop() noexcept;
  void animate(float delta);
  std::reference_wrapper<LogoMedia> media_;
  Phase phase_{Phase::initialize};
  ScreenProgress progress_;
  float elapsed_{};
  float common_opacity_{1};
  float common_target_{1};
  float detail_opacity_{};
  float detail_target_{};
  Pulse prompt_;
  Pulse prompt_highlight_;
  unsigned pending_result_{};
  bool owns_movie_{};
  bool movie_available_{};
  std::string diagnostic_;
};
}  // namespace bw::game
