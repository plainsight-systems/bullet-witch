// The forward pass's lit pixel shader (bw/rendering/scene/forward_pass.hpp,
// contracts 1, 3 and 4): the lit template's attributes for the pixel
// (bw/materials/lit.hlsli), shaded by the lit shading model under the
// fixture light (bw/scene/lit_shading.hlsli). It serves forward_lit_mesh.hlsl.
//
// Registry entry (src/engine/rendering/shaders/permutation.cpp, ForwardLit):
// name forward_lit, entry point main, pixel stage, the lit template's domain
// and predicate, static blocks View and FixtureLight, per-draw block
// RecordDraw, buffer blocks InstanceRecord and LitMaterial.
//
// The instance is the InstanceRecord at the index the vertex stage passed
// (forward_pass.hpp, contract 4), loaded from RecordDraw.instances; the
// material is the LitMaterial at the instance's material. A pixel stage has
// no draw index (bw/bindless.hlsli, BW_DRAW_INDEX), so it reads its
// instance at the index the vertex stage passes. V, the unit vector from the
// world position to View.eye_position, is computed once and given to both
// the template and the shading model. With BW_MASKED the pixel is
// discarded when the opacity mask is below 0. The target receives
// float4(color, opacity), linear; nothing is tone mapped.
// The level transition keep rule is not applied: every converted mesh has one level.
#include "bw/bindless.hlsli"
#include "bw/generated/forward_lit.hlsli"
#include "bw/forward_lit.hlsli"
#include "bw/materials/attributes.hlsli"
#include "bw/materials/lit.hlsli"
#include "bw/scene/lit_shading.hlsli"
#include "bw/scene/records.hlsli"

float4 main(BwForwardLitVaryings input) : SV_Target0 {
  const BW_InstanceRecord instance = bw_load_InstanceRecord(
      RecordDraw.instances, input.instance * BW_INSTANCE_RECORD_BYTES);
  const BW_LitMaterial material =
      bw_load_LitMaterial(instance.material.buffer, instance.material.offset);
  const float3 view = normalize(View.eye_position - input.world_position);
  BwLitPixel pixel;
  pixel.normal = input.normal;
  pixel.view = view;
#if BW_NORMAL_MAP
  pixel.tangent = input.tangent;
#endif
#if BW_BASE_COLOR_MAP || BW_NORMAL_MAP
  pixel.uv0 = input.uv0;
#endif
  const BwMaterialAttributes attributes = bw_lit_attributes(material, pixel);
#if BW_MASKED
  if (attributes.opacity_mask < 0.0) {
    discard;
  }
#endif
  const float3 color = bw_lit_shade(attributes, view, FixtureLight.direction, FixtureLight.color,
                                    FixtureLight.ambient);
  return float4(color, attributes.opacity);
}
