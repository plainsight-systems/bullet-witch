// The presentation slice's fixture vertex shader: a mesh drawn with pulled
// vertices and no input layout (docs/architecture/shaders.md, invariant 5).
// game_host draws the fixture cube with it, and the RHI integration test
// draws the cube offscreen against a software reference.
//
// Registry entry (src/engine/rendering/shaders/permutation.cpp, PulledMesh):
// name pulled_mesh, entry point main, vertex stage, an empty permutation
// domain, no static block, per-draw block PulledMeshDraw (pulled_mesh_fixture.hpp:
// Float4x4 world_to_clip, then the VertexStreams block). The generated
// include declares BW_VertexStreams and BW_PulledMeshDraw and binds the
// block through BW_DRAW_BLOCK.
//
// The vertex read is SV_VertexID + streams.base_vertex: every draw passes
// base vertex 0 to the API and carries its base in the block
// (vertex_streams.hpp, "Vertex index"). Position, normal and color 0 are read
// through the attribute table: the word gives the stream and the byte
// offset, and the element type is the one the fixture's layout states
// (float3, short4n, ubyte4n). The position is transformed as a row vector,
// mul(float4(p, 1), world_to_clip): the block stores row r at byte 16 * r and
// the tool compiles with -Zpr, so the HLSL rows are the C++ rows
// (parameter_block.hpp, Float4x4).
//
// Shading, which the cube test's reference repeats exactly:
//   shade = 0.25 + 0.75 * saturate(dot(normal.xyz, normalize(float3(0.4, 0.8, 0.45))))
//   COLOR0 = float4(color0.rgb * shade, 1)
// with the normal in object space as decoded.
//
// Reflection on every target: the draw block and the ByteAddressBuffer
// array; no texture or sampler array.
//
// The content draws one mesh per draw with its transform in the draw block
// and its authored winding; front faces are counter-clockwise on every
// backend (pipeline_states.hpp, CullMode). Indices stay in a hardware index
// buffer and the draw issues exactly its range. This slice has no visibility
// buffer: the pixel shader writes the interpolated color.
#include "bw/bindless.hlsli"
#include "bw/generated/pulled_mesh.hlsli"
#include "bw/vertex_fetch.hlsli"

struct PulledMeshOutput {
  float4 position : SV_Position;
  float4 color : COLOR0;
};

// The stream an attribute word names.
BwVertexStream pulled_mesh_stream(BW_VertexStreams streams, uint word) {
  return BW_VERTEX_STREAM(streams, bw_attribute_stream(word));
}

PulledMeshOutput main(uint vertex_id : SV_VertexID) {
  const BW_VertexStreams streams = PulledMeshDraw.streams;
  const uint vertex = vertex_id + streams.base_vertex;

  const uint position_word = BW_ATTRIBUTE_WORD(streams, BW_ATTRIBUTE_POSITION);
  const uint normal_word = BW_ATTRIBUTE_WORD(streams, BW_ATTRIBUTE_NORMAL);
  const uint color_word = BW_ATTRIBUTE_WORD(streams, BW_ATTRIBUTE_COLOR0);
  const float3 position = bw_load_float3(pulled_mesh_stream(streams, position_word), vertex,
                                         bw_attribute_offset(position_word));
  const float4 normal = bw_load_short4n(pulled_mesh_stream(streams, normal_word), vertex,
                                        bw_attribute_offset(normal_word));
  const float4 color = bw_load_ubyte4n(pulled_mesh_stream(streams, color_word), vertex,
                                       bw_attribute_offset(color_word));

  const float shade =
      0.25 + 0.75 * saturate(dot(normal.xyz, normalize(float3(0.4, 0.8, 0.45))));

  PulledMeshOutput output;
  output.position = mul(float4(position, 1.0), PulledMeshDraw.world_to_clip);
  output.color = float4(color.rgb * shade, 1.0);
  return output;
}
