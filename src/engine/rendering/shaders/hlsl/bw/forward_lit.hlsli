// What the forward pass's lit vertex shader (forward_lit_mesh.hlsl) hands
// its pixel shader (forward_lit.hlsl), in world space. Both stages are
// compiled with the same permutation defines, so the struct is the same on
// both sides and they link by semantic on every target; a member exists
// only when the permutation reads the attribute it carries
// (bw/rendering/materials/lit_template.hpp, contract 2).
//
// instance is the index of the instance record the draw names, constant
// over a triangle (bw/rendering/scene/forward_pass.hpp, contract 4): the
// pixel stage cannot read the draw's index (bw/bindless.hlsli,
// BW_DRAW_INDEX), so it finds the instance's material through this.
#ifndef BW_FORWARD_LIT_HLSLI
#define BW_FORWARD_LIT_HLSLI

struct BwForwardLitVaryings {
  precise float4 position : SV_Position;
  float3 world_position : TEXCOORD0;
  float3 normal : TEXCOORD1;
#if BW_NORMAL_MAP
  float4 tangent : TEXCOORD2;
#endif
#if BW_BASE_COLOR_MAP || BW_NORMAL_MAP
  float2 uv0 : TEXCOORD3;
#endif
  nointerpolation uint instance : TEXCOORD4;
};

#endif
