#ifndef BW_RENDERING_GEOMETRY_TEXTURED_MESH_FIXTURE_HPP
#define BW_RENDERING_GEOMETRY_TEXTURED_MESH_FIXTURE_HPP
#include "bw/rendering/geometry/vertex_streams.hpp"
#include "bw/rendering/shaders/parameter_block.hpp"

#include <cstdint>
#include <string_view>

// Responsibility: the content slice's fixture shaders: a pulled-vertex mesh
// shaded by one base color texture, the first draw of converted content
// (docs/architecture/content.md). The pair, its draw block and the layout
// it reads are stated here; the draw of a loaded model through it is
// bw/rendering/textured_model_fixture.hpp. Like pulled_mesh_fixture.hpp it
// leaves with the renderer's material templates.
//
// Contract.
//   Shaders: kTexturedMeshShader (vertex, entry main, no permutation
//   dimension, draw block TexturedMeshDraw) reads position, normal and
//   texture coordinate 1 through the VertexStreams block, writes clip
//   position (row vector times world_to_clip), the texture coordinate and
//   shade = 0.25 + 0.75 * saturate(dot(normal.xyz, normalize(float3(0.4,
//   0.8, 0.45)))); kTexturedColorShader (pixel, entry main, the same draw
//   block) writes float4(base.rgb * shade, 1) with base =
//   BW_TEXTURE2D(base_color).Sample(BW_SAMPLER(sampler), uv). The pair
//   decodes one element type per attribute: position float3, normal
//   short4n, texture coordinate 1 half2 (the shape of every PW0 group).
//   textured_pair_reads(layout) is true exactly when the layout carries
//   all three attributes with those types, whatever else it carries; a
//   group for which it is false is not drawn by this pair.
//   TexturedMeshDraw: world_to_clip at 0, the VertexStreams block at 64,
//   base_color at 144, sampler at 148, two reserved words; size 160.
//
// Core guidelines read: I.1 (the block carries every input of the draw).
// Performance guidelines read: GDSA.12 (texture and sampler by direct index).
namespace bw::geometry {

inline constexpr std::string_view kTexturedMeshShader = "textured_mesh";
inline constexpr std::string_view kTexturedColorShader = "textured_color";

[[nodiscard]] bool textured_pair_reads(const VertexStreamLayout& layout) noexcept;

struct alignas(shaders::kBlockAlignment) TexturedMeshDraw {
  shaders::Float4x4 world_to_clip{};
  VertexStreams streams{};
  shaders::TextureIndex base_color{};
  shaders::SamplerIndex sampler{};
  std::uint32_t reserved0{};
  std::uint32_t reserved1{};
};
inline constexpr std::uint32_t kTexturedMeshDrawBytes = 160;
static_assert(sizeof(TexturedMeshDraw) == kTexturedMeshDrawBytes);

} // namespace bw::geometry

using TexturedMeshDraw = bw::geometry::TexturedMeshDraw;
BW_PARAMETER_BLOCK_LAYOUT(TexturedMeshDraw, BW_PARAMETER_MEMBER(TexturedMeshDraw, world_to_clip),
                          BW_PARAMETER_MEMBER(TexturedMeshDraw, streams),
                          BW_PARAMETER_MEMBER(TexturedMeshDraw, base_color),
                          BW_PARAMETER_MEMBER(TexturedMeshDraw, sampler),
                          BW_PARAMETER_MEMBER(TexturedMeshDraw, reserved0),
                          BW_PARAMETER_MEMBER(TexturedMeshDraw, reserved1));
#endif
