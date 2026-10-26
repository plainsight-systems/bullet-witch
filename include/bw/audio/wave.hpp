// Prepared CPU sound samples, separate from device mixing and game cue policy.
// Decode copies immutable XACT WMAv2 packets during loading; input spans may be
// released afterward. The caller supplies a finite sample budget and a domain.
// XACT seek duration retains a zero-filled EOF tail of at most one codec frame.
// Sample ranges are frame-based, idempotent and interleaved: loops, seeks and
// resampler lookahead read the same prepared samples without decoder mutation.
// Invalid metadata, truncated packets or decoded overrun or excessive EOF
// padding throw before publication. Allocation is confined to preparation;
// read() allocates nothing and returns false (without writing) for an invalid
// channel-aligned range. Cost O(encoded packets + decoded samples), retained
// sizeof(float)*samples. E.6 (#re-raii) scopes codec resources; MEM.9
// (guidelines/memory/ MEM.9-embedded-allocate-at-init.md) moves decoding out of
// native callbacks. Both design corpora consulted. Reference:
// animation-audio-presentation.md.
#pragma once
#include "bw/content/formats/audio_bank.hpp"

namespace bw::audio {
// Explicit WMAv2 packet metadata shared by XACT and RIFF XWMA containers.
// Spans are borrowed only for decode; no container encoding is synthesized.
struct WmaWave {
  std::uint32_t channels{}, sample_rate{}, average_bytes_per_second{};
  std::uint32_t block_bytes{}, duration{};
  std::span<const std::uint8_t> encoded, seek_entries;
};
class PreparedWave {
 public:
  static PreparedWave decode(const formats::AudioWave& source,
                             std::size_t maximum_samples,
                             const std::shared_ptr<memory::Domain>& domain);
  static PreparedWave decode(const WmaWave& source, std::size_t maximum_samples,
                             const std::shared_ptr<memory::Domain>& domain);
  std::uint32_t sample_rate() const { return sample_rate_; }
  std::uint32_t channels() const { return channels_; }
  std::size_t frames() const { return samples_.size() / channels_; }
  std::span<const float> samples() const { return samples_; }
  bool read(std::size_t first_frame,
            std::span<float> destination) const noexcept;

 private:
  explicit PreparedWave(const std::shared_ptr<memory::Domain>& domain);
  std::uint32_t sample_rate_{}, channels_{1};
  memory::Vector<float> samples_;
};
}  // namespace bw::audio
