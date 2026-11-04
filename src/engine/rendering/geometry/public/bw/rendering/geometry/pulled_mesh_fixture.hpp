#ifndef BW_RENDERING_GEOMETRY_PULLED_MESH_FIXTURE_HPP
#define BW_RENDERING_GEOMETRY_PULLED_MESH_FIXTURE_HPP
#include "bw/rendering/geometry/vertex_streams.hpp"
#include "bw/rendering/shaders/parameter_block.hpp"

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

// Responsibility: the presentation slice's fixture: the draw blocks of the
// three fixture shaders and a generated cube with the five element types
// across two streams. game_host draws the cube until the importer supplies
// content, and the RHI integration tests draw the cube and the decode probe
// offscreen. The shader registry (src/engine/rendering/shaders/permutation.cpp)
// names these blocks for the fixture entries, so the shader module includes
// this header: a dependency from the shader system to a rendering module
// that exists for the fixture shaders only and leaves with them.
//
// Contract.
//   Shaders: kPulledMeshShader (vertex, entry main, no permutation
//   dimension, draw block PulledMeshDraw), kVertexDecodeProbeShader (vertex,
//   entry main, one enumeration dimension BW_DECODE_TYPE over
//   VertexElementType in declaration order, draw block
//   VertexDecodeProbeDraw), kVertexColorShader (pixel, entry main, no
//   block): it writes the interpolated COLOR0 to target 0 and serves both
//   vertex shaders.
//   PulledMeshDraw: world_to_clip (row-major, parameter_block.hpp Float4x4)
//   then the VertexStreams block. The cube shader reads position, normal
//   and color 0; clip position is the row vector (p, 1) times world_to_clip;
//   COLOR0 = float4(color.rgb * shade, 1) with shade = 0.25 + 0.75 *
//   saturate(dot(normal.xyz, normalize(float3(0.4, 0.8, 0.45)))), the normal
//   in object space. A reference renderer uses exactly these.
//   VertexDecodeProbeDraw: the VertexStreams block, the attribute slot to
//   decode, and the target width; the probe draws 6 * width vertices as a
//   triangle list, two triangles per probe vertex p covering pixel column p
//   of a one-row target, and writes COLOR0 = the decoded value of vertex
//   p + base_vertex as float4 (a half2 in xy, a ubyte4 as float(uint));
//   the lanes the element type does not fill are 0.
//   The cube: 24 vertices, 36 16-bit indices, 12 triangles wound
//   counter-clockwise seen from outside, unit half-extent, centered at the
//   origin. Stream 1 holds position float3 at stride 12 (the content's
//   two-stream declarations); stream 0 holds normal short4n at 0, texture
//   coordinate 1 half2 at 8, color 0 ubyte4n at 12, bone slots ubyte4 at
//   16, bone weights ubyte4n at 20, stride 24. The normals are the face
//   normals with w = 1 (32767), as the content stores them; colors are one
//   per face.
//
// Core guidelines read: I.1 (the fixture is data a caller receives, not a
// global), R.1 (vectors own the bytes). Performance guidelines read: CDSA.32
// (the cube is built once in the layout the shader reads).
namespace bw::geometry {

inline constexpr std::string_view kPulledMeshShader = "pulled_mesh";
inline constexpr std::string_view kVertexDecodeProbeShader = "vertex_decode_probe";
inline constexpr std::string_view kVertexColorShader = "vertex_color";
inline constexpr std::string_view kDecodeTypeDefine = "BW_DECODE_TYPE";

// Offsets: world_to_clip 0, streams 64; size 144.
struct alignas(shaders::kBlockAlignment) PulledMeshDraw {
  shaders::Float4x4 world_to_clip{};
  VertexStreams streams{};
};

// Offsets: streams 0, attribute 80, width 84, reserved0 88, reserved1 92;
// size 96.
struct alignas(shaders::kBlockAlignment) VertexDecodeProbeDraw {
  VertexStreams streams{};
  std::uint32_t attribute{};
  std::uint32_t width{};
  std::uint32_t reserved0{};
  std::uint32_t reserved1{};
};

struct FixtureCube {
  std::vector<std::byte> stream0{};
  std::vector<std::byte> stream1{};
  std::vector<std::uint16_t> indices{};
  VertexStreamLayout layout{};
  std::uint32_t vertex_count{};
  std::uint32_t index_count{};
};
inline constexpr std::uint32_t kFixtureCubeVertices = 24;
inline constexpr std::uint32_t kFixtureCubeIndices = 36;

// The cube above; its layout validates and its buffers hold exactly
// vertex_count vertices at the layout's strides.
[[nodiscard]] FixtureCube make_fixture_cube();

} // namespace bw::geometry

using PulledMeshDraw = bw::geometry::PulledMeshDraw;
BW_PARAMETER_BLOCK_LAYOUT(PulledMeshDraw, BW_PARAMETER_MEMBER(PulledMeshDraw, world_to_clip),
                          BW_PARAMETER_MEMBER(PulledMeshDraw, streams));
using VertexDecodeProbeDraw = bw::geometry::VertexDecodeProbeDraw;
BW_PARAMETER_BLOCK_LAYOUT(VertexDecodeProbeDraw,
                          BW_PARAMETER_MEMBER(VertexDecodeProbeDraw, streams),
                          BW_PARAMETER_MEMBER(VertexDecodeProbeDraw, attribute),
                          BW_PARAMETER_MEMBER(VertexDecodeProbeDraw, width),
                          BW_PARAMETER_MEMBER(VertexDecodeProbeDraw, reserved0),
                          BW_PARAMETER_MEMBER(VertexDecodeProbeDraw, reserved1));
#endif
