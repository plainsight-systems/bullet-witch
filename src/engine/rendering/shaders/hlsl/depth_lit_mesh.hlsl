// The lit template's depth vertex shader (bw/rendering/scene/
// depth_shaders.hpp, contracts 1 and 2): the clip position of a vertex
// drawn from records, for the depth pass.
//
// Registry entry (src/engine/rendering/shaders/permutation.cpp,
// DepthLitMesh): name depth_lit_mesh, entry point main, vertex stage, the
// lit template's domain and predicate (lit_template.hpp), static block
// View, per-draw block RecordDraw, buffer blocks DrawRecord and
// InstanceRecord; it reads no material.
//
// The draw's record, the instance, the vertex, the position and the clip
// position are bw/scene/record_draw.hlsli's, the functions the forward lit
// vertex shader calls, so the two passes give a pixel the same depth.
// Masked permutations also pass texture coordinate set 0, half2
// (lit_template.hpp, kLitTexCoordType; masked requires base_color_map, so
// every masked permutation reads it), and the instance index.
#include "bw/bindless.hlsli"
#include "bw/generated/depth_lit_mesh.hlsli"
#include "bw/vertex_fetch.hlsli"
#include "bw/depth_lit.hlsli"
#include "bw/scene/record_draw.hlsli"

BwDepthLitVaryings main(uint vertex_id : SV_VertexID) {
  const BW_DrawRecord record = bw_record_draw();
  const BW_InstanceRecord instance = bw_record_instance(record.instance);
  const uint vertex = bw_record_vertex(instance, vertex_id);
  const BwRecordPosition position =
      bw_record_position(instance, bw_record_object_position(instance, vertex));

  BwDepthLitVaryings output;
  output.position = position.clip;
#if BW_MASKED
  const uint uv_word = BW_ATTRIBUTE_WORD(instance.streams, BW_ATTRIBUTE_TEXCOORD0);
  output.uv0 =
      bw_load_half2(bw_record_stream(instance, uv_word), vertex, bw_attribute_offset(uv_word));
  output.instance = record.instance;
#endif
  return output;
}
