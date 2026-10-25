// Engine presentation time is an explicit snapshot, including paused frames;
// exposure never reads a target-local wall clock. Both-phase I.1 (#ri-explicit)
// exposes the input; MEM.9 (guidelines/memory/MEM.9-embedded-allocate-at-init.md)
// keeps the handoff inline and allocation-free. Lighting equations are unchanged.
// Encode the recovered HDR-to-display dependency chain into one caller-owned
// command buffer. Targets survive frames; only completed R32 luminance is read
// back. Geometry and overlays remain separate passes. Contracts and both-phase
// I.11 / GPU.9 application are in world_postprocess.hpp. Native target sizes and
// formats follow original construction; source equations live in standalone
// shaders and world-display-output.md. Work is bounded by scene pixels and fixed
// postprocess targets, with no steady-state device storage allocation.
#include "platform/macos/world_postprocess.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>
#include "bw/memory/allocator.hpp"
#include "platform/macos/texture_allocation.hpp"
namespace bw::platform::macos {
namespace {
enum class Target : std::uint8_t {
  log_color,
  down_color,
  down_luminance,
  reduce64,
  reduce16,
  reduce4,
  reduce1,
  bloom_extract,
  bloom_radial256,
  bloom_down,
  bloom_radial128,
  bloom_horizontal,
  bloom_vertical,
  count
};
enum class Pass : std::uint8_t {
  scale_rgb,
  log,
  down,
  down_luminance,
  reduce,
  extract,
  radial,
  horizontal,
  vertical,
  display,
  count
};
constexpr std::size_t kTargets = static_cast<std::size_t>(Target::count);
constexpr std::size_t kPasses = static_cast<std::size_t>(Pass::count);
struct OutputUniforms {
  std::array<float, 2> inverse_extent{};
  float exposure_key{}, inverse_gamma{}, bloom_key{}, bloom_threshold{}, bloom_soft_knee{},
      log_luminance{};
  std::uint32_t adapted{}, bloom{}, enabled{};
  float opacity{1};
  std::array<float, 4> global_color{1, 1, 1, 1};
  std::array<float, 4> first_measurement_bounds{};
};
constexpr std::array<std::uint32_t, kTargets> kDimensions{512, 256, 256, 64,  16,  4,  1,
                                                          256, 256, 128, 128, 128, 128};
constexpr std::size_t index(Target value) { return static_cast<std::size_t>(value); }
constexpr std::size_t index(Pass value) { return static_cast<std::size_t>(value); }
bool luminance_target(std::size_t ordinal) {
  return ordinal >= index(Target::down_luminance) && ordinal <= index(Target::reduce1);
}
}  // namespace
struct WorldPostprocess::State {
  State(id<MTLDevice> native_device, std::shared_ptr<memory::DeviceBudget> native_budget)
      : device(native_device), budget(std::move(native_budget)) {}
  id<MTLDevice> device;
  std::shared_ptr<memory::DeviceBudget> budget;
  TextureAllocation scene, depth, filtered_scene;
  std::array<TextureAllocation, kTargets> targets;
  std::array<id<MTLRenderPipelineState>, kPasses> pipelines{};
  render::WorldOutputParameters parameters;
  render::ExposureController exposure;
  OutputUniforms uniforms;
  id<MTLCommandBuffer> previous{};
  TextureAllocation make_target(std::uint32_t width, std::uint32_t height, MTLPixelFormat format) {
    return TextureAllocation::create(device, budget, width, height, format, MTLTextureType2D,
                                     MTLTextureUsageShaderRead | MTLTextureUsageRenderTarget);
  }
  id<MTLTexture> texture(Target target) const { return targets.at(index(target)).get(); }
  id<MTLRenderPipelineState> pipeline(Pass pass, id<MTLLibrary> library) {
    auto& result = pipelines.at(index(pass));
    if (result != nil) return result;
    const std::array<NSString*, kPasses> names{
        @"world_scale_rgb",        @"world_log_luminance",
        @"world_downsample",       @"world_downsample_luminance",
        @"world_reduce_luminance", @"world_bloom_extract",
        @"world_bloom_radial",     @"world_bloom_horizontal",
        @"world_bloom_vertical",   @"world_display_output"};
    auto* descriptor = [[MTLRenderPipelineDescriptor alloc] init];
    descriptor.vertexFunction = [library newFunctionWithName:@"world_output_vertex"];
    descriptor.fragmentFunction = [library newFunctionWithName:names.at(index(pass))];
    descriptor.colorAttachments[0].pixelFormat =
        pass == Pass::display
            ? MTLPixelFormatBGRA8Unorm
            : (pass == Pass::down_luminance || pass == Pass::reduce ? MTLPixelFormatR32Float
                                                                    : MTLPixelFormatRGBA16Float);
    NSError* error = nil;
    result = [device newRenderPipelineStateWithDescriptor:descriptor error:&error];
    if (result == nil) throw std::runtime_error("World output pipeline creation failed");
    return result;
  }
  void bind(id<MTLRenderCommandEncoder> encoder, Pass pass, id<MTLLibrary> library,
            id<MTLTexture> input, const OutputUniforms& values) {
    [encoder setRenderPipelineState:pipeline(pass, library)];
    [encoder setCullMode:MTLCullModeNone];
    [encoder setFragmentTexture:input atIndex:0];
    if (pass == Pass::extract || pass == Pass::display) {
      [encoder setFragmentTexture:texture(Target::reduce1) atIndex:1];
    }
    if (pass == Pass::display) {
      [encoder setFragmentTexture:texture(Target::bloom_vertical) atIndex:2];
    }
    [encoder setVertexBytes:&values length:sizeof(values) atIndex:0];
    [encoder setFragmentBytes:&values length:sizeof(values) atIndex:0];
    [encoder drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4];
  }
  void run(id<MTLCommandBuffer> command, id<MTLLibrary> library, id<MTLTexture> input, Pass pass,
           id<MTLTexture> target) {
    auto* descriptor = [MTLRenderPassDescriptor renderPassDescriptor];
    descriptor.colorAttachments[0].texture = target;
    descriptor.colorAttachments[0].loadAction = MTLLoadActionDontCare;
    descriptor.colorAttachments[0].storeAction = MTLStoreActionStore;
    auto encoder = [command renderCommandEncoderWithDescriptor:descriptor];
    if (encoder == nil) throw std::runtime_error("World output pass encoder unavailable");
    auto values = uniforms;
    values.inverse_extent = {1.0F / static_cast<float>(target.width),
                             1.0F / static_cast<float>(target.height)};
    bind(encoder, pass, library, input, values);
    [encoder endEncoding];
  }
  void run(id<MTLCommandBuffer> command, id<MTLLibrary> library, Pass pass, id<MTLTexture> input,
           Target output) {
    run(command, library, input, pass, texture(output));
  }
  id<MTLTexture> color_input() const {
    return parameters.global_color_mode == 2 ? filtered_scene.get() : scene.get();
  }
};
WorldPostprocess::WorldPostprocess(id<MTLDevice> device, const std::shared_ptr<memory::Domain>& cpu,
                                   std::shared_ptr<memory::DeviceBudget> budget)
    : state_(
          std::allocate_shared<State>(memory::Allocator<State>(cpu), device, std::move(budget))) {
  if (device == nil || !state_->budget) {
    throw std::invalid_argument("Missing world output device/budget");
  }
}
WorldPostprocess::~WorldPostprocess() = default;
void WorldPostprocess::configure(const render::WorldOutputParameters& parameters) {
  if ((parameters.global_color_mode != 0 && parameters.global_color_mode != 2) ||
      !std::ranges::all_of(parameters.global_color,
                           [](float value) { return std::isfinite(value); })) {
    throw std::invalid_argument("Unsupported or invalid world color filter");
  }
  if (parameters.tone_mapping_enabled) {
    const std::array values{parameters.exposure_key, parameters.gamma,
                            parameters.bloom_exposure_key, parameters.bloom_threshold,
                            parameters.bloom_soft_knee};
    if (!std::ranges::all_of(values, [](float value) { return std::isfinite(value); })) {
      throw std::invalid_argument("Invalid world output parameters");
    }
  }
  // Validate adaptation inputs without advancing the retained scene history.
  if (parameters.tone_mapping_enabled) {
    render::ExposureController validation;
    validation.advance({0, 0}, parameters.exposure);
  }
  if (state_->parameters.tone_mapping_enabled != parameters.tone_mapping_enabled) {
    state_->previous = nil;
    state_->exposure.reset();
  }
  state_->parameters = parameters;
}
void WorldPostprocess::reset() noexcept {
  auto& state = *state_;
  state.scene.reset();
  state.depth.reset();
  state.filtered_scene.reset();
  for (auto& target : state.targets) target.reset();
  state.previous = nil;
  state.exposure.reset();
}
id<MTLRenderCommandEncoder> WorldPostprocess::begin_scene(id<MTLCommandBuffer> command,
                                                          std::uint32_t width,
                                                          std::uint32_t height) {
  auto& state = *state_;
  if (command == nil || command.device != state.device) {
    throw std::invalid_argument("Invalid world output command");
  }
  if (state.previous != nil && state.previous.status != MTLCommandBufferStatusCompleted) {
    throw std::logic_error("World output reused before GPU completion");
  }
  if (state.scene.get() == nil || state.scene.get().width != width ||
      state.scene.get().height != height) {
    auto scene = state.make_target(width, height, MTLPixelFormatRGBA16Float);
    auto depth = state.make_target(width, height, MTLPixelFormatDepth32Float);
    state.scene = std::move(scene);
    state.depth = std::move(depth);
  }
  auto* descriptor = [MTLRenderPassDescriptor renderPassDescriptor];
  descriptor.colorAttachments[0].texture = state.scene.get();
  descriptor.colorAttachments[0].loadAction = MTLLoadActionClear;
  descriptor.colorAttachments[0].storeAction = MTLStoreActionStore;
  descriptor.colorAttachments[0].clearColor = MTLClearColorMake(0, 0, 0, 1);
  descriptor.depthAttachment.texture = state.depth.get();
  descriptor.depthAttachment.loadAction = MTLLoadActionClear;
  descriptor.depthAttachment.storeAction = MTLStoreActionDontCare;
  // Original scene targets clear far depth to zero before greater-depth draws.
  descriptor.depthAttachment.clearDepth = 0;
  auto encoder = [command renderCommandEncoderWithDescriptor:descriptor];
  if (encoder == nil) throw std::runtime_error("World HDR encoder unavailable");
  return encoder;
}
void WorldPostprocess::prepare_output(id<MTLCommandBuffer> command, id<MTLLibrary> library,
                                      timing::FrameTiming frame) {
  if (!std::isfinite(frame.presentation_seconds) || frame.presentation_seconds < 0) {
    throw std::invalid_argument("Invalid world presentation interval");
  }
  auto& state = *state_;
  if (command == nil || library == nil || state.scene.get() == nil) {
    throw std::invalid_argument("Missing world output inputs");
  }
  // Only the previous completed measurement crosses to CPU; all image data stays native.
  auto& u = state.uniforms;
  const auto& p = state.parameters;
  u = {};
  u.opacity = 1;
  u.enabled = static_cast<std::uint32_t>(p.tone_mapping_enabled);
  // The original ScalingRGB executor writes a half-float scene before any
  // luminance or bloom work. It preserves alpha, clamps only the authored RGB
  // multipliers, and never saturates the resulting HDR pixels.
  if (p.global_color_mode == 2) {
    for (std::size_t i = 0; i < 3; ++i) u.global_color.at(i) = std::max(p.global_color.at(i), 0.0F);
    const auto scene = state.scene.get();
    if (state.filtered_scene.get() == nil || state.filtered_scene.get().width != scene.width ||
        state.filtered_scene.get().height != scene.height) {
      state.filtered_scene =
          state.make_target(static_cast<std::uint32_t>(scene.width),
                            static_cast<std::uint32_t>(scene.height), MTLPixelFormatRGBA16Float);
    }
    state.run(command, library, scene, Pass::scale_rgb, state.filtered_scene.get());
  }
  if (p.tone_mapping_enabled) {
    constexpr float minimum_parameter = .01F, maximum_parameter = 64;
    u.exposure_key = std::clamp(p.exposure_key, minimum_parameter, maximum_parameter);
    u.inverse_gamma = 1 / std::clamp(p.gamma, minimum_parameter, maximum_parameter);
    u.bloom_key = std::clamp(p.bloom_exposure_key, minimum_parameter, maximum_parameter);
    constexpr float maximum_threshold = 16;
    u.bloom_threshold = std::clamp(p.bloom_threshold, minimum_parameter, maximum_threshold);
    u.bloom_soft_knee = std::clamp(p.bloom_soft_knee, minimum_parameter, maximum_parameter);
    u.bloom = static_cast<std::uint32_t>(p.bloom_enabled);
    const auto bounds = render::exposure_output_bounds(p.exposure);
    u.first_measurement_bounds = {
        bounds.minimum_luminance.value_or(0),
        bounds.maximum_luminance.value_or(std::numeric_limits<float>::max()),
        bounds.measurement_floor, p.exposure.history_window > 0 ? 1.0F : 0.0F};
    if (state.previous != nil && state.texture(Target::reduce1) != nil) {
      float measured{};
      [state.texture(Target::reduce1) getBytes:&measured
                                   bytesPerRow:sizeof(float)
                                    fromRegion:MTLRegionMake2D(0, 0, 1, 1)
                                   mipmapLevel:0];
      const auto adapted =
          state.exposure.advance({measured, frame.presentation_seconds}, p.exposure);
      u.log_luminance = std::log(adapted);
      u.adapted = static_cast<std::uint32_t>(p.exposure.history_window > 0);
    }
    for (std::size_t i = 0; i < kTargets; ++i) {
      if (state.targets.at(i).get() == nil) {
        state.targets.at(i) = state.make_target(
            kDimensions.at(i), kDimensions.at(i),
            luminance_target(i) ? MTLPixelFormatR32Float : MTLPixelFormatRGBA16Float);
      }
    }
    state.run(command, library, Pass::log, state.color_input(), Target::log_color);
    state.run(command, library, Pass::down, state.texture(Target::log_color), Target::down_color);
    state.run(command, library, Pass::down_luminance, state.texture(Target::log_color),
              Target::down_luminance);
    for (std::size_t i = index(Target::reduce64); i <= index(Target::reduce1); ++i) {
      state.run(command, library, Pass::reduce, state.targets.at(i - 1).get(),
                static_cast<Target>(i));
    }
    if (p.bloom_enabled) {
      state.run(command, library, Pass::extract, state.texture(Target::down_color),
                Target::bloom_extract);
      state.run(command, library, Pass::radial, state.texture(Target::bloom_extract),
                Target::bloom_radial256);
      state.run(command, library, Pass::down, state.texture(Target::bloom_radial256),
                Target::bloom_down);
      state.run(command, library, Pass::radial, state.texture(Target::bloom_down),
                Target::bloom_radial128);
      state.run(command, library, Pass::horizontal, state.texture(Target::bloom_radial128),
                Target::bloom_horizontal);
      state.run(command, library, Pass::vertical, state.texture(Target::bloom_horizontal),
                Target::bloom_vertical);
    }
  }
  state.previous = command;
}
void WorldPostprocess::encode_display(id<MTLRenderCommandEncoder> encoder, id<MTLLibrary> library,
                                      float opacity) {
  if (encoder == nil || library == nil || !std::isfinite(opacity) || opacity < 0 || opacity > 1) {
    throw std::invalid_argument("Invalid world display output");
  }
  auto& state = *state_;
  auto values = state.uniforms;
  values.opacity = opacity;
  values.inverse_extent = {1.0F / static_cast<float>(state.scene.get().width),
                           1.0F / static_cast<float>(state.scene.get().height)};
  state.bind(encoder, Pass::display, library, state.color_input(), values);
}
}  // namespace bw::platform::macos
