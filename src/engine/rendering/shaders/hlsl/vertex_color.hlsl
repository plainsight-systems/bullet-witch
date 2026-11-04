// The presentation slice's fixture pixel shader: it writes the interpolated
// COLOR0 to render target 0 and serves both fixture vertex shaders,
// pulled_mesh.hlsl and vertex_decode_probe.hlsl.
//
// Registry entry (src/engine/rendering/shaders/permutation.cpp, VertexColor):
// name vertex_color, entry point main, pixel stage, an empty permutation
// domain, no static block, no per-draw block. It reads no descriptor array
// and no block, so its reflection names no resource on any target.
//
// The input struct repeats the vertex shaders' output in the same order
// (SV_Position, COLOR0), so the stages link by semantic on every target.
#include "bw/bindless.hlsli"
#include "bw/generated/vertex_color.hlsli"

struct VertexColorInput {
  float4 position : SV_Position;
  float4 color : COLOR0;
};

float4 main(VertexColorInput input) : SV_Target0 { return input.color; }
