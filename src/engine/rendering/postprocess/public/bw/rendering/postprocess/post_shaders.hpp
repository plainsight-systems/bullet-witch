#ifndef BW_RENDERING_POSTPROCESS_POST_SHADERS_HPP
#define BW_RENDERING_POSTPROCESS_POST_SHADERS_HPP

#include "bw/rendering/shaders/parameter_block.hpp"

#include <cstdint>
#include <string_view>

// Responsibility: the post-processing module's shaders as the engine
// declares them (docs/architecture/post-processing.md, "The chain"): their
// names, their blocks, and the pre-exposure block and include every pass
// that writes light reads (invariant 1). The passes that run them are
// exposure.hpp's, bloom.hpp's, grade_bake.hpp's and tone_mapping.hpp's;
// this header names nothing of the RHI or the render graph, so the shader
// registry, which is compiled at configure time, reads it, and so does
// bw/rendering/scene/forward_pass.hpp.
//
// Contract.
//   1. Pre-exposure. PreExposure is static slot kPreExposureSlot of the
//      engine root layout: `factor` names a 1 by 1 texture whose red
//      channel is the frame's pre-exposure factor f (exposure.hpp,
//      contract 6). The include bw/postprocess/pre_exposure.hlsli, under
//      src/engine/rendering/postprocess/hlsl, supplies two functions over
//      the block and nothing else; the block itself is declared by the
//      generated include of a shader whose registry entry lists it.
//      bw_pre_exposure() is f, loaded from that texel;
//      bw_pre_expose(light) is min(light * f, kSceneColorMax) per channel.
//      A pass that writes light into scene colour, or into any
//      light target of the frame, writes bw_pre_expose of it, in renderer
//      code and never in a material template; a reader divides f out. The
//      bound is on what a pass outputs: a blend that adds to the texel
//      already there can pass it, so each reader of scene colour in this
//      module takes min(texel, kSceneColorMax) and no infinite texel
//      reaches the meter, the bloom or the display. The factor is never a
//      constant in a block: no reader holds a CPU copy.
//   2. Vertex shader. kFullscreenVertexShader (vertex, entry main, no
//      permutation, no block) covers the target with one triangle from
//      SV_VertexID and passes uv, (0, 0) at the target's top left texel
//      corner and (1, 1) at its bottom right. Every raster pass of the
//      module pairs it with one of the pixel shaders below.
//   3. Pixel and compute shaders, each entry main, no permutation, no
//      static block and no buffer block, with the per-draw block named:
//        kHistogramShader        compute  PostHistogram
//        kAdaptationShader       compute  PostAdaptation
//        kBloomDownsampleShader  pixel    PostBloomDownsample
//        kBloomUpsampleShader    pixel    PostBloomUpsample
//        kGradeBakeShader        compute  PostGradeBake
//        kToneMapShader          pixel    PostToneMap
//      Every buffer a kernel reads or writes is raw words whose layout is
//      the implementation's.
//   4. What is promised to a caller is the pass headers' and nothing here
//      adds to it: how many times a pass runs a shader, thread group
//      sizes and the scratch buffers' layouts are the implementation's and
//      no test asserts them. This header states what the shader registry
//      and the code that fills a block must agree on: the names and the
//      blocks' layouts.
//
// The module has its own vertex shader because a pipeline has one per-draw
// block for both stages (bw/rendering/rhi/binding_structure.hpp,
// derive_pipeline_binding) and the fixture vertex shader fullscreen_triangle
// declares a block of its own.
//
// kSceneColorMax bounds what a pass writes: the lit shading's specular
// peak at the minimum roughness can pass the largest 16-bit float, and an
// infinite texel would reach the histogram, the bloom and the display.
//
// Core guidelines read: I.1 (every input of a kernel is in its block),
// Con.5, ES.45 (each constant is named). Performance guidelines read:
// GPU.1 (the factor stays on the device), CACHE.2 (every layout is stated
// and asserted).
namespace bw::postprocess {

inline constexpr std::string_view kFullscreenVertexShader = "post_fullscreen";
inline constexpr std::string_view kHistogramShader = "post_histogram";
inline constexpr std::string_view kAdaptationShader = "post_adaptation";
inline constexpr std::string_view kBloomDownsampleShader = "post_bloom_downsample";
inline constexpr std::string_view kBloomUpsampleShader = "post_bloom_upsample";
inline constexpr std::string_view kGradeBakeShader = "post_grade_bake";
inline constexpr std::string_view kToneMapShader = "post_tone_map";

// The largest finite value of a 16-bit float channel; pre-exposed light is
// written no larger (contract 1).
inline constexpr float kSceneColorMax = 65504.0F;

// Offsets: factor 0, three reserved words; size 16.
struct alignas(shaders::kBlockAlignment) PreExposure {
  shaders::TextureIndex factor{};
  std::uint32_t reserved0{};
  std::uint32_t reserved1{};
  std::uint32_t reserved2{};
};
inline constexpr std::uint32_t kPreExposureBytes = 16;
static_assert(sizeof(PreExposure) == kPreExposureBytes);
inline constexpr std::uint32_t kPreExposureSlot = 2;

// PostHistogram::mode: zero the histogram's words, or add the frame's
// samples to them.
inline constexpr std::uint32_t kHistogramClear = 0;
inline constexpr std::uint32_t kHistogramAccumulate = 1;

// Offsets: scene_color 0, factor 4, histogram 8, mode 12, width 16, height
// 20, log_min 24, log_max 28; size 32. width and height are scene colour's
// size in texels; log_min and log_max are the histogram's range
// (exposure.hpp, kHistogramLogMin and kHistogramLogMax).
struct alignas(shaders::kBlockAlignment) PostHistogram {
  shaders::TextureIndex scene_color{};
  shaders::TextureIndex factor{};
  shaders::StorageBufferIndex histogram{};
  std::uint32_t mode{};
  std::uint32_t width{};
  std::uint32_t height{};
  float log_min{};
  float log_max{};
};
inline constexpr std::uint32_t kPostHistogramBytes = 32;
static_assert(sizeof(PostHistogram) == kPostHistogramBytes);

// Offsets: histogram 0, previous 4, adapted 8, reset 12,
// compensation_stops 16, delta_seconds 20, log_min 24, log_max 28,
// min_ev100 32, max_ev100 36, two reserved words; size 48. previous is the
// texel the frame reads as its factor, adapted the texel the pass writes
// (exposure.hpp, contract 6); min_ev100 and max_ev100 are the range of
// exposure.hpp contract 5.
struct alignas(shaders::kBlockAlignment) PostAdaptation {
  shaders::BufferIndex histogram{};
  shaders::TextureIndex previous{};
  shaders::StorageTextureIndex adapted{};
  std::uint32_t reset{};
  float compensation_stops{};
  float delta_seconds{};
  float log_min{};
  float log_max{};
  float min_ev100{};
  float max_ev100{};
  std::uint32_t reserved0{};
  std::uint32_t reserved1{};
};
inline constexpr std::uint32_t kPostAdaptationBytes = 48;
static_assert(sizeof(PostAdaptation) == kPostAdaptationBytes);

// Offsets: source 0, sampler 4, source_texel 8 (one over the source's
// size), firefly 16 (1 on the first level, else 0), three reserved words;
// size 32.
struct alignas(shaders::kBlockAlignment) PostBloomDownsample {
  shaders::TextureIndex source{};
  shaders::SamplerIndex sampler{};
  shaders::Float2 source_texel{};
  std::uint32_t firefly{};
  std::uint32_t reserved0{};
  std::uint32_t reserved1{};
  std::uint32_t reserved2{};
};
inline constexpr std::uint32_t kPostBloomDownsampleBytes = 32;
static_assert(sizeof(PostBloomDownsample) == kPostBloomDownsampleBytes);

// Offsets: level 0 (the downsampled level of the destination's size),
// below 4 (the sum of the levels below, at half that size), sampler 8,
// level_scale 12 (the factor on `level`), below_texel 16 (one over below's
// size), radius 24, below_scale 28 (the factor on the filtered `below`: 1
// when it is a sum already scaled, the level's sixth when it is the last
// downsampled level itself); size 32.
struct alignas(shaders::kBlockAlignment) PostBloomUpsample {
  shaders::TextureIndex level{};
  shaders::TextureIndex below{};
  shaders::SamplerIndex sampler{};
  float level_scale{};
  shaders::Float2 below_texel{};
  float radius{};
  float below_scale{};
};
inline constexpr std::uint32_t kPostBloomUpsampleBytes = 32;
static_assert(sizeof(PostBloomUpsample) == kPostBloomUpsampleBytes);

// What the bake kernel reads of a grade (grade.hpp), already resolved on
// the CPU: the white balance as the rows of its matrix (grade_bake.hpp,
// white_balance_matrix; apply_white_balance 0 leaves it out), and each
// tonal range's five terms as the range's times the whole range's, the
// offsets added (grade_bake.hpp, contract 5).
// Offsets: table 0, shadows_max 4, highlights_min 8, highlights_max 12,
// slope 16, toe 20, shoulder 24, black_clip 28, white_clip 32,
// apply_white_balance 36, two reserved words, white_balance_row0 48, row1
// 64, row2 80, then per range saturation, contrast, gamma, gain, offset:
// shadows 96 to 160, midtones 176 to 240, highlights 256 to 320; size 336.
struct alignas(shaders::kBlockAlignment) PostGradeBake {
  shaders::StorageBufferIndex table{};
  float shadows_max{};
  float highlights_min{};
  float highlights_max{};
  float slope{};
  float toe{};
  float shoulder{};
  float black_clip{};
  float white_clip{};
  std::uint32_t apply_white_balance{};
  std::uint32_t reserved0{};
  std::uint32_t reserved1{};
  shaders::Float3 white_balance_row0{};
  shaders::Float3 white_balance_row1{};
  shaders::Float3 white_balance_row2{};
  shaders::Float3 shadows_saturation{};
  shaders::Float3 shadows_contrast{};
  shaders::Float3 shadows_gamma{};
  shaders::Float3 shadows_gain{};
  shaders::Float3 shadows_offset{};
  shaders::Float3 midtones_saturation{};
  shaders::Float3 midtones_contrast{};
  shaders::Float3 midtones_gamma{};
  shaders::Float3 midtones_gain{};
  shaders::Float3 midtones_offset{};
  shaders::Float3 highlights_saturation{};
  shaders::Float3 highlights_contrast{};
  shaders::Float3 highlights_gamma{};
  shaders::Float3 highlights_gain{};
  shaders::Float3 highlights_offset{};
};
inline constexpr std::uint32_t kPostGradeBakeBytes = 336;
static_assert(sizeof(PostGradeBake) == kPostGradeBakeBytes);

// Offsets: scene_color 0, bloom 4, factor 8, adapted 12, table 16, sampler
// 20, bloom_intensity 24, vignette 28, target_size 32 (pixels),
// dither_offset 40, dither_amplitude 48, three reserved words; size 64.
struct alignas(shaders::kBlockAlignment) PostToneMap {
  shaders::TextureIndex scene_color{};
  shaders::TextureIndex bloom{};
  shaders::TextureIndex factor{};
  shaders::TextureIndex adapted{};
  shaders::TextureIndex table{};
  shaders::SamplerIndex sampler{};
  float bloom_intensity{};
  float vignette{};
  shaders::Float2 target_size{};
  shaders::Float2 dither_offset{};
  float dither_amplitude{};
  std::uint32_t reserved0{};
  std::uint32_t reserved1{};
  std::uint32_t reserved2{};
};
inline constexpr std::uint32_t kPostToneMapBytes = 64;
static_assert(sizeof(PostToneMap) == kPostToneMapBytes);

} // namespace bw::postprocess

// The block layout macro stringizes its argument as the HLSL struct name,
// so each block is named unqualified at global scope, as every block is.
using PreExposure = bw::postprocess::PreExposure;
BW_PARAMETER_BLOCK_LAYOUT(PreExposure, BW_PARAMETER_MEMBER(PreExposure, factor),
                          BW_PARAMETER_MEMBER(PreExposure, reserved0),
                          BW_PARAMETER_MEMBER(PreExposure, reserved1),
                          BW_PARAMETER_MEMBER(PreExposure, reserved2));
using PostHistogram = bw::postprocess::PostHistogram;
BW_PARAMETER_BLOCK_LAYOUT(PostHistogram, BW_PARAMETER_MEMBER(PostHistogram, scene_color),
                          BW_PARAMETER_MEMBER(PostHistogram, factor),
                          BW_PARAMETER_MEMBER(PostHistogram, histogram),
                          BW_PARAMETER_MEMBER(PostHistogram, mode),
                          BW_PARAMETER_MEMBER(PostHistogram, width),
                          BW_PARAMETER_MEMBER(PostHistogram, height),
                          BW_PARAMETER_MEMBER(PostHistogram, log_min),
                          BW_PARAMETER_MEMBER(PostHistogram, log_max));
using PostAdaptation = bw::postprocess::PostAdaptation;
BW_PARAMETER_BLOCK_LAYOUT(
    PostAdaptation, BW_PARAMETER_MEMBER(PostAdaptation, histogram),
    BW_PARAMETER_MEMBER(PostAdaptation, previous), BW_PARAMETER_MEMBER(PostAdaptation, adapted),
    BW_PARAMETER_MEMBER(PostAdaptation, reset),
    BW_PARAMETER_MEMBER(PostAdaptation, compensation_stops),
    BW_PARAMETER_MEMBER(PostAdaptation, delta_seconds),
    BW_PARAMETER_MEMBER(PostAdaptation, log_min), BW_PARAMETER_MEMBER(PostAdaptation, log_max),
    BW_PARAMETER_MEMBER(PostAdaptation, min_ev100), BW_PARAMETER_MEMBER(PostAdaptation, max_ev100),
    BW_PARAMETER_MEMBER(PostAdaptation, reserved0), BW_PARAMETER_MEMBER(PostAdaptation, reserved1));
using PostBloomDownsample = bw::postprocess::PostBloomDownsample;
BW_PARAMETER_BLOCK_LAYOUT(PostBloomDownsample, BW_PARAMETER_MEMBER(PostBloomDownsample, source),
                          BW_PARAMETER_MEMBER(PostBloomDownsample, sampler),
                          BW_PARAMETER_MEMBER(PostBloomDownsample, source_texel),
                          BW_PARAMETER_MEMBER(PostBloomDownsample, firefly),
                          BW_PARAMETER_MEMBER(PostBloomDownsample, reserved0),
                          BW_PARAMETER_MEMBER(PostBloomDownsample, reserved1),
                          BW_PARAMETER_MEMBER(PostBloomDownsample, reserved2));
using PostBloomUpsample = bw::postprocess::PostBloomUpsample;
BW_PARAMETER_BLOCK_LAYOUT(PostBloomUpsample, BW_PARAMETER_MEMBER(PostBloomUpsample, level),
                          BW_PARAMETER_MEMBER(PostBloomUpsample, below),
                          BW_PARAMETER_MEMBER(PostBloomUpsample, sampler),
                          BW_PARAMETER_MEMBER(PostBloomUpsample, level_scale),
                          BW_PARAMETER_MEMBER(PostBloomUpsample, below_texel),
                          BW_PARAMETER_MEMBER(PostBloomUpsample, radius),
                          BW_PARAMETER_MEMBER(PostBloomUpsample, below_scale));
using PostGradeBake = bw::postprocess::PostGradeBake;
BW_PARAMETER_BLOCK_LAYOUT(
    PostGradeBake, BW_PARAMETER_MEMBER(PostGradeBake, table),
    BW_PARAMETER_MEMBER(PostGradeBake, shadows_max),
    BW_PARAMETER_MEMBER(PostGradeBake, highlights_min),
    BW_PARAMETER_MEMBER(PostGradeBake, highlights_max), BW_PARAMETER_MEMBER(PostGradeBake, slope),
    BW_PARAMETER_MEMBER(PostGradeBake, toe), BW_PARAMETER_MEMBER(PostGradeBake, shoulder),
    BW_PARAMETER_MEMBER(PostGradeBake, black_clip), BW_PARAMETER_MEMBER(PostGradeBake, white_clip),
    BW_PARAMETER_MEMBER(PostGradeBake, apply_white_balance),
    BW_PARAMETER_MEMBER(PostGradeBake, reserved0), BW_PARAMETER_MEMBER(PostGradeBake, reserved1),
    BW_PARAMETER_MEMBER(PostGradeBake, white_balance_row0),
    BW_PARAMETER_MEMBER(PostGradeBake, white_balance_row1),
    BW_PARAMETER_MEMBER(PostGradeBake, white_balance_row2),
    BW_PARAMETER_MEMBER(PostGradeBake, shadows_saturation),
    BW_PARAMETER_MEMBER(PostGradeBake, shadows_contrast),
    BW_PARAMETER_MEMBER(PostGradeBake, shadows_gamma),
    BW_PARAMETER_MEMBER(PostGradeBake, shadows_gain),
    BW_PARAMETER_MEMBER(PostGradeBake, shadows_offset),
    BW_PARAMETER_MEMBER(PostGradeBake, midtones_saturation),
    BW_PARAMETER_MEMBER(PostGradeBake, midtones_contrast),
    BW_PARAMETER_MEMBER(PostGradeBake, midtones_gamma),
    BW_PARAMETER_MEMBER(PostGradeBake, midtones_gain),
    BW_PARAMETER_MEMBER(PostGradeBake, midtones_offset),
    BW_PARAMETER_MEMBER(PostGradeBake, highlights_saturation),
    BW_PARAMETER_MEMBER(PostGradeBake, highlights_contrast),
    BW_PARAMETER_MEMBER(PostGradeBake, highlights_gamma),
    BW_PARAMETER_MEMBER(PostGradeBake, highlights_gain),
    BW_PARAMETER_MEMBER(PostGradeBake, highlights_offset));
using PostToneMap = bw::postprocess::PostToneMap;
BW_PARAMETER_BLOCK_LAYOUT(
    PostToneMap, BW_PARAMETER_MEMBER(PostToneMap, scene_color),
    BW_PARAMETER_MEMBER(PostToneMap, bloom), BW_PARAMETER_MEMBER(PostToneMap, factor),
    BW_PARAMETER_MEMBER(PostToneMap, adapted), BW_PARAMETER_MEMBER(PostToneMap, table),
    BW_PARAMETER_MEMBER(PostToneMap, sampler), BW_PARAMETER_MEMBER(PostToneMap, bloom_intensity),
    BW_PARAMETER_MEMBER(PostToneMap, vignette), BW_PARAMETER_MEMBER(PostToneMap, target_size),
    BW_PARAMETER_MEMBER(PostToneMap, dither_offset),
    BW_PARAMETER_MEMBER(PostToneMap, dither_amplitude), BW_PARAMETER_MEMBER(PostToneMap, reserved0),
    BW_PARAMETER_MEMBER(PostToneMap, reserved1), BW_PARAMETER_MEMBER(PostToneMap, reserved2));

#endif
