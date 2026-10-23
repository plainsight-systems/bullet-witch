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
// callback. Backend independence: C.129 (#rh-kind) keeps concrete factory
// selection out of this interface. E.6 (#re-raii) and performance COPY.5
// (guidelines/copy-move/ COPY.5-resource-owning-types-are-move-only.md)
// preserve owned session lifetime. Portable asynchronous movie session:
// immutable metadata and independent video and PCM consumption. See
// docs/video-decoder.md and the reference-backed movie-demux-and-decode-design
// study. A selected factory completes acquisition before starting
// demux/audio/video owners; it throws on acquisition failure.
// poll_video/failure/wait_until_ready are control-thread operations. Exactly
// one audio consumer may call read_audio; it only copies prepared float PCM and
// uses lock-free atomics, never allocates, decodes, logs or takes a mutex.
// Retire that consumer before stop/destruction. stop wakes and joins workers;
// it is idempotent. Samples already moved to callers remain caller-owned.
// Algorithm: route compressed packets to independent bounded stream queues;
// decode each stream independently; publish BGRA frames and continuous stereo
// PCM; drain each codec/converter before its own end. Empty means pending until
// true stream end. Errors/cancellation never masquerade as EOF.
// Cost: fixed descriptor/PCM storage at open, bounded compressed payload and
// decoded video reservations; codec internals remain separately owned external
// allocations. Video conversion allocates one owned frame; PCM reads are
// O(requested frames). Stream limits do not bound samples retained by callers.
// Design MCP: cpp-guidelines CP.26 (#Rconc-detach) requires joined owners;
// cpp-performance COPY.2 (guidelines/copy-move/
// COPY.2-mark-move-constructors-noexcept.md) preserves cheap ownership moves.
// Implementation MCP: CP.42 (#rconc-wait), CP.43 (#rconc-time) use predicate
// waits and short bookkeeping locks; cpp-performance CONC.5 (guidelines/
// concurrency/CONC.5-spsc-no-cas-needed.md) defines single-owner PCM indices;
// MEM.9 (guidelines/memory/MEM.9-embedded-allocate-at-init.md) preallocates the
// callback-visible ring. These do not claim allocation-free codec operation.
#pragma once

#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <type_traits>

#include "bw/memory/allocator.hpp"

namespace bw::video {
inline constexpr std::uint32_t kAudioSampleRate = 48000;
inline constexpr std::uint32_t kAudioChannelCount = 2;
inline constexpr std::size_t kMebibyte = std::size_t{1024} * 1024;
inline constexpr std::size_t kCompressedBytes = 16 * kMebibyte;
inline constexpr std::size_t kStreamCompressedBytes = 12 * kMebibyte;
inline constexpr std::size_t kCompressedPackets = 8192;
inline constexpr std::size_t kStreamCompressedPackets = 6144;
inline constexpr std::size_t kSinglePacketBytes = 4 * kMebibyte;
inline constexpr std::size_t kVideoSlots = 3;
inline constexpr std::size_t kDecodedVideoBytes = 128 * kMebibyte;
struct DecoderLimits {
  std::size_t compressed_bytes = kCompressedBytes;
  std::size_t stream_compressed_bytes = kStreamCompressedBytes;
  std::size_t compressed_packets = kCompressedPackets;
  std::size_t stream_compressed_packets = kStreamCompressedPackets;
  std::size_t single_packet_bytes = kSinglePacketBytes;
  std::size_t video_slots = kVideoSlots;
  std::size_t video_bytes = kDecodedVideoBytes;
  std::size_t audio_frames = kAudioSampleRate;
};
struct DecoderMetadata {
  bool has_audio = false;
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  double video_start_seconds = 0;
  double audio_start_seconds = 0;
};
struct VideoFrame {
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  double timestamp_seconds = 0;
  double duration_seconds = 0;
  memory::Vector<std::uint8_t> bgra;
};
static_assert(std::is_nothrow_move_constructible_v<VideoFrame>);
enum class DecodeState : std::uint8_t { pending, sample, end, failed, stopped };
struct VideoPoll {
  DecodeState state = DecodeState::pending;
  std::optional<VideoFrame> sample;
  std::exception_ptr failure;
};
struct AudioRead {
  DecodeState state = DecodeState::pending;
  std::size_t frame_count = 0;
  double timestamp_seconds = 0;
};
struct DecoderDiagnostics {
  std::size_t compressed_peak_bytes = 0;
  std::size_t compressed_peak_packets = 0;
  std::size_t video_peak_bytes = 0;
  std::uint64_t video_frames = 0;
  std::uint64_t audio_frames = 0;
  std::uint64_t audio_timestamp_anchors = 0;
  double audio_anchor_bias_seconds = 0;
  double audio_anchor_max_error_seconds = 0;
};
class Decoder {
 public:
  virtual ~Decoder() = default;
  Decoder(const Decoder&) = delete;
  Decoder& operator=(const Decoder&) = delete;
  Decoder(Decoder&&) = delete;
  Decoder& operator=(Decoder&&) = delete;
  virtual const DecoderMetadata& metadata() const noexcept = 0;
  virtual VideoPoll poll_video() = 0;
  // Destination contains interleaved stereo floats; an odd trailing float is
  // unused. End is returned only on a later empty read after the final PCM.
  virtual AudioRead read_audio(std::span<float> destination) noexcept = 0;
  virtual std::size_t buffered_audio_frames() const noexcept = 0;
  // True after codec/resampler drain; remaining ring PCM may still be read.
  virtual bool audio_ended() const noexcept = 0;
  // No audio consumer during priming. Requires target <= audio ring capacity;
  // short streams may satisfy it through true EOF. Throws on failure/stop.
  virtual void wait_until_ready(std::size_t audio_frames) = 0;
  virtual std::exception_ptr failure() const = 0;
  virtual DecoderDiagnostics diagnostics() const = 0;
  virtual void stop() noexcept = 0;

 protected:
  Decoder() = default;
};
}  // namespace bw::video
