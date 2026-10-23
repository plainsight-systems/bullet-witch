// Original world-texture pixel interpretation, separate from UI image decoding.
// Inputs borrow encoded PNG and CPR metadata for this call; output owns domain
// bytes. Invalid extents throw malformed; unsupported conversion/filtering
// throws unsupported. No device objects, mip generation, colorspace conversion
// or synchronization. Native metadata is required: the HDR PNG is a byte
// container, not the texture's declared dimensions or ordinary RGBA colors.
// Algorithm: read native format/packed dimensions; decode bounded PNG; require
// exact native source dimensions; for HALF4 restore each source pixel's BGRA
// byte order and reinterpret pairs as little-endian RGBA half words. Return the
// original bytes without floating-point arithmetic. O(encoded + decoded bytes),
// one final pixel owner, plus one face scratch owner for cubes. Cubes use six
// bounded offset/size pairs and retain native +X,-X,+Y,-Y,+Z,-Z face order.
// Optimization: convert channel order in place.
// Source construction evidence: stage-content-decoding.md.
// Design/implementation cpp-guidelines SL.con.3 (#rsl-bounds) and R.2
// (#rr-use-ptr) bound metadata and images; cpp-performance MEM.6
// (guidelines/memory/ MEM.6-inject-allocators-behind-one-interface.md)
// propagates the supplied domain.
#pragma once

#include "bw/formats/image.hpp"

namespace bw::formats {
enum class WorldTextureShape : std::uint8_t { two_dimensional, cube };
enum class WorldTextureFormat : std::uint8_t { rgba8, rgba16_float };
struct WorldTextureImage {
  std::uint32_t width{}, height{};
  WorldTextureFormat format{WorldTextureFormat::rgba8};
  memory::Vector<std::uint8_t> pixels;
  // Cube pixels contain six consecutive square faces in native face order.
  WorldTextureShape shape{WorldTextureShape::two_dimensional};
};
struct WorldTextureSource {
  std::span<const std::uint8_t> encoded, metadata;
  WorldTextureShape shape{WorldTextureShape::two_dimensional};
};
WorldTextureImage decode_world_texture(
    WorldTextureSource source,
    const memory::Allocator<std::uint8_t>& allocator);
}  // namespace bw::formats
