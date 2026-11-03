// The engine's first pixel shader: it writes the interpolated COLOR0 of the
// fullscreen triangle vertex shader to render target 0. The RHI slice's
// integration tests pair it with fullscreen_triangle.hlsl to build a graphics
// pipeline, draw into an offscreen target and read the pixels back.
//
// Registry entry (src/engine/rendering/shaders/permutation.cpp,
// FullscreenTriangleColor): name fullscreen_triangle_color, entry point main,
// pixel stage, an empty permutation domain, no static block, no per-draw
// block. It reads no descriptor array and no block, so its reflection names
// no resource on any target.
//
// The input struct repeats fullscreen_triangle.hlsl's output in the same order
// (SV_Position, COLOR0, TEXCOORD0), so the stages link by semantic on every
// target.
#include "bw/bindless.hlsli"
#include "bw/generated/fullscreen_triangle_color.hlsli"

struct FullscreenTriangleInput {
  float4 position : SV_Position;
  float4 color : COLOR0;
  float2 uv : TEXCOORD0;
};

float4 main(FullscreenTriangleInput input) : SV_Target0 { return input.color; }
