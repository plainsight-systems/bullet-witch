// Original segmented XWMA container preparation, separate from device
// transport. Borrow encoded bytes only for this call; return owned interleaved
// PCM and authored frame switch positions. Validate both RIFFs, cumulative
// packet metadata, matching formats and total budget before allocating decoded
// PCM. A second RIFF is followed by absolute first/second switch frame
// positions. Core E.6 (#re-raii) scopes decode failures; performance MEM.9
// (guidelines/memory/MEM.9-embedded-allocate-at-init.md) moves allocation and
// decode off callbacks. Both corpora read for design and implementation.
// Reference: research/engine/subsystems/frontend-audio.md.
#pragma once
#include <span>

#include "music.hpp"

namespace bw::audio {
MusicSamples decode_segmented_music(
    std::span<const std::uint8_t> encoded, std::size_t maximum_samples,
    const std::shared_ptr<memory::Domain>& domain);
}  // namespace bw::audio
