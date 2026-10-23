// Backend composition: callers supply the decoder factory explicitly. C.129
// (#rh-kind) separates interface from backend; E.6 (#re-raii) and performance
// COPY.5 (guidelines/copy-move/COPY.5-resource-owning-types-are-move-only.md)
// preserve existing owner lifetimes without per-frame selection overhead.
// Thin game adapter from LogoMedia to the reusable video::Player. Owns
// recovered black fade policy and game-relative path resolution; media queues,
// clocks and EOF/drain belong to Player. Output outlives this nonmovable,
// single-owner adapter. Failures throw; stop cancels playback, not the
// independent fade. Algorithm: resolve safe relative path -> delegate open;
// update fade using game delta while Player receives uncapped wall delta ->
// draw with 1-overlay alpha. Fade begin preserves alpha; update uses recovered
// 0x485a20 rate/clamp/snap. Costs: path allocation on open, O(1) fade work per
// tick plus Player's contract. Design/implementation MCP: cpp-guidelines R.1
// (#rr-raii) gives Player sole media ownership; cpp-performance COPY.5
// (guidelines/copy-move/ COPY.5-resource-owning-types-are-move-only.md)
// prevents copied playback owners.
#pragma once
#include <filesystem>

#include "bw/video/player.hpp"
#include "startup_flows.hpp"

namespace bw::game {
struct PlaybackUpdate {
  float wall_delta{};
  float game_delta{};
};
class StartupPlayback final : public LogoMedia {
 public:
  using OpenDecoder = video::Player::OpenDecoder;
  StartupPlayback(video::Output& output, std::filesystem::path game_directory,
                  OpenDecoder open_decoder);
  ~StartupPlayback() override = default;
  StartupPlayback(const StartupPlayback&) = delete;
  StartupPlayback& operator=(const StartupPlayback&) = delete;
  StartupPlayback(StartupPlayback&&) = delete;
  StartupPlayback& operator=(StartupPlayback&&) = delete;
  bool open_movie(std::string_view path) override;
  bool movie_complete() const override { return player_.complete(); }
  void stop_movie() noexcept override { player_.stop(); }
  void begin_fade(FadeDirection direction, float duration) override;
  bool fade_pending() const override { return fade_remaining_ > 0; }
  void advance(PlaybackUpdate input);
  void draw() { player_.draw(opacity()); }
  float opacity() const { return 1 - overlay_alpha_; }
  const video::PlaybackStatistics& statistics() const {
    return player_.statistics();
  }
  std::size_t presented_frames() const { return player_.presented_frames(); }
  std::size_t completed_movies() const { return player_.completed_movies(); }

 private:
  video::Player player_;
  std::filesystem::path game_directory_;
  float overlay_alpha_{1}, fade_remaining_{}, fade_rate_{};
  FadeDirection fade_direction_{FadeDirection::reveal};
};
}  // namespace bw::game
