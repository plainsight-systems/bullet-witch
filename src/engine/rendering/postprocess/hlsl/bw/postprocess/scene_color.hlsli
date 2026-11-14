// Scene colour's bound (bw/rendering/postprocess/post_shaders.hpp,
// contract 1): the largest finite 16-bit float. A pass that writes light
// writes no more (pre_exposure.hlsli), and since a blend that adds to the
// texel already there can pass it, each reader of scene colour in the
// module takes min(texel, BW_SCENE_COLOR_MAX), so no infinite value
// reaches the meter, the bloom or the display. The constant alone: the
// pre-exposure include supplies its two functions and nothing else.
#ifndef BW_POSTPROCESS_SCENE_COLOR_HLSLI
#define BW_POSTPROCESS_SCENE_COLOR_HLSLI

// post_shaders.hpp, kSceneColorMax.
static const float BW_SCENE_COLOR_MAX = 65504.0;

#endif
