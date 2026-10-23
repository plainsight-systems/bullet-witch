// Portable presentation and independently serviced audio output contracts.
// Player borrows Output; prepare_audio borrows a Decoder until stop_audio has
// synchronously retired every callback. Audio feeding continues without game
// ticks or draws. Native code owns its device timeline and publishes a coherent
// snapshot: decoded/submitted/played PCM are different quantities. Initial
// timeline silence advances media; maintenance silence during buffering does
// not. Algorithm: prepare bounded native buffers -> start after first
// presentation -> consume PCM on audio execution -> map played device ranges to
// media -> drain. No device types cross this header. Failures throw on control
// operations; callbacks publish failure for control-thread reporting. One
// control owner. Costs: bounded PCM copies and retained BGRA upload; no decoder
// work in audio callbacks. Design/implementation MCP: CP.23 (#rconc-join) keeps
// borrowed source alive through joined output ownership; cpp-performance MEM.9
// (guidelines/ memory/MEM.9-embedded-allocate-at-init.md) prepares callback
// storage up front.
#pragma once
#include <cstddef>
#include <cstdint>

#include "bw/video/decoder.hpp"

namespace bw::video {
struct AudioSnapshot {
  double media_seconds{};
  std::uint64_t submitted_frames{}, played_frames{}, initial_silence_frames{},
      maintenance_frames{}, underruns{};
  bool valid{}, drained{}, buffering{};
  std::int32_t failure_code{};
};
class AudioOutput {
 public:
  AudioOutput() = default;
  virtual ~AudioOutput() = default;
  AudioOutput(const AudioOutput&) = delete;
  AudioOutput& operator=(const AudioOutput&) = delete;
  AudioOutput(AudioOutput&&) = delete;
  AudioOutput& operator=(AudioOutput&&) = delete;
  virtual void prepare(Decoder& source, std::size_t prime_frames) = 0;
  virtual void start() = 0;
  virtual AudioSnapshot snapshot() const = 0;
  virtual void stop() noexcept = 0;
};
class Output {
 public:
  Output() = default;
  virtual ~Output() = default;
  Output(const Output&) = delete;
  Output& operator=(const Output&) = delete;
  Output(Output&&) = delete;
  Output& operator=(Output&&) = delete;
  virtual void upload(const VideoFrame& frame) = 0;
  virtual void clear_video() noexcept = 0;
  // Acceptance must retain the frame through GPU completion; false retries.
  virtual bool draw(float opacity) = 0;
  virtual AudioOutput& audio() noexcept = 0;
};
}  // namespace bw::video
