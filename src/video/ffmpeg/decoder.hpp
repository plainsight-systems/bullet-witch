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
// callback. Explicit FFmpeg software-backend factory, selected by target
// composition. Requires a supported local movie; open probes read-only input
// and returns one owned asynchronous Decoder or throws. No FFmpeg types escape
// this header. The consumer must retire native callbacks before destroying the
// decoder. Algorithm: validate/probe -> construct bounded demux/audio/video
// owners -> transfer unique session ownership. Costs and limits are in
// docs/video-decoder.md. Design MCP: C.129 (#rh-kind) keeps the abstract
// Decoder free of backend choice; cpp-performance COPY.5 (guidelines/copy-move/
// COPY.5-resource-owning-types-are-move-only.md) transfers a unique owner.
// Implementation MCP: E.6 (#re-raii) retains exception-safe owner cleanup;
// COPY.5 above preserves unique ownership through composition.
#pragma once
#include "bw/video/decoder.hpp"

namespace bw::video::ffmpeg {
std::unique_ptr<Decoder> open_decoder_in_domain(
    const std::filesystem::path& path, std::shared_ptr<memory::Domain> domain,
    DecoderLimits limits = {});
// Legacy tool factories; native composition selects the explicit domain above.
std::unique_ptr<Decoder> open_decoder(const std::filesystem::path& path);
std::unique_ptr<Decoder> open_decoder_with_limits(
    const std::filesystem::path& path, DecoderLimits limits);
}  // namespace bw::video::ffmpeg
