// The vertex stage of a shader that draws records
// (bw/rendering/scene/draw_records.hpp, contract 2): the draw's record, the
// instance it names, the pulled vertex and its clip position. The forward
// lit vertex shader (forward_lit_mesh.hlsl) and the depth vertex shader
// (depth_lit_mesh.hlsl) both take their clip position from
// bw_record_position, so the depth pass and the forward pass give a
// pixel the same depth and the forward pass can test depth Equal against
// what the depth pass wrote (bw/rendering/scene/depth_shaders.hpp,
// contract 2).
//
// A shader includes bw/bindless.hlsli, its generated include and
// bw/vertex_fetch.hlsli first. The generated include must declare the per-draw block RecordDraw, the static
// block View and the buffer blocks DrawRecord and InstanceRecord, whose
// loaders bw_load_DrawRecord and bw_load_InstanceRecord this file calls.
// BW_DRAW_INDEX is a vertex-stage input, so only a vertex shader includes
// this file; a pixel stage receives the instance index from it.
//
// The clip position is `precise`: the value and every operation that feeds
// it, the two matrix products from the loaded position on, are compiled without reassociation or fused multiply-add contraction, so two
// shaders that call this function compute the same bits whatever else
// each shader computes. Without it each shader is optimized on its own and
// the two may round differently.
#ifndef BW_SCENE_RECORD_DRAW_HLSLI
#define BW_SCENE_RECORD_DRAW_HLSLI

#include "bw/scene/records.hlsli"

// The draw's record: the DrawRecord at RecordDraw.list_offset + the draw's
// index * kDrawRecordBytes in the record buffer. A draw the CPU issues
// binds a list of one record and reads index 0 (draw_records.hpp,
// contract 2).
BW_DrawRecord bw_record_draw() {
  return bw_load_DrawRecord(RecordDraw.records,
                            RecordDraw.list_offset + BW_DRAW_INDEX * BW_DRAW_RECORD_BYTES);
}

// The InstanceRecord at `instance` in the instance buffer (instance.hpp,
// contract 1: the index is the record's position).
BW_InstanceRecord bw_record_instance(uint instance) {
  return bw_load_InstanceRecord(RecordDraw.instances, instance * BW_INSTANCE_RECORD_BYTES);
}

// The vertex a draw reads: SV_VertexID + the instance's base vertex
// (vertex_streams.hpp).
uint bw_record_vertex(BW_InstanceRecord instance, uint vertex_id) {
  return vertex_id + instance.streams.base_vertex;
}

// The stream an attribute word of the instance's streams names.
BwVertexStream bw_record_stream(BW_InstanceRecord instance, uint word) {
  return BW_VERTEX_STREAM(instance.streams, bw_attribute_stream(word));
}

// The vertex's position, float3 (lit_template.hpp, kLitPositionType).
float3 bw_record_object_position(BW_InstanceRecord instance, uint vertex) {
  const uint word = BW_ATTRIBUTE_WORD(instance.streams, BW_ATTRIBUTE_POSITION);
  return bw_load_float3(bw_record_stream(instance, word), vertex, bw_attribute_offset(word));
}

// A vertex's world and clip positions.
struct BwRecordPosition {
  float4 world;
  float4 clip;
};

// The world position is the position as a row vector times
// object_to_world; the clip position is that times View.world_to_clip
// (forward_pass.hpp, contract 2; depth_shaders.hpp, contract 2). Both are
// one precise chain from the object position, so every shader that calls
// this function computes the same clip position.
// The record's bone transform is not applied after object_to_world
// (forward_pass.hpp, contract 4; scene.md, "The instance"): bone_first is
// kNone in every record until the geometry feed writes one (instance.hpp,
// contract 2), and a bone is not applied while it is.
BwRecordPosition bw_record_position(BW_InstanceRecord instance, float3 position) {
  precise const float4 world = mul(float4(position, 1.0), instance.object_to_world);
  precise const float4 clip = mul(world, View.world_to_clip);
  BwRecordPosition result;
  result.world = world;
  result.clip = clip;
  return result;
}

#endif
