// Shared purchased input-art compatibility: physical scan/button codes resolve
// atlas cells; decode packs the original 149 PNGs into one owned RGBA image.
// Callers own GPU upload/release; this module has no renderer mutation or input
// policy. Decode allocates only during preparation through the supplied memory
// domain. UV and index queries are bounded/allocation-free. Core I.1/C.9 and
// MEM.9 (guidelines/memory/MEM.9-embedded-allocate-at-init.md), read for
// design, preserve explicit ownership and one setup allocation domain.
#pragma once
#include <span>

#include "bw/formats/image.hpp"
#include "bw/render/sprites.hpp"
namespace bw::game {
inline constexpr unsigned kInputIconCount = 149, kInputIconPixels = 128;
unsigned input_key_icon(unsigned code) noexcept;
unsigned input_pad_icon(unsigned code) noexcept;
formats::Image decode_input_icon_atlas(
    std::span<const std::uint8_t> bytes,
    const memory::Allocator<std::uint8_t>& allocator);
render::Rect input_icon_uv(unsigned index);
}  // namespace bw::game
