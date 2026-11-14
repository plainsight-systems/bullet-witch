// The post-processing module's vertex shader
// (bw/rendering/postprocess/post_shaders.hpp, contract 2): one triangle from
// SV_VertexID that covers the target, with no vertex buffer and no block.
// Every raster pass of the module draws it with three vertices.
//
// Registry entry (src/engine/rendering/shaders/permutation.cpp,
// PostFullscreenShader): vertex, entry main, no permutation, no block.
//
// Vertices 0, 1 and 2 have uv (0, 0), (2, 0) and (0, 2), so the triangle's
// clip extent covers -1 to 1 on both axes, uv (0, 0) at the top left corner
// of the target and (1, 1) at its bottom right.
#include "bw/bindless.hlsli"
#include "bw/generated/post_fullscreen.hlsli"
#include "bw/postprocess/fullscreen.hlsli"

BwFullscreenVaryings main(uint vertex_id : SV_VertexID) {
  const float2 uv = float2((vertex_id << 1) & 2, vertex_id & 2);
  BwFullscreenVaryings output;
  output.position = float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
  output.uv = uv;
  return output;
}
