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
// callback. Private prepared-PCM transport. One codec producer and one native
// audio consumer own opposite indices; no reset or destruction until both
// retire. Construction allocates fixed stereo storage. write/read copy bounded
// spans, return frame counts and never allocate, destroy payloads or acquire a
// mutex. A full write/empty read returns zero; the session owns
// waiting/cancellation. Algorithm: acquire the opposite cursor, bound by
// free/available frames, copy up to two wrap spans, release-publish the local
// cursor. Monotonic cursors also identify each output sample's continuous
// timeline. Observer availability is approximate, clamped to capacity; it is
// not an atomic playback snapshot. Source study: Godot4.5 AudioRBResampler uses
// fixed PCM storage and separate read/write positions. This narrower transport
// has no rate conversion or engine-object dependencies; codec-side swresample
// owns conversion already. Design/implementation MCP: cpp-guidelines CP.26
// scoped ownership outlives callers; CP.43 (#rconc-time) removes the callback
// mutex. Performance CONC.5
// (guidelines/concurrency/CONC.5-spsc-no-cas-needed.md) uses one
// acquire/release handoff per batch; MEM.9
// (guidelines/memory/MEM.9-embedded-allocate-at-init.md) allocates storage
// before native output. Cost O(copied frames), fixed PCM and 1024 segment
// descriptors.
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <span>

#include "bw/memory/allocator.hpp"

namespace bw::video {
struct PcmRead {
  std::size_t frames = 0;
  double timestamp_seconds = 0;
};
class PcmBuffer {
 public:
  static constexpr std::size_t kSegmentCapacity = 1024;
  explicit PcmBuffer(std::size_t frames,
                     const memory::Allocator<float>& allocator = {});
  std::size_t write(std::span<const float> source,
                    double timestamp_seconds) noexcept;
  PcmRead read(std::span<float> destination) noexcept;
  bool writable() const noexcept;
  std::size_t available() const noexcept;
  std::uint64_t consumed_frames() const noexcept;
  std::size_t capacity() const noexcept;

 private:
  static constexpr std::size_t kCursorAlignment = 128;
  struct Segment {
    std::uint64_t first = 0, end = 0;
    double timestamp_seconds = 0;
  };
  alignas(kCursorAlignment) std::atomic<std::uint64_t> segment_written_{0};
  const std::size_t capacity_;
  memory::Vector<float> samples_;
  memory::Vector<Segment> segments_;
  alignas(kCursorAlignment) std::atomic<std::uint64_t> segment_read_{0};
  alignas(kCursorAlignment) std::atomic<std::uint64_t> written_{0};
  alignas(kCursorAlignment) std::atomic<std::uint64_t> read_{0};
};
}  // namespace bw::video
