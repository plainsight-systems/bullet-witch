// What the module's vertex shader (post_fullscreen.hlsl) passes to every
// pixel shader of the module (bw/rendering/postprocess/post_shaders.hpp,
// contract 2): the position and uv, (0, 0) at the target's top left texel
// corner and (1, 1) at its bottom right, so a pixel receives the uv of its
// centre. The pixel shaders declare the same struct, so the stages link by
// semantic on every target.
#ifndef BW_POSTPROCESS_FULLSCREEN_HLSLI
#define BW_POSTPROCESS_FULLSCREEN_HLSLI

struct BwFullscreenVaryings {
  float4 position : SV_Position;
  float2 uv : TEXCOORD0;
};

#endif
