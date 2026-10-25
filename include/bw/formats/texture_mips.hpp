// Portable original texture staging/filter boundary; no renderer or settings.
// Borrow a complete, single-level image for this call and return an independent
// full chain in the supplied allocator domain. Exact byte count, legal shape,
// bounded extents and expanded four-bit precision are checked before filtering.
// Invalid input throws resources::Error; allocation failures propagate. Output
// owns level-major bytes with each level's cube faces in source order. No
// shared state or synchronization; callers retain the immutable base during the
// call. Algorithm: preserve base bytes; independently filter that base to every
// lower extent. Original exact-halving BOX averages adjacent texels; other
// reductions integrate the linear reconstruction over each destination
// footprint with wrap. Quantize UNORM with original serpentine Bayer dither;
// finite HALF rounds even. No sRGB conversion, premultiplication, recursive
// filtering or cross-face taps. Cost: O(base pixels * levels), one output owner
// plus reusable axis coefficients and a horizontal-filter scratch surface, all
// in the supplied domain. Optimization: separable filtering reuses horizontal
// results across rows. Evidence and original-DLL numerical witnesses:
// texture-mip-filter.md. Both design/implementation: Core SL.con.3
// (#rsl-bounds) validates sizes before indexing; Performance MEM.6
// (guidelines/memory/ MEM.6-inject-allocators-behind-one-interface.md) injects
// every owned buffer.
#pragma once

#include "bw/formats/world_texture.hpp"

namespace bw::formats {
WorldTextureImage generate_texture_mips(
    const WorldTextureImage& base,
    const memory::Allocator<std::uint8_t>& allocator);

// Convert an ordinary single-level RGBA8 staging image in place to expanded
// RGBA4, with nearest quantization and no dither, matching the initial source
// format conversion. Validate before mutation; preserve dimensions/ownership.
void quantize_texture_rgba4(WorldTextureImage& image);
}  // namespace bw::formats
