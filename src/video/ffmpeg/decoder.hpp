// Explicit FFmpeg software-backend factory, selected by target composition.
// Requires a supported local movie; open probes read-only input and returns one
// owned asynchronous Decoder or throws. No FFmpeg types escape this header.
// The consumer must retire native callbacks before destroying the decoder.
// Algorithm: validate/probe -> construct bounded demux/audio/video owners ->
// transfer unique session ownership. Costs and limits are in
// docs/video-decoder.md. Design MCP: C.129 (#rh-kind) keeps the abstract
// Decoder free of backend choice; cpp-performance COPY.5 (guidelines/copy-move/
// COPY.5-resource-owning-types-are-move-only.md) transfers a unique owner.
// Implementation MCP: E.6 (#re-raii) retains exception-safe owner cleanup;
// COPY.5 above preserves unique ownership through composition.
#pragma once
#include "bw/video/decoder.hpp"

namespace bw::video::ffmpeg {
std::unique_ptr<Decoder> open_decoder(const std::filesystem::path& path);
std::unique_ptr<Decoder> open_decoder_with_limits(
    const std::filesystem::path& path, DecoderLimits limits);
}  // namespace bw::video::ffmpeg
