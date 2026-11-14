// The pre-exposure of a pass that writes light (bw/rendering/postprocess/
// post_shaders.hpp, contract 1): the frame's factor f, read from the 1 by 1
// texel PreExposure.factor names, and the light it writes, multiplied by f
// and bounded by the largest finite 16-bit float.
//
// A shader includes bw/bindless.hlsli and its generated include first. The
// generated include must declare the static block PreExposure at slot 2
// (post_shaders.hpp, kPreExposureSlot), which it does when the shader's
// registry entry lists the block: the block's struct and binding come from
// the C++ declaration, so this file does not restate them.
//
// The writer multiplies by the frame's pre-exposure where scene colour is
// written. The light buffer is Float16 with the exposure pre-applied; its
// largest value is 65504. Only rgb is pre-exposed: the forward pass writes
// opacity in alpha.
#ifndef BW_POSTPROCESS_PRE_EXPOSURE_HLSLI
#define BW_POSTPROCESS_PRE_EXPOSURE_HLSLI

#include "bw/postprocess/scene_color.hlsli"

float bw_pre_exposure() { return BW_TEXTURE2D(PreExposure.factor).Load(int3(0, 0, 0)).r; }

float3 bw_pre_expose(float3 light) { return min(light * bw_pre_exposure(), BW_SCENE_COLOR_MAX); }

#endif
