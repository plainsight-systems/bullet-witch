// Prepared music capability on the existing game audio device. A control-thread
// owner moves bounded, domain-backed interleaved PCM into prepare_music(); the
// output retains it until replacement/destruction. No caller sample view is
// retained. Preparation validates rate/channel/frame/loop bounds before
// replacing a previous track. Start restarts the prepared track, stop joins its
// native voice while retaining samples, and destruction joins before sample
// retirement. The optional frame interval loops after the intro; zero length is
// one-shot. A game-owned envelope multiplies the device's music mix gain; no
// world clock, Profile access or fade algorithm belongs here. Native callbacks
// only consume prepared PCM. Core I.1/I.11 and CP.2 (#rconc-races), performance
// MEM.9 (guidelines/memory/MEM.9-embedded-allocate-at-init.md) were read for
// design and implementation: explicit ownership, bounded loading allocations,
// no callback mutation or allocation. See
// research/engine/subsystems/frontend-audio.md.
#pragma once
#include <cstdint>

#include "bw/memory/allocator.hpp"

namespace bw::audio {
struct MusicSamples {
  memory::Vector<float> samples;
  std::uint32_t sample_rate{}, channels{};
  std::uint32_t loop_begin_frame{}, loop_frame_count{};
  std::uint32_t first_switch_frame{}, loop_period_frames{};
};
class MusicOutput {
 public:
  MusicOutput() = default;
  virtual ~MusicOutput() = default;
  MusicOutput(const MusicOutput&) = delete;
  MusicOutput& operator=(const MusicOutput&) = delete;
  MusicOutput(MusicOutput&&) = delete;
  MusicOutput& operator=(MusicOutput&&) = delete;
  virtual void prepare_music(MusicSamples samples) = 0;
  virtual void start_music() = 0;
  virtual void stop_music() noexcept = 0;
  virtual void set_music_envelope(float gain) = 0;
  virtual bool music_active() const = 0;
};
}  // namespace bw::audio
