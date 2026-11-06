// The content slice's fixture pixel shader: the base color texture sampled
// at the interpolated texture coordinate, times the vertex shader's shade,
// written opaque to render target 0. It serves textured_mesh.hlsl.
//
// Registry entry (src/engine/rendering/shaders/permutation.cpp,
// TexturedColor): name textured_color, entry point main, pixel stage, an
// empty permutation domain, no static block, per-draw block
// TexturedMeshDraw, the vertex shader's block: this stage reads base_color
// and sampler from it (textured_mesh_fixture.hpp).
//
// The input struct repeats textured_mesh.hlsl's output in the same order
// (SV_Position, TEXCOORD0, TEXCOORD1), so the stages link by semantic on
// every target.
//
// Reflection on every target: the draw block, the Texture2D array and the
// sampler array.
#include "bw/bindless.hlsli"
#include "bw/generated/textured_color.hlsli"

struct TexturedColorInput {
  float4 position : SV_Position;
  float2 uv : TEXCOORD0;
  float shade : TEXCOORD1;
};

float4 main(TexturedColorInput input) : SV_Target0 {
  const float4 base = BW_TEXTURE2D(TexturedMeshDraw.base_color)
                          .Sample(BW_SAMPLER(TexturedMeshDraw.sampler), input.uv);
  return float4(base.rgb * input.shade, 1.0);
}
