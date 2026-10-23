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
// callback. Backend composition: callers supply the decoder factory explicitly.
// C.129
// (#rh-kind) separates interface from backend; E.6 (#re-raii) and performance
// COPY.5 (guidelines/copy-move/COPY.5-resource-owning-types-are-move-only.md)
// preserve existing owner lifetimes without per-frame selection overhead.
// Reusable movie control, independent of game screens, fades and native APIs.
// Output outlives Player; audio callbacks retire before its Decoder is stopped.
// Immutable stream metadata selects the clock before any PCM arrives. A native
// audio consumer runs independently; advance only samples its coherent played
// timeline and selects due video. Open primes and selects initial video; first
// accepted draw starts playback. Video-only/tail clocks use measured wall time.
// EOF, device drain and terminal-frame presentation interval all gate
// completion. Failures throw; stop cancels and joins owners. At most one future
// frame beyond decoder queues; move-only payload transfer, explicit output
// upload/copy costs. See docs/video-player.md. Design/implementation MCP: CP.23
// (#rconc-join) scopes borrowed source lifetime; cpp-performance MEM.9
// (guidelines/memory/ MEM.9-embedded-allocate-at-init.md) bounds native
// callback storage separately.
#pragma once
#include <filesystem>
#include <functional>
#include <memory>

#include "bw/video/output.hpp"

namespace bw::video {
struct PlaybackStatistics {
  std::size_t decoded_frames{}, uploaded_frames{}, source_audio_frames{},
      queued_audio_frames{}, silence_frames{};
  double wall_seconds{}, media_seconds{}, video_end_seconds{},
      audio_end_seconds{}, largest_clock_step{}, audio_starvation_seconds{},
      largest_draw_seconds{}, largest_audio_timestamp_gap{},
      prebuffer_seconds{};
  std::size_t played_audio_frames{}, underruns{};
  bool final_frame_presented{};
};
struct PlayerLimits {
  static constexpr std::size_t kDefaultPrimeFrames = 4096;
  std::size_t prime_audio_frames = kDefaultPrimeFrames;
};
class Player {
 public:
  using OpenDecoder =
      std::function<std::unique_ptr<Decoder>(const std::filesystem::path&)>;
  explicit Player(Output& output, OpenDecoder open_decoder,
                  PlayerLimits limits = {},
                  const std::shared_ptr<memory::Domain>& domain = {});
  ~Player();
  Player(const Player&) = delete;
  Player& operator=(const Player&) = delete;
  Player(Player&&) = delete;
  Player& operator=(Player&&) = delete;
  void open(const std::filesystem::path& path);
  void stop() noexcept;
  void advance(float wall_delta);
  void draw(float opacity);
  bool complete() const;
  const PlaybackStatistics& statistics() const;
  std::size_t presented_frames() const;
  std::size_t completed_movies() const;

 private:
  struct State;
  std::shared_ptr<State> state_;
};
}  // namespace bw::video
