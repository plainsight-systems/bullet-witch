// Integration test: the post chain on this Mac's Metal device with the
// build's artifacts (bw/rendering/postprocess/post_chain.hpp, exposure.hpp,
// tone_mapping.hpp, bloom.hpp, grade_bake.hpp). Scene colour is written by
// a fixture pass or made with initial data; the chain's outputs are read
// back and compared with the headers' pure functions.
//
// Argument after the scenario name: the Metal artifact directory.
#include "bw/rendering/graph/builder.hpp"
#include "bw/rendering/graph/compiled_frame.hpp"
#include "bw/rendering/graph/declaration.hpp"
#include "bw/rendering/graph/executor.hpp"
#include "bw/rendering/graph/schedule_policy.hpp"
#include "bw/rendering/postprocess/bloom.hpp"
#include "bw/rendering/postprocess/exposure.hpp"
#include "bw/rendering/postprocess/grade.hpp"
#include "bw/rendering/postprocess/grade_bake.hpp"
#include "bw/rendering/postprocess/post_chain.hpp"
#include "bw/rendering/postprocess/tone_mapping.hpp"
#include "bw/rendering/rhi/access.hpp"
#include "bw/rendering/rhi/command_list.hpp"
#include "bw/rendering/rhi/device.hpp"
#include "bw/rendering/rhi/format.hpp"
#include "bw/rendering/rhi/pipeline_states.hpp"
#include "bw/rendering/rhi/resources.hpp"
#include "bw/rendering/rhi/synchronization.hpp"
#include "bw/rendering/rhi/viewport.hpp"
#include "bw/rendering/shaders/artifact_file.hpp"
#include "bw/rendering/shaders/parameter_block.hpp"
#include "bw/rendering/shaders/permutation.hpp"
#include "bw/testing/harness.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <functional>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

// The fixture vertex shader's per-draw block, as the registry declares it;
// uniform_field compares its layout hash with the registry's.
struct alignas(bw::shaders::kBlockAlignment) FullscreenTriangleDraw {
  bw::shaders::Float4 color{};
  bw::shaders::Float2 scale{};
  bw::shaders::TextureIndex texture{};
  bw::shaders::SamplerIndex sampler{};
};

} // namespace

BW_PARAMETER_BLOCK_LAYOUT(FullscreenTriangleDraw,
                          BW_PARAMETER_MEMBER(FullscreenTriangleDraw, color),
                          BW_PARAMETER_MEMBER(FullscreenTriangleDraw, scale),
                          BW_PARAMETER_MEMBER(FullscreenTriangleDraw, texture),
                          BW_PARAMETER_MEMBER(FullscreenTriangleDraw, sampler));

namespace {

namespace fs = std::filesystem;
namespace graph = bw::graph;
namespace post = bw::postprocess;
namespace rhi = bw::rhi;
using bw::testing::require;

struct Arguments {
  fs::path shaders{};
};

template <class T, class E> T take(rhi::Result<T, E> result, std::string_view what) {
  if (!result.ok()) {
    throw std::runtime_error(std::string(what) + " failed: " + std::string(result.detail()));
  }
  return std::move(result.value());
}

std::unique_ptr<rhi::Device> make_device() {
  rhi::DeviceDescription description{};
  description.backend = rhi::BackendKind::Metal;
  description.miss_policy = rhi::PipelineMissPolicy::CreateOnMiss;
  return take(rhi::Device::create(description), "Device::create");
}

// A float whose value a 16-bit float holds exactly, as its bits.
std::uint16_t exact_half(float value) {
  if (value == 0.0F) {
    return 0;
  }
  const auto bits = std::bit_cast<std::uint32_t>(value);
  return static_cast<std::uint16_t>(((((bits >> 23U) & 0xFFU) - 112U) << 10U) |
                                    ((bits & 0x7FFFFFU) >> 13U));
}

// One small float of R11G11B10Float: `bits` mantissa bits, a 5-bit exponent.
float small_float(std::uint32_t word, std::uint32_t mantissa_bits) {
  const std::uint32_t mantissa = word & ((1U << mantissa_bits) - 1U);
  const auto exponent = static_cast<int>(word >> mantissa_bits);
  const float fraction = static_cast<float>(mantissa) / static_cast<float>(1U << mantissa_bits);
  return exponent == 0 ? std::ldexp(fraction, -14) : std::ldexp(1.0F + fraction, exponent - 15);
}

// A texture the test owns, made with RGBA 16-bit float texels.
rhi::Ref<rhi::Texture> scene_texture(rhi::Device& device, rhi::Extent2 size,
                                     const std::function<float(std::uint32_t, std::uint32_t)>& at) {
  std::vector<std::uint16_t> texels(std::size_t{size.width} * size.height * 4U);
  for (std::uint32_t y = 0; y < size.height; ++y) {
    for (std::uint32_t x = 0; x < size.width; ++x) {
      const std::size_t base = (std::size_t{y} * size.width + x) * 4U;
      const std::uint16_t value = exact_half(at(x, y));
      texels[base] = value;
      texels[base + 1U] = value;
      texels[base + 2U] = value;
      texels[base + 3U] = exact_half(1.0F);
    }
  }
  rhi::TextureDescription description{};
  description.extent = rhi::Extent3{size.width, size.height, 1};
  description.format = post::kSceneColorFormat;
  description.usage = rhi::TextureUsage::ShaderResource;
  description.initial_access = rhi::kShaderResourceMask;
  description.debug_name = "test scene colour";
  const std::array<rhi::SubresourceData, 1> data{rhi::SubresourceData{
      std::as_bytes(std::span<const std::uint16_t>(texels)), size.width * 8U, 0U}};
  return take(device.create_texture(description, data), "scene colour texture");
}

// One readback: a staging buffer, its fence and the layout of what it holds.
struct Readback {
  rhi::Ref<rhi::StagingBuffer> staging;
  rhi::Ref<rhi::GpuFence> fence;
  rhi::StagingLayout layout{};
};

Readback make_readback(rhi::Device& device, const rhi::TextureDescription& like) {
  Readback readback{};
  readback.layout = rhi::staging_layout(like, 0);
  readback.staging =
      take(device.create_staging_buffer(readback.layout.total_bytes, "readback"), "staging buffer");
  readback.fence = take(device.create_fence("readback"), "fence");
  return readback;
}

void mark(graph::FrameGraph& frame_graph, graph::TextureHandle handle, const Readback& readback) {
  frame_graph.mark_readback(handle,
                            graph::ReadBack{readback.staging.get(), readback.fence.get(), 0});
}

// Compiles, executes and ends the frame; every readback is then locked and
// copied out, in the order given.
std::vector<std::vector<std::byte>> run(rhi::Device& device, graph::FrameGraph& frame_graph,
                                        std::span<Readback* const> readbacks) {
  graph::CompiledFrame compiled;
  require(frame_graph
              .compile(*graph::schedule_policy_for(rhi::BackendKind::Metal), device.capabilities(),
                       compiled)
              .ok(),
          "the graph compiles");
  rhi::CommandList list;
  require(graph::execute(compiled, frame_graph, device, list).ok(), "the graph records");
  require(device.execute(list).ok(), "the list executes");
  require(!device.end_frame().gpu_fault, "no GPU fault");
  std::vector<std::vector<std::byte>> out;
  for (Readback* readback : readbacks) {
    const rhi::StagingLock lock = take(
        device.lock_staging(*readback->staging, *readback->fence, rhi::WaitPolicy::Block), "lock");
    out.emplace_back(lock.bytes.begin(), lock.bytes.end());
    require(device.unlock_staging(*readback->staging) == rhi::StagingError::None, "unlock");
  }
  frame_graph.reset();
  return out;
}

std::uint32_t read_word(const std::vector<std::byte>& bytes, std::size_t offset) {
  std::uint32_t value = 0;
  std::memcpy(&value, bytes.data() + offset, sizeof(value));
  return value;
}

bool near(float got, float want, float relative) {
  return std::fabs(got - want) <= relative * std::fabs(want);
}

// The CPU's metering of a field of one colour: every sample of the 64 by 64
// target in the same buckets (exposure.hpp, contracts 3 and 4).
float metered_average(float luminance) {
  constexpr float kSamples = 32.0F * 32.0F;
  std::array<float, post::kHistogramBuckets> histogram{};
  const post::BucketSplit split = post::histogram_buckets(luminance);
  histogram[split.first] += split.first_weight * kSamples;
  histogram[split.second] += split.second_weight * kSamples;
  return post::windowed_average_luminance(histogram, post::kDefaultMinEv100);
}

// The table sampled trilinearly at the lookup coordinate of a neutral
// exposed value x (tone_mapping.hpp, contract 2d), with each texel at the
// format's nearest level (grade_bake.hpp, contract 11).
float neutral_lookup(float x, const post::Grade& grade) {
  const float position =
      post::lookup_coordinate(post::shaper_encode(x)) * static_cast<float>(post::kLookupSize) -
      0.5F;
  const float lower = std::floor(position);
  const float t = position - lower;
  const auto index = [lower](std::uint32_t step) {
    return static_cast<std::uint32_t>(std::clamp(lower + static_cast<float>(step), 0.0F,
                                                 static_cast<float>(post::kLookupSize - 1U)));
  };
  float sum = 0.0F;
  for (std::uint32_t corner = 0; corner < 8U; ++corner) {
    const std::array<std::uint32_t, 3> steps{corner & 1U, (corner >> 1U) & 1U, corner >> 2U};
    const post::Rgb stored = post::bake_texel(post::Rgb{post::table_coordinate(index(steps[0])),
                                                        post::table_coordinate(index(steps[1])),
                                                        post::table_coordinate(index(steps[2]))},
                                              grade);
    float weight = 1.0F;
    for (const std::uint32_t step : steps) {
      weight *= step == 0U ? 1.0F - t : t;
    }
    sum += weight * std::round(stored.r * 1023.0F) / 1023.0F;
  }
  return sum * post::kLookupScale;
}

// (post_chain.hpp, contracts 2 to 4; exposure.hpp, contracts 6 to 8;
// tone_mapping.hpp, contracts 1 and 2): a fixture pass writes one colour
// into a 64 by 64 scene colour and the chain runs into a B8G8R8A8Unorm
// target, under the default grade's range, which leaves the exposure
// following the meter.
// Frame 0, a neutral level with reset: the adapted texel is the CPU's
// adaptation of the CPU's metering, and the centre pixel's display value is
// the table's lookup of the value exposed by that texel over the factor,
// within one code value plus the dither's amplitude, and its alpha is 1.
// Frame 1, a saturated
// colour and a time step: the texel moves by the adaptation step from the
// channel mean. Frame 2 is declared and dropped; frame 3 reads the factor
// frame 2 read. A hardware-sRGB target is refused.
void uniform_field(const Arguments& args) {
  constexpr rhi::Extent2 kSize{64, 64};
  constexpr std::uint32_t kCentre = 32;
  const std::unique_ptr<rhi::Device> device = make_device();
  post::ToneMapping refused;
  require(post::create_tone_mapping(*device, args.shaders, rhi::Format::B8G8R8A8UnormSrgb, refused)
                  .error == post::ToneMappingError::TargetFormatUnsupported,
          "a hardware-sRGB target is refused");
  post::PostChain chain;
  require(post::build_post_chain(*device, args.shaders, rhi::Format::B8G8R8A8Unorm, chain).ok(),
          "build_post_chain");
  const bw::shaders::ShaderDescription* triangle =
      bw::shaders::find_shader(bw::shaders::shader_registry(), "fullscreen_triangle");
  require(triangle != nullptr && triangle->draw_block != nullptr &&
              triangle->draw_block->hash ==
                  bw::shaders::BlockLayoutOf<FullscreenTriangleDraw>::value.hash,
          "the test's draw block has the registry's layout");

  // The fixture pass: the fullscreen triangle times a white texel.
  bw::shaders::ArtifactFile vertex;
  bw::shaders::ArtifactFile pixel;
  require(bw::shaders::read_artifact_file(args.shaders, "fullscreen_triangle", {}, vertex).ok() &&
              bw::shaders::read_artifact_file(args.shaders, "fullscreen_triangle_color", {}, pixel)
                  .ok(),
          "the fixture artifacts");
  rhi::DepthStencilDescription depth_off{};
  depth_off.depth_write = false;
  depth_off.depth_test = rhi::CompareFunction::Always;
  rhi::GraphicsPipelineDescription fixture{};
  fixture.vertex_shader = &vertex.artifact;
  fixture.pixel_shader = &pixel.artifact;
  fixture.rasterizer = take(device->rasterizer_state({}), "rasterizer").get();
  fixture.depth_stencil = take(device->depth_stencil_state(depth_off), "depth").get();
  fixture.blend = take(device->blend_state({}), "blend").get();
  fixture.targets.color_formats[0] = post::kSceneColorFormat;
  fixture.targets.color_count = 1;
  fixture.debug_name = "fixture field";
  const rhi::Ref<rhi::GraphicsPipeline> pipeline =
      take(device->graphics_pipeline(fixture), "fixture pipeline");
  const rhi::Ref<rhi::Texture> white =
      scene_texture(*device, {1, 1}, [](auto, auto) { return 1.0F; });
  const rhi::Ref<rhi::ShaderResourceView> white_view =
      take(device->create_shader_resource_view(white, {}), "white view");
  const rhi::Ref<rhi::Sampler> sampler = take(device->sampler({}), "sampler");

  rhi::TextureDescription target_description{};
  target_description.extent = rhi::Extent3{kSize.width, kSize.height, 1};
  target_description.format = rhi::Format::B8G8R8A8Unorm;
  target_description.usage = rhi::TextureUsage::RenderTarget | rhi::TextureUsage::CopySource;
  target_description.initial_access = rhi::Access::RenderTarget;
  target_description.debug_name = "display target";
  const rhi::Ref<rhi::Texture> target = take(device->create_texture(target_description), "target");
  rhi::TextureDescription texel_description = target_description;
  texel_description.extent = rhi::Extent3{1, 1, 1};
  texel_description.format = post::kExposureFormat;
  Readback display = make_readback(*device, target_description);
  Readback adapted = make_readback(*device, texel_description);
  const std::array<Readback*, 2> both{&display, &adapted};

  graph::FrameGraph frame_graph;
  FullscreenTriangleDraw block{};
  auto record = [&](graph::RecordContext& context) {
    context.list().set_graphics_pipeline(*pipeline);
    context.list().set_draw_block(block);
    context.list().draw(rhi::DrawArguments{3, 1, 0, 0});
  };
  // A compensation that puts the centre's exposed value half way between
  // two of the table's texels, so the display value depends on the filter.
  post::Grade grade = post::default_grade();
  grade.exposure_compensation = 0.79F;
  // Declares one frame of `color`.
  const auto declare = [&](post::Rgb color, float seconds, bool reset) {
    block = FullscreenTriangleDraw{bw::shaders::Float4{color.r, color.g, color.b, 1.0F},
                                   bw::shaders::Float2{1.0F, 1.0F}, white_view->texture_index(),
                                   sampler->index()};
    post::PostFrame frame{};
    require(post::begin_post_frame(chain, *device, frame_graph, frame).ok(), "begin_post_frame");
    const graph::TextureHandle scene =
        frame_graph.create_texture(post::scene_color_description(kSize));
    const std::array<graph::TextureUse, 1> uses{
        graph::TextureUse{scene, graph::Access::Attachment, graph::Contents::Overwrite, 0}};
    frame_graph.add_pass(
        graph::PassDeclaration{"fixture field", graph::PassKind::Raster, uses, {}, record});
    const graph::TextureHandle out = frame_graph.import_texture(
        graph::ImportedTexture{target.get(), rhi::Access::RenderTarget, rhi::Access::RenderTarget});
    post::PostFrameInputs inputs{};
    inputs.grade = grade;
    inputs.display = rhi::display_report_for(rhi::Format::B8G8R8A8Unorm);
    inputs.delta_seconds = seconds;
    inputs.reset = reset;
    require(post::declare_post_chain(chain, *device, frame_graph, frame, scene, out, kSize, inputs)
                .ok(),
            "declare_post_chain");
    mark(frame_graph, out, display);
    mark(frame_graph, frame.exposure.adapted, adapted);
  };
  // Frame 0: neutral, reset.
  const float level = 0.5F;
  declare(post::Rgb{level, level, level}, 0.0F, true);
  std::vector<std::vector<std::byte>> bytes = run(*device, frame_graph, both);
  const float e0 = std::bit_cast<float>(read_word(bytes[1], 0));
  const float want_e0 =
      post::adapt_exposure({post::kInitialExposure, metered_average(level / post::kInitialExposure),
                            grade.exposure_compensation, 0.0F, true});
  // Scene colour is half-float and the histogram's weights fixed point, so
  // the texel matches the CPU's float metering to about 1e-5; 1e-3 is far
  // below the 10% a meter on luminance moves frame 1 by.
  require(near(e0, want_e0, 1e-3F), "frame 0's adapted texel is the CPU's metering");
  const float uv = (static_cast<float>(kCentre) + 0.5F) / static_cast<float>(kSize.width);
  const float exposed = (level + level * grade.bloom_intensity) * (e0 / post::kInitialExposure) *
                        post::vignette_mask(uv, uv, 1.0F, grade.vignette);
  const float want_display = neutral_lookup(exposed, grade);
  const std::size_t pixel_offset =
      (std::size_t{kCentre} * display.layout.row_pitch) + (std::size_t{kCentre} * 4U);
  // B, G, R, A; the level is neutral, so the three colour bytes agree.
  for (std::size_t channel = 0; channel < 3U; ++channel) {
    const float got = static_cast<float>(bytes[0][pixel_offset + channel]) / 255.0F;
    require(std::fabs(got - want_display) <=
                1.0F / 255.0F + post::dither_amplitude(rhi::Format::B8G8R8A8Unorm),
            channel, "the centre's display value is the lookup of the exposed level");
  }
  require(bytes[0][pixel_offset + 3U] == std::byte{255}, "the centre's alpha is 1");

  // Frame 1: a saturated colour, pre-exposed by e0, a time step.
  const post::Rgb saturated{2.0F, 0.25F, 0.0F};
  const float seconds = 0.5F;
  const auto mean_over = [&saturated](float factor) {
    return (saturated.r + saturated.g + saturated.b) / 3.0F / factor;
  };
  declare(saturated, seconds, false);
  const float e1 = std::bit_cast<float>(read_word(run(*device, frame_graph, both)[1], 0));
  const float want_e1 = post::adapt_exposure(
      {e0, metered_average(mean_over(e0)), grade.exposure_compensation, seconds, false});
  require(near(e1, want_e1, 1e-3F), "frame 1 adapts from the channel mean");

  // Frame 2 declared and dropped; frame 3 reads e1 again.
  declare(saturated, seconds, false);
  post::post_frame_dropped(chain);
  frame_graph.reset();
  declare(saturated, seconds, false);
  const float e3 = std::bit_cast<float>(read_word(run(*device, frame_graph, both)[1], 0));
  const float want_e3 = post::adapt_exposure(
      {e1, metered_average(mean_over(e1)), grade.exposure_compensation, seconds, false});
  require(near(e3, want_e3, 1e-3F), "after a dropped frame the factor is the dropped frame's");
}

// (bloom.hpp, contracts 1 and 2): at an odd size, a uniform field gives the
// same value in the result, a bright block centred on the target gives a
// result equal to its own mirror in x and in y, and the result's extent is
// level 0's, 45 by 29 halved rounding up.
void bloom(const Arguments& args) {
  constexpr rhi::Extent2 kSize{45, 29};
  constexpr rhi::Extent2 level0{23, 15};
  const std::unique_ptr<rhi::Device> device = make_device();
  post::Bloom chain;
  require(post::create_bloom(*device, args.shaders, chain).ok(), "create_bloom");
  rhi::TextureDescription result_description{};
  result_description.extent = rhi::Extent3{level0.width, level0.height, 1};
  result_description.format = post::kBloomFormat;
  Readback readback = make_readback(*device, result_description);
  const std::array<Readback*, 1> one{&readback};
  graph::FrameGraph frame_graph;
  // Runs the chain over a field and returns its result's texels.
  const auto filter = [&](const std::function<float(std::uint32_t, std::uint32_t)>& at) {
    const rhi::Ref<rhi::Texture> scene = scene_texture(*device, kSize, at);
    const graph::TextureHandle source = frame_graph.import_texture(
        graph::ImportedTexture{scene.get(), rhi::kShaderResourceMask, rhi::kShaderResourceMask});
    graph::TextureHandle result{};
    require(post::declare_bloom(chain, *device, frame_graph, source, kSize, result).ok(),
            "declare_bloom");
    require(frame_graph.texture_description(result)->extent == level0,
            "the result is level 0's size");
    mark(frame_graph, result, readback);
    const std::vector<std::byte> bytes = run(*device, frame_graph, one)[0];
    std::vector<float> red(std::size_t{level0.width} * level0.height);
    for (std::uint32_t y = 0; y < level0.height; ++y) {
      for (std::uint32_t x = 0; x < level0.width; ++x) {
        red[std::size_t{y} * level0.width + x] = small_float(
            read_word(bytes, (std::size_t{y} * readback.layout.row_pitch) + (std::size_t{x} * 4U)) &
                0x7FFU,
            6U);
      }
    }
    return red;
  };
  // One step of the format's 6-bit mantissa.
  const auto same = [](float a, float b) {
    return std::fabs(a - b) <= std::max(std::fabs(a), std::fabs(b)) / 64.0F + 1e-6F;
  };
  const std::vector<float> uniform = filter([](auto, auto) { return 0.5F; });
  for (std::size_t i = 0; i < uniform.size(); ++i) {
    require(same(uniform[i], 0.5F), i, "a uniform field keeps its value");
  }
  const std::vector<float> block = filter([](std::uint32_t x, std::uint32_t y) {
    return x >= 18U && x < 27U && y >= 10U && y < 19U ? 8.0F : 0.25F;
  });
  for (std::uint32_t y = 0; y < level0.height; ++y) {
    for (std::uint32_t x = 0; x < level0.width; ++x) {
      const float here = block[std::size_t{y} * level0.width + x];
      require(same(here, block[std::size_t{y} * level0.width + (level0.width - 1U - x)]) &&
                  same(here, block[std::size_t{level0.height - 1U - y} * level0.width + x]),
              std::size_t{y} * 100U + x, "a mirrored field gives a mirrored result");
    }
  }
}

// (grade_bake.hpp, contract 11): eight texels of the table, four of them
// saturated colours, read back equal bake_texel within one level of the
// format, unpacked by R10G10B10A2Unorm's layout; a display that is not sRGB
// and a grade that does not validate are refused by name with nothing
// declared.
void table(const Arguments& args) {
  const std::unique_ptr<rhi::Device> device = make_device();
  post::GradeBake bake;
  require(post::create_grade_bake(*device, args.shaders, bake).ok(), "create_grade_bake");
  const post::Grade grade = post::default_grade();
  const rhi::DisplayReport srgb = rhi::display_report_for(rhi::Format::B8G8R8A8Unorm);
  graph::FrameGraph frame_graph;
  graph::TextureHandle handle{};
  rhi::DisplayReport pq = srgb;
  pq.encoding = rhi::DisplayEncoding::Pq2020;
  post::Grade invalid = grade;
  invalid.curve.slope = 0.0F;
  const post::GradeBakeFailure display_refused =
      post::declare_grade_bake(bake, *device, frame_graph, grade, pq, handle);
  const post::GradeBakeFailure grade_refused =
      post::declare_grade_bake(bake, *device, frame_graph, invalid, srgb, handle);
  require(display_refused.error == post::GradeBakeError::DisplayEncodingUnsupported,
          "a PQ display is refused");
  require(grade_refused.error == post::GradeBakeError::GradeInvalid &&
              grade_refused.grade == post::GradeError::CurveInvalid,
          "an invalid grade is refused with its rule");
  require(frame_graph.pass_count() == 0U && frame_graph.texture_count() == 0U &&
              frame_graph.buffer_count() == 0U,
          "a refused bake declares nothing");

  require(post::declare_grade_bake(bake, *device, frame_graph, grade, srgb, handle).ok(),
          "declare_grade_bake");
  rhi::TextureDescription like{};
  like.extent = rhi::Extent3{post::kLookupSize, post::kLookupSize, post::kLookupSize};
  like.dimension = rhi::TextureDimension::Texture3D;
  like.format = post::kLookupFormat;
  Readback readback = make_readback(*device, like);
  mark(frame_graph, handle, readback);
  const std::array<Readback*, 1> one{&readback};
  const std::vector<std::byte> bytes = run(*device, frame_graph, one)[0];
  const std::array<std::array<std::uint32_t, 3>, 8> texels{{{0, 0, 0},
                                                            {13, 13, 13},
                                                            {31, 31, 31},
                                                            {20, 9, 4},
                                                            {31, 0, 0},
                                                            {0, 31, 0},
                                                            {0, 0, 31},
                                                            {24, 0, 24}}};
  for (std::size_t i = 0; i < texels.size(); ++i) {
    const auto [x, y, z] = texels[i];
    const std::uint32_t word =
        read_word(bytes, std::size_t{z} * readback.layout.slice_pitch +
                             std::size_t{y} * readback.layout.row_pitch + std::size_t{x} * 4U);
    const post::Rgb want = post::bake_texel(
        post::Rgb{post::table_coordinate(x), post::table_coordinate(y), post::table_coordinate(z)},
        grade);
    const std::array<float, 3> wanted{want.r, want.g, want.b};
    for (std::uint32_t channel = 0; channel < 3U; ++channel) {
      const auto got = static_cast<float>((word >> (10U * channel)) & 0x3FFU);
      require(std::fabs(got - wanted[channel] * 1023.0F) <= 1.0F, (i * 10U) + channel,
              "the texel is bake_texel within one level");
    }
  }
}

constexpr std::array<bw::testing::Scenario<Arguments>, 3> kScenarios{{
    {"uniform_field", &uniform_field},
    {"bloom", &bloom},
    {"table", &table},
}};

} // namespace

int main(int argc, char** argv) {
  return bw::testing::run_scenario(argc, argv, kScenarios, "<metal artifact directory>",
                                   [](std::span<char* const> rest, Arguments& out) {
                                     if (rest.size() != 1U) {
                                       return false;
                                     }
                                     out = Arguments{rest[0]};
                                     return true;
                                   });
}
