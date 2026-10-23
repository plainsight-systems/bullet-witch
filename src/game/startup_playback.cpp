// Memory migration: transfer the selected media capability into Player once.
// R.1 (#rr-raii) and MEM.6 (guidelines/memory/
// MEM.6-inject-allocators-behind-one-interface.md), read in both phases, keep
// allocation choice outside game fade policy and preserve exception cleanup.
// Game-relative movie paths and recovered black-overlay fade; transport lives
// in video::Player. See startup_playback.hpp and docs/native-startup-host.md.
// Reject absolute/parent traversal paths and nonfinite/negative deltas. One
// owning thread; Player outlives every delegated call and releases native
// output on destruction. Fade arithmetic is O(1); opening allocates only the
// path here. Design/implementation MCP: cpp-guidelines R.1 (#rr-raii) delegates
// lifecycle; cpp-performance COPY.5 (guidelines/copy-move/
// COPY.5-resource-owning-types-are-move-only.md) keeps ownership singular.
#include "startup_playback.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace bw::game {
StartupPlayback::StartupPlayback(
    video::Output& output, std::filesystem::path game_directory,
    OpenDecoder open_decoder,
    const std::shared_ptr<memory::Domain>& media_memory)
    : player_(output, std::move(open_decoder), {}, media_memory),
      game_directory_(std::move(game_directory)) {}
bool StartupPlayback::open_movie(std::string_view path) {
  std::string normalized(path);
  std::ranges::replace(normalized, '\\', '/');
  const std::filesystem::path relative(normalized);
  if (relative.empty() || relative.is_absolute()) {
    throw std::runtime_error("Movie path must be relative to game data");
  }
  for (const auto& component : relative) {
    if (component == "..") {
      throw std::runtime_error("Movie path escapes game data");
    }
  }
  player_.open(game_directory_ / relative);
  return true;
}
void StartupPlayback::begin_fade(FadeDirection direction, float duration) {
  if (!std::isfinite(duration) || duration < 0) {
    throw std::runtime_error("Fade duration must be finite and nonnegative");
  }
  fade_direction_ = direction;
  fade_remaining_ = duration;
  // Native zero-duration fades snap on the next update. Avoid its transient
  // division by zero while preserving that update boundary (0x485a20).
  fade_active_ = true;
  fade_rate_ =
      duration == 0
          ? 0
          : (direction == FadeDirection::reveal ? -1.0F : 1.0F) / duration;
}
void StartupPlayback::advance(PlaybackUpdate input) {
  const auto game_delta = input.game_delta;
  if (!std::isfinite(game_delta) || game_delta < 0) {
    throw std::runtime_error("Game delta must be finite and nonnegative");
  }
  if (fade_pending()) {
    fade_remaining_ -= game_delta;
    overlay_alpha_ =
        std::clamp(overlay_alpha_ + fade_rate_ * game_delta, 0.0F, 1.0F);
    if (fade_remaining_ <= 0) {
      fade_remaining_ = 0;
      fade_active_ = false;
      overlay_alpha_ = fade_direction_ == FadeDirection::reveal ? 0.0F : 1.0F;
    }
  }
  player_.advance(input.wall_delta);
}
}  // namespace bw::game
