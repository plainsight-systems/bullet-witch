// One session owns original music command admission and envelope state.
// Frontend/mission borrow commands only; they cannot advance time, prepare a
// track, write gain or stop the device directly. The stable owner outlives all
// command handles and borrows the output/preparer until teardown. Source
// 44bd90/44c190/44bdf0: playing ignores a new play unless stop is pending;
// a play during fade-out waits for the old fade, then starts on the next tick.
// Caller supplies the existing control interval once per service tick. No
// clock, asynchronous callbacks or steady-state allocation. Preparation occurs
// only on track admission. Core I.1/E.6 and performance MEM.9 were read for
// design and implementation. See mission-music-ownership.md for source and
// verification.
#pragma once
#include <cstdint>
#include <functional>

#include "bw/audio/music.hpp"

namespace bw::game {
class MusicPlayback;
struct MusicRequest {
  std::uint32_t track{};
  float fade_seconds{};
};
class MusicCommands {
 public:
  void play(MusicRequest request) const;
  void stop(float fade_seconds) const;

 private:
  friend class MusicPlayback;
  explicit MusicCommands(MusicPlayback& owner) : owner_(&owner) {}
  MusicPlayback* owner_;
};
class MusicPlayback final {
 public:
  using Prepare = std::function<void(std::uint32_t)>;
  MusicPlayback(audio::MusicOutput&, Prepare);
  ~MusicPlayback();
  MusicPlayback(const MusicPlayback&) = delete;
  MusicPlayback& operator=(const MusicPlayback&) = delete;
  MusicPlayback(MusicPlayback&&) = delete;
  MusicPlayback& operator=(MusicPlayback&&) = delete;
  MusicCommands commands() { return MusicCommands(*this); }
  // True reports track preparation so composition can rebase wall sampling.
  bool advance(float control_seconds);
  // Composition retires mission music after its script owners, before root
  // retirement observes completion. This also discards pending commands.
  void retire() noexcept;

 private:
  friend class MusicCommands;
  void play(MusicRequest request);
  void stop(float fade_seconds);
  enum class Phase : std::uint8_t { idle, fading_in, playing, fading_out };
  enum class Command : std::uint8_t { none, play, stop };
  audio::MusicOutput& output_;
  Prepare prepare_;
  Phase phase_{Phase::idle};
  Command command_{Command::none};
  std::uint32_t requested_track_{};
  float requested_fade_{}, stop_fade_{}, duration_{}, remaining_{};
  float gain_{}, fade_start_{};
};
}  // namespace bw::game
