// The content slice's fixture vertex shader: a converted mesh drawn with
// pulled vertices and no input layout (docs/architecture/shaders.md,
// invariant 5), shaded by one base color texture in textured_color.hlsl.
// game_host draws a converted asset with this pair, and the content
// integration test draws one group offscreen with it.
//
// Registry entry (src/engine/rendering/shaders/permutation.cpp,
// TexturedMesh): name textured_mesh, entry point main, vertex stage, an
// empty permutation domain, no static block, per-draw block TexturedMeshDraw
// (textured_mesh_fixture.hpp: Float4x4 world_to_clip, the VertexStreams
// block, the base color texture index, the sampler index, two reserved
// words). The generated include declares BW_VertexStreams and
// BW_TexturedMeshDraw and binds the block through BW_DRAW_BLOCK.
//
// The vertex read is SV_VertexID + streams.base_vertex, as in
// pulled_mesh.hlsl (vertex_streams.hpp, "Vertex index"). Position, normal
// and texture coordinate 1 are read through the attribute table, one
// element type per attribute: position float3, normal short4n, texture
// coordinate 1 half2 (the shape of every PW0 group). A group whose layout
// lacks texture coordinate 1 or carries another type for any of the three
// is not drawn by this pair; a caller checks that before the draw and fails
// by name (textured_mesh_fixture.hpp, Shaders). The position is
// transformed as a row vector, mul(float4(p, 1), world_to_clip), with -Zpr
// making the HLSL rows the C++ rows (parameter_block.hpp, Float4x4).
//
// Shading, the header's formula:
//   shade = 0.25 + 0.75 * saturate(dot(normal.xyz, normalize(float3(0.4, 0.8, 0.45))))
// with the normal in object space as decoded; the pixel shader multiplies
// the sampled base color by it.
//
// Reflection on every target: the draw block and the ByteAddressBuffer
// array; the texture and sampler arrays are the pixel shader's.
#include "bw/bindless.hlsli"
#include "bw/generated/textured_mesh.hlsli"
#include "bw/vertex_fetch.hlsli"

struct TexturedMeshOutput {
  float4 position : SV_Position;
  float2 uv : TEXCOORD0;
  float shade : TEXCOORD1;
};

// The stream an attribute word names.
BwVertexStream textured_mesh_stream(BW_VertexStreams streams, uint word) {
  return BW_VERTEX_STREAM(streams, bw_attribute_stream(word));
}

TexturedMeshOutput main(uint vertex_id : SV_VertexID) {
  const BW_VertexStreams streams = TexturedMeshDraw.streams;
  const uint vertex = vertex_id + streams.base_vertex;

  const uint position_word = BW_ATTRIBUTE_WORD(streams, BW_ATTRIBUTE_POSITION);
  const uint normal_word = BW_ATTRIBUTE_WORD(streams, BW_ATTRIBUTE_NORMAL);
  const uint uv_word = BW_ATTRIBUTE_WORD(streams, BW_ATTRIBUTE_TEXCOORD1);
  const float3 position = bw_load_float3(textured_mesh_stream(streams, position_word), vertex,
                                         bw_attribute_offset(position_word));
  const float4 normal = bw_load_short4n(textured_mesh_stream(streams, normal_word), vertex,
                                        bw_attribute_offset(normal_word));
  const float2 uv =
      bw_load_half2(textured_mesh_stream(streams, uv_word), vertex, bw_attribute_offset(uv_word));

  TexturedMeshOutput output;
  output.position = mul(float4(position, 1.0), TexturedMeshDraw.world_to_clip);
  output.uv = uv;
  output.shade = 0.25 + 0.75 * saturate(dot(normal.xyz, normalize(float3(0.4, 0.8, 0.45))));
  return output;
}
