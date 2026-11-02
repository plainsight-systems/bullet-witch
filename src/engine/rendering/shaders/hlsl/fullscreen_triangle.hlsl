// The engine's first shader: a vertex shader that covers the viewport with
// one triangle generated from SV_VertexID, with no vertex buffer. The RHI
// slice's first pipeline draws it, and the shader toolchain's integration
// tests compile it on all three targets.
//
// Registry entry (src/engine/rendering/shaders/permutation.cpp,
// FullscreenTriangle): name fullscreen_triangle, entry point main, vertex
// stage, one dimension BW_FLIP_Y (BoolDimension), per-draw block
// FullscreenTriangleDraw { Float4 color; Float2 scale; TextureIndex texture;
// SamplerIndex sampler; }, no static block. The generated include declares that block as
// BW_FullscreenTriangleDraw and binds it through BW_DRAW_BLOCK.
//
// Every member of the draw block is read, and the texture is sampled through
// the descriptor arrays, so the draw block, the Texture2D array and the
// sampler array all appear in reflection on every target. A vertex shader has
// no derivatives, so the sample uses SampleLevel with level 0.
#include "bw/bindless.hlsli"
#include "bw/generated/fullscreen_triangle.hlsli"

struct FullscreenTriangleOutput {
  float4 position : SV_Position;
  float4 color : COLOR0;
  float2 uv : TEXCOORD0;
};

FullscreenTriangleOutput main(uint vertex_id : SV_VertexID) {
  // Vertices 0, 1, 2 map to uv (0, 0), (2, 0), (0, 2): one triangle whose
  // clip-space extent covers [-1, 1] on both axes.
  const float2 uv = float2((vertex_id << 1) & 2, vertex_id & 2);
  float2 clip = uv * float2(2.0, -2.0) + float2(-1.0, 1.0);
#if BW_FLIP_Y
  clip.y = -clip.y;
#endif
  const float4 texel = BW_TEXTURE2D(FullscreenTriangleDraw.texture)
                           .SampleLevel(BW_SAMPLER(FullscreenTriangleDraw.sampler),
                                        uv * FullscreenTriangleDraw.scale, 0.0);

  FullscreenTriangleOutput output;
  output.position = float4(clip, 0.0, 1.0);
  output.color = FullscreenTriangleDraw.color * texel;
  output.uv = uv * FullscreenTriangleDraw.scale;
  return output;
}
