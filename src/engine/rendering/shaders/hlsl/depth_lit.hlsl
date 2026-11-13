// The lit template's depth pixel shader (bw/rendering/scene/
// depth_shaders.hpp, contracts 1 and 3), built for the masked permutations
// only: it discards the pixel when the lit template's opacity mask for it
// is below 0 and writes nothing but depth. It serves depth_lit_mesh.hlsl.
//
// Registry entry (src/engine/rendering/shaders/permutation.cpp,
// DepthLit): name depth_lit, entry point main, pixel stage, the lit
// template's domain, admitted and masked permutations only, static block
// View, per-draw block RecordDraw, buffer blocks InstanceRecord and
// LitMaterial.
//
// The instance is the InstanceRecord at the index the vertex stage passed,
// the material the LitMaterial at the instance's material. The opacity mask
// is bw_lit_opacity_mask of bw_lit_base at texture coordinate set 0, the
// two functions bw_lit_attributes takes it from in the forward pass, so the
// two passes keep the same pixels. A pixel stage has no draw index
// (bw/bindless.hlsli, BW_DRAW_INDEX), so it reads its instance at the
// index the vertex stage passes.
// The level transition keep rule is not applied: every converted mesh has one level.
#include "bw/bindless.hlsli"
#include "bw/generated/depth_lit.hlsli"
#include "bw/depth_lit.hlsli"
#include "bw/materials/attributes.hlsli"
#include "bw/materials/lit.hlsli"
#include "bw/scene/records.hlsli"

#if !BW_MASKED
#error "depth_lit is built for the masked permutations only"
#endif

void main(BwDepthLitVaryings input) {
  const BW_InstanceRecord instance = bw_load_InstanceRecord(
      RecordDraw.instances, input.instance * BW_INSTANCE_RECORD_BYTES);
  const BW_LitMaterial material =
      bw_load_LitMaterial(instance.material.buffer, instance.material.offset);
  if (bw_lit_opacity_mask(material, bw_lit_base(material, input.uv0)) < 0.0) {
    discard;
  }
}
