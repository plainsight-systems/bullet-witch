// The forward pass's lit vertex shader (bw/rendering/scene/forward_pass.hpp,
// contracts 1 and 2): a mesh drawn with pulled vertices and no input layout
// (docs/architecture/shaders.md, invariant 5), its attributes moved to world
// space for forward_lit.hlsl.
//
// Registry entry (src/engine/rendering/shaders/permutation.cpp,
// ForwardLitMesh): name forward_lit_mesh, entry point main, vertex stage,
// the lit template's domain and predicate (lit_template.hpp), static blocks
// View and FixtureLight, per-draw block ForwardDraw; it reads no material and
// declares no buffer block.
//
// The vertex is SV_VertexID + streams.base_vertex (vertex_streams.hpp). It
// reads exactly the attributes of lit_vertex_attributes for its
// permutation: position float3 and normal short4n always, tangent short4n
// with BW_NORMAL_MAP, texture coordinate set 0 half2 with BW_BASE_COLOR_MAP or
// BW_NORMAL_MAP. The world position is mul(float4(p, 1), object_to_world)
// (row vectors, -Zpr: parameter_block.hpp, Float4x4), the clip position
// that times View.world_to_clip; the normal and the tangent's xyz go
// through object_to_world's upper 3x3 and leave unit length, tangent.w
// passes through.
#include "bw/bindless.hlsli"
#include "bw/generated/forward_lit_mesh.hlsli"
#include "bw/vertex_fetch.hlsli"
#include "bw/forward_lit.hlsli"

// The stream an attribute word names.
BwVertexStream forward_lit_stream(BW_VertexStreams streams, uint word) {
  return BW_VERTEX_STREAM(streams, bw_attribute_stream(word));
}

BwForwardLitVaryings main(uint vertex_id : SV_VertexID) {
  const BW_VertexStreams streams = ForwardDraw.streams;
  const uint vertex = vertex_id + streams.base_vertex;
  const float4x4 object_to_world = ForwardDraw.object_to_world;
  const float3x3 rotation = (float3x3)object_to_world;

  const uint position_word = BW_ATTRIBUTE_WORD(streams, BW_ATTRIBUTE_POSITION);
  const uint normal_word = BW_ATTRIBUTE_WORD(streams, BW_ATTRIBUTE_NORMAL);
  const float3 position = bw_load_float3(forward_lit_stream(streams, position_word), vertex,
                                         bw_attribute_offset(position_word));
  const float4 normal = bw_load_short4n(forward_lit_stream(streams, normal_word), vertex,
                                        bw_attribute_offset(normal_word));

  BwForwardLitVaryings output;
  const float4 world = mul(float4(position, 1.0), object_to_world);
  output.position = mul(world, View.world_to_clip);
  output.world_position = world.xyz;
  output.normal = normalize(mul(normal.xyz, rotation));
#if BW_NORMAL_MAP
  const uint tangent_word = BW_ATTRIBUTE_WORD(streams, BW_ATTRIBUTE_TANGENT);
  const float4 tangent = bw_load_short4n(forward_lit_stream(streams, tangent_word), vertex,
                                         bw_attribute_offset(tangent_word));
  output.tangent = float4(normalize(mul(tangent.xyz, rotation)), tangent.w);
#endif
#if BW_BASE_COLOR_MAP || BW_NORMAL_MAP
  const uint uv_word = BW_ATTRIBUTE_WORD(streams, BW_ATTRIBUTE_TEXCOORD0);
  output.uv0 =
      bw_load_half2(forward_lit_stream(streams, uv_word), vertex, bw_attribute_offset(uv_word));
#endif
  return output;
}
