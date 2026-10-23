// Fixed SPSC PCM transport implementation; see pcm_buffer.hpp for ownership,
// wrap/copy algorithm and cost. No waits, callbacks or storage ownership
// changes occur here. Immutable capacity bounds every subspan. Counters are
// unsigned monotonic frame positions; operation lifetimes must not exceed
// uint64 range. Design/implementation MCP: CP.43 (#rconc-time), CONC.5
// (guidelines/concurrency/ CONC.5-spsc-no-cas-needed.md) keep the callback off
// shared locks; MEM.9 (guidelines/memory/MEM.9-embedded-allocate-at-init.md)
// allocates only at init.
#include "pcm_buffer.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>

#include "bw/video/decoder.hpp"

namespace bw::video {
namespace {
std::size_t valid_frames(std::size_t frames) {
  if (frames == 0 || frames > std::numeric_limits<std::size_t>::max() /
                                  kAudioChannelCount / sizeof(float)) {
    throw std::invalid_argument("PCM ring capacity is invalid");
  }
  return frames;
}
}  // namespace
static_assert(std::atomic<std::uint64_t>::is_always_lock_free);
PcmBuffer::PcmBuffer(std::size_t frames)
    : capacity_(valid_frames(frames)),
      samples_(capacity_ * kAudioChannelCount),
      segments_(kSegmentCapacity) {}
std::size_t PcmBuffer::write(std::span<const float> source,
                             double timestamp_seconds) noexcept {
  const auto segment = segment_written_.load(std::memory_order_relaxed);
  if (segment - segment_read_.load(std::memory_order_acquire) ==
      kSegmentCapacity) {
    return 0;
  }
  const auto written = written_.load(std::memory_order_relaxed);
  const auto read = read_.load(std::memory_order_acquire);
  const auto free = capacity_ - static_cast<std::size_t>(written - read);
  const auto frames = std::min(free, source.size() / kAudioChannelCount);
  if (frames == 0) {
    return 0;
  }
  const auto offset = static_cast<std::size_t>(written % capacity_);
  const auto first = std::min(frames, capacity_ - offset);
  auto storage = std::span(samples_);
  std::ranges::copy(source.first(first * kAudioChannelCount),
                    storage.subspan(offset * kAudioChannelCount).begin());
  std::ranges::copy(source.subspan(first * kAudioChannelCount,
                                   (frames - first) * kAudioChannelCount),
                    storage.begin());
  segments_[static_cast<std::size_t>(segment % kSegmentCapacity)] = {
      written, written + frames, timestamp_seconds};
  written_.store(written + frames, std::memory_order_release);
  segment_written_.store(segment + 1, std::memory_order_release);
  return frames;
}
PcmRead PcmBuffer::read(std::span<float> destination) noexcept {
  const auto segment_index = segment_read_.load(std::memory_order_relaxed);
  if (segment_index == segment_written_.load(std::memory_order_acquire)) {
    return {};
  }
  const auto read = read_.load(std::memory_order_relaxed);
  const auto segment =
      segments_[static_cast<std::size_t>(segment_index % kSegmentCapacity)];
  const auto written = segment.end;
  const auto frames = std::min(static_cast<std::size_t>(written - read),
                               destination.size() / kAudioChannelCount);
  const auto offset = static_cast<std::size_t>(read % capacity_);
  const auto first = std::min(frames, capacity_ - offset);
  const auto storage = std::span<const float>(samples_);
  std::ranges::copy(
      storage.subspan(offset * kAudioChannelCount, first * kAudioChannelCount),
      destination.begin());
  std::ranges::copy(storage.first((frames - first) * kAudioChannelCount),
                    destination.subspan(first * kAudioChannelCount).begin());
  read_.store(read + frames, std::memory_order_release);
  if (read + frames == segment.end) {
    segment_read_.store(segment_index + 1, std::memory_order_release);
  }
  return {frames,
          segment.timestamp_seconds +
              static_cast<double>(read - segment.first) / kAudioSampleRate};
}
bool PcmBuffer::writable() const noexcept {
  return written_.load(std::memory_order_relaxed) -
                 read_.load(std::memory_order_acquire) <
             capacity_ &&
         segment_written_.load(std::memory_order_relaxed) -
                 segment_read_.load(std::memory_order_acquire) <
             kSegmentCapacity;
}
std::size_t PcmBuffer::available() const noexcept {
  const auto read = read_.load(std::memory_order_acquire);
  const auto written = written_.load(std::memory_order_acquire);
  return std::min(capacity_, static_cast<std::size_t>(written - read));
}
std::uint64_t PcmBuffer::consumed_frames() const noexcept {
  return read_.load(std::memory_order_relaxed);
}
std::size_t PcmBuffer::capacity() const noexcept { return capacity_; }
}  // namespace bw::video
