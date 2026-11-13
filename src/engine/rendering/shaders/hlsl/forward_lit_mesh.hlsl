// The forward pass's lit vertex shader (bw/rendering/scene/forward_pass.hpp,
// contracts 1, 2 and 4): a mesh drawn from records with pulled vertices and
// no input layout (docs/architecture/shaders.md, invariant 5), its
// attributes moved to world space for forward_lit.hlsl.
//
// Registry entry (src/engine/rendering/shaders/permutation.cpp,
// ForwardLitMesh): name forward_lit_mesh, entry point main, vertex stage,
// the lit template's domain and predicate (lit_template.hpp), static blocks
// View and FixtureLight, per-draw block RecordDraw, buffer blocks
// DrawRecord and InstanceRecord; it reads no material.
//
// The draw's record and the instance it names are bw/scene/record_draw.hlsli's;
// the streams and object_to_world are the instance's. The vertex is
// SV_VertexID + streams.base_vertex (vertex_streams.hpp). It reads exactly
// the attributes of lit_vertex_attributes for its permutation: position
// float3 and normal short4n always, tangent short4n with BW_NORMAL_MAP,
// texture coordinate set 0 half2 with BW_BASE_COLOR_MAP or BW_NORMAL_MAP.
// The world and clip positions are bw_record_position's, the expression
// the depth vertex shader uses (depth_shaders.hpp, contract 2); the normal
// and the tangent's xyz go through object_to_world's upper 3x3 and leave
// unit length, tangent.w passes through. The instance index leaves for the
// pixel stage unchanged.
#include "bw/bindless.hlsli"
#include "bw/generated/forward_lit_mesh.hlsli"
#include "bw/vertex_fetch.hlsli"
#include "bw/forward_lit.hlsli"
#include "bw/scene/record_draw.hlsli"

BwForwardLitVaryings main(uint vertex_id : SV_VertexID) {
  const BW_DrawRecord record = bw_record_draw();
  const BW_InstanceRecord instance = bw_record_instance(record.instance);
  const uint vertex = bw_record_vertex(instance, vertex_id);
  const float3x3 rotation = (float3x3)instance.object_to_world;

  const BwRecordPosition position =
      bw_record_position(instance, bw_record_object_position(instance, vertex));
  const uint normal_word = BW_ATTRIBUTE_WORD(instance.streams, BW_ATTRIBUTE_NORMAL);
  const float4 normal = bw_load_short4n(bw_record_stream(instance, normal_word), vertex,
                                        bw_attribute_offset(normal_word));

  BwForwardLitVaryings output;
  output.position = position.clip;
  output.world_position = position.world.xyz;
  output.normal = normalize(mul(normal.xyz, rotation));
#if BW_NORMAL_MAP
  const uint tangent_word = BW_ATTRIBUTE_WORD(instance.streams, BW_ATTRIBUTE_TANGENT);
  const float4 tangent = bw_load_short4n(bw_record_stream(instance, tangent_word), vertex,
                                         bw_attribute_offset(tangent_word));
  output.tangent = float4(normalize(mul(tangent.xyz, rotation)), tangent.w);
#endif
#if BW_BASE_COLOR_MAP || BW_NORMAL_MAP
  const uint uv_word = BW_ATTRIBUTE_WORD(instance.streams, BW_ATTRIBUTE_TEXCOORD0);
  output.uv0 =
      bw_load_half2(bw_record_stream(instance, uv_word), vertex, bw_attribute_offset(uv_word));
#endif
  output.instance = record.instance;
  return output;
}
