// Memory migration: native composition injects a reclaiming Domain; allocator-
// bearing samples and containers retain it through worker shutdown and caller
// retention. Default construction is the explicit legacy tool/test route, not
// fallback on domain exhaustion. Callback PCM reads/writes do not allocate,
// release or enter domain locks. State and queue allocation occurs on control/
// codec owners. See research/engine/subsystems/memory-media-ownership.md for
// pinned references, coverage and external codec/bootstrap exclusions.
// Design MCP R.1 (#rr-raii), implementation E.6 (#re-raii) retain lifetime;
// both phases apply MEM.6 (guidelines/memory/
// MEM.6-inject-allocators-behind-one-interface.md) for injected ownership and
// MEM.9 (guidelines/memory/MEM.9-embedded-allocate-at-init.md) for callback
// preallocation. Domain zeroing adds O(bytes) work on allocation, never
// callback. Shared media control and video selection; no game/fade/native API
// policy. Decoder metadata exists before clock choice; independent native audio
// consumes PCM without advance/draw calls. Player owns the session, borrows
// Output, and retires its audio consumer before stopping decoder workers. See
// docs/video-player.md. Algorithm: open/prime -> retain first video -> accept
// first presentation -> start native audio -> select due frames using consumed
// audio snapshots (wall time only without audio or after explicit audio drain)
// -> require both stream ends and a visible terminal-frame interval. Device or
// decoder errors throw; cancellation is not completion. At most one future
// frame is retained beyond decoder queues; dropped late video is counted.
// Design/implementation MCP: CP.23 (#rconc-join) makes source lifetime enclose
// native and decoder workers; cpp-performance MEM.9 (guidelines/memory/
// MEM.9-embedded-allocate-at-init.md) bounds callback storage in the output,
// while this control layer moves owned frames instead of duplicating pixels.
#include "bw/video/player.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

namespace bw::video {
namespace {
using Clock = std::chrono::steady_clock;
double elapsed(Clock::time_point started) {
  return std::chrono::duration<double>(Clock::now() - started).count();
}
}  // namespace
struct Player::State {
  Output& output;
  OpenDecoder factory;
  PlayerLimits limits;
  std::unique_ptr<Decoder> decoder;
  std::optional<VideoFrame> pending;
  PlaybackStatistics stats;
  AudioSnapshot audio;
  double media_time{}, video_end{}, wall_time{}, last_presented_at{},
      selected_duration{};
  bool video_ended{}, started{}, audio_drained{}, complete{};
  std::size_t presented{}, completed{}, drawn_upload{};
  Clock::time_point opened{};
  State(Output& device, OpenDecoder open, PlayerLimits bounds)
      : output(device), factory(std::move(open)), limits(bounds) {
    if (!factory || limits.prime_audio_frames == 0) {
      throw std::invalid_argument(
          "Movie player requires a decoder and positive priming target");
    }
  }
  ~State() { stop(); }
  State(const State&) = delete;
  State& operator=(const State&) = delete;
  State(State&&) = delete;
  State& operator=(State&&) = delete;
  void stop() noexcept {
    output.audio().stop();
    if (decoder) {
      decoder->stop();
      decoder.reset();
    }
    output.clear_video();
    pending.reset();
    media_time = video_end = wall_time = last_presented_at = selected_duration =
        0;
    video_ended = started = audio_drained = complete = false;
    drawn_upload = 0;
    audio = {};
  }
  void select() {
    std::optional<VideoFrame> due;
    while (!video_ended) {
      if (!pending) {
        auto result = decoder->poll_video();
        switch (result.state) {
          case DecodeState::pending:
            break;
          case DecodeState::end:
            video_ended = true;
            break;
          case DecodeState::failed:
            std::rethrow_exception(result.failure);
          case DecodeState::stopped:
            throw std::runtime_error("Movie decoder stopped unexpectedly");
          case DecodeState::sample:
            pending = std::move(result.sample);
            if (!pending || !std::isfinite(pending->timestamp_seconds) ||
                !std::isfinite(pending->duration_seconds) ||
                pending->duration_seconds < 0) {
              throw std::runtime_error("Invalid movie video sample");
            }
            ++stats.decoded_frames;
            video_end = std::max(video_end, pending->timestamp_seconds +
                                                pending->duration_seconds);
            break;
        }
      }
      if (!pending || pending->timestamp_seconds > media_time) {
        break;
      }
      due = std::move(pending);
      pending.reset();
    }
    if (due) {
      output.upload(*due);
      selected_duration = due->duration_seconds;
      ++presented;
      ++stats.uploaded_frames;
    }
  }
  void finish() {
    const bool final_presented =
        stats.uploaded_frames != 0 && drawn_upload == stats.uploaded_frames &&
        wall_time - last_presented_at >= selected_duration;
    stats.final_frame_presented = video_ended && final_presented;
    if (!complete && video_ended && !pending && media_time >= video_end &&
        final_presented && (!decoder->metadata().has_audio || audio.drained)) {
      complete = true;
      ++completed;
    }
  }
  void observe() {
    const auto diagnostics = decoder->diagnostics();
    stats.source_audio_frames = diagnostics.audio_frames;
    stats.queued_audio_frames = audio.submitted_frames;
    stats.played_audio_frames = audio.played_frames;
    stats.silence_frames = audio.initial_silence_frames;
    stats.underruns = audio.underruns;
    stats.audio_starvation_seconds =
        static_cast<double>(audio.maintenance_frames) / kAudioSampleRate;
    stats.largest_audio_timestamp_gap =
        diagnostics.audio_anchor_max_error_seconds;
    stats.wall_seconds = elapsed(opened);
    stats.media_seconds = media_time;
    stats.video_end_seconds = video_end;
    stats.audio_end_seconds = audio.media_seconds;
  }
};
Player::Player(Output& output, OpenDecoder factory, PlayerLimits limits,
               const std::shared_ptr<memory::Domain>& domain)
    : state_(std::allocate_shared<State>(domain
                                             ? memory::Allocator<State>(domain)
                                             : memory::Allocator<State>{},
                                         output, std::move(factory), limits)) {}
Player::~Player() = default;
void Player::open(const std::filesystem::path& path) {
  auto& state = *state_;
  state.stop();
  state.stats = {};
  const auto prime_started = Clock::now();
  state.decoder = state.factory(path);
  if (!state.decoder) {
    throw std::runtime_error("Movie factory returned no decoder");
  }
  state.decoder->wait_until_ready(state.limits.prime_audio_frames);
  state.stats.prebuffer_seconds = elapsed(prime_started);
  state.opened = Clock::now();
  state.select();
  if (state.decoder->metadata().has_audio) {
    state.output.audio().prepare(*state.decoder,
                                 state.limits.prime_audio_frames);
  }
}
void Player::stop() noexcept { state_->stop(); }
void Player::advance(float wall_delta) {
  if (!std::isfinite(wall_delta) || wall_delta < 0) {
    throw std::runtime_error("Movie wall delta must be finite and nonnegative");
  }
  auto& state = *state_;
  if (!state.decoder || state.complete) {
    return;
  }
  if (const auto failure = state.decoder->failure()) {
    std::rethrow_exception(failure);
  }
  const auto previous = state.media_time;
  if (state.started) {
    state.wall_time += wall_delta;
    if (state.decoder->metadata().has_audio && !state.audio_drained) {
      const auto snapshot = state.output.audio().snapshot();
      if (snapshot.failure_code != 0) {
        throw std::runtime_error("Native media audio failed: " +
                                 std::to_string(snapshot.failure_code));
      }
      if (snapshot.valid) {
        state.audio = snapshot;
        state.media_time = snapshot.media_seconds;
        state.audio_drained = snapshot.drained;
      }
    } else {
      state.media_time += wall_delta;
    }
  }
  state.stats.largest_clock_step =
      std::max(state.stats.largest_clock_step, state.media_time - previous);
  state.select();
  state.observe();
  state.finish();
}
void Player::draw(float opacity) {
  auto& state = *state_;
  const auto before = Clock::now();
  if (state.output.draw(opacity) && state.decoder) {
    if (state.drawn_upload != state.stats.uploaded_frames) {
      state.drawn_upload = state.stats.uploaded_frames;
      state.last_presented_at = state.wall_time;
    }
    if (!state.started) {
      if (state.decoder->metadata().has_audio) {
        state.output.audio().start();
      }
      state.started = true;
    }
    state.finish();
  }
  state.stats.largest_draw_seconds =
      std::max(state.stats.largest_draw_seconds, elapsed(before));
}
bool Player::complete() const { return state_->complete; }
const PlaybackStatistics& Player::statistics() const { return state_->stats; }
std::size_t Player::presented_frames() const { return state_->presented; }
std::size_t Player::completed_movies() const { return state_->completed; }
}  // namespace bw::video
