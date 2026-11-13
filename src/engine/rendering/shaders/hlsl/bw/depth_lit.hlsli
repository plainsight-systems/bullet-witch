// What the lit template's depth vertex shader (depth_lit_mesh.hlsl) hands
// its pixel shader (depth_lit.hlsl). Both stages are compiled with the same
// permutation defines, so the struct is the same on both sides. Only the
// masked permutations have a pixel stage (bw/rendering/scene/
// depth_shaders.hpp, contract 1), and only they carry texture coordinate
// set 0 and the instance index the pixel stage finds its material through;
// every other permutation hands on the position alone.
#ifndef BW_DEPTH_LIT_HLSLI
#define BW_DEPTH_LIT_HLSLI

struct BwDepthLitVaryings {
  precise float4 position : SV_Position;
#if BW_MASKED
  float2 uv0 : TEXCOORD0;
  nointerpolation uint instance : TEXCOORD1;
#endif
};

#endif
