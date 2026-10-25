// Native scene replay contract: the same opaque geometry must survive its
// preparation depth when the color pipeline replays it with GREATER_EQUAL.
// Varying clip w/z and joint transforms exercise full surfaces, not one affine
// center pixel. Inputs are authored; no game window or original assets are used.
// Retained renderer/texture owners survive GPU completion, then one bounded
// full-frame readback compares color-only and preparation-plus-color outcomes.
// No production readback or widened mutation capability is introduced.
// Both design and implementation retrieved Core E.6 (#re-raii) and performance
// GPU.1 (guidelines/gpu/GPU.1-keep-data-on-device.md): RAII native candidates,
// explicit completed diagnostic readback, no throughput claim.
#import <Metal/Metal.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>

#include "bw/formats/model.hpp"
#include "bw/formats/world_texture.hpp"
#include "model_fixture.hpp"
#include "platform/macos/cpu_backing.hpp"
#include "platform/macos/texture_allocation.hpp"
#include "platform/macos/world_renderer.hpp"

namespace {
constexpr std::uint32_t kExtent = 128;
using Pixels = std::array<std::uint16_t, std::size_t{kExtent} * kExtent * 4>;
void require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}
struct SceneTargets {
  id<MTLTexture> color;
  id<MTLTexture> depth;
};
// The caller owns all resources; complete this command before reading or
// submitting a replacement view. No lifetime escapes this diagnostic call.
Pixels render(bw::platform::macos::WorldRenderer& renderer, id<MTLCommandQueue> queue,
              id<MTLLibrary> library, const SceneTargets& targets) {
  auto* pass = [MTLRenderPassDescriptor renderPassDescriptor];
  pass.colorAttachments[0].texture = targets.color;
  pass.colorAttachments[0].loadAction = MTLLoadActionClear;
  pass.colorAttachments[0].storeAction = MTLStoreActionStore;
  pass.colorAttachments[0].clearColor = MTLClearColorMake(0, 0, 0, 0);
  pass.depthAttachment.texture = targets.depth;
  pass.depthAttachment.loadAction = MTLLoadActionClear;
  pass.depthAttachment.storeAction = MTLStoreActionDontCare;
  pass.depthAttachment.clearDepth = 0;
  id<MTLCommandBuffer> command = [queue commandBuffer];
  renderer.prepare_shadows(command, library);
  id<MTLRenderCommandEncoder> encoder = [command renderCommandEncoderWithDescriptor:pass];
  // Match begin_scene: D3D integer sample centers on the native color target.
  [encoder setViewport:MTLViewport{.5, .5, kExtent, kExtent, 0, 1}];
  renderer.encode_color(encoder, library, 1, MTLPixelFormatRGBA16Float);
  [encoder endEncoding];
  [command commit];
  [command waitUntilCompleted];
  require(command.status == MTLCommandBufferStatusCompleted, "Scene replay command completed");
  Pixels pixels{};
  [targets.color getBytes:pixels.data()
              bytesPerRow:std::size_t{kExtent} * 8
               fromRegion:MTLRegionMake2D(0, 0, kExtent, kExtent)
              mipmapLevel:0];
  return pixels;
}
}  // namespace

void BWTestSceneReplay(id<MTLDevice> device) {
  auto domain = bw::memory::Domain::create(bw::platform::macos::make_cpu_backing());
  auto budget = bw::memory::DeviceBudget::create(domain);
  id<MTLCommandQueue> queue = [device newCommandQueue];
  bw::platform::macos::WorldRenderer renderer(device, queue, domain, budget);
  auto color = bw::platform::macos::TextureAllocation::create(
      device, budget, kExtent, kExtent, MTLPixelFormatRGBA16Float, MTLTextureType2D,
      MTLTextureUsageRenderTarget);
  auto depth = bw::platform::macos::TextureAllocation::create(device, budget, kExtent, kExtent,
                                                              MTLPixelFormatDepth32Float);
  NSError* error = nil;
  id<MTLLibrary> library =
      [device newLibraryWithURL:[NSURL fileURLWithPath:@BW_WORLD_SHADER_LIBRARY] error:&error];
  require(library != nil, "Scene replay shader library available");
  constexpr std::array<float, 9> positions{-.9137F, -.8621F, .3127F, .8989F, -.7423F,
                                           .7319F,  -.0731F, .9143F, .5287F};
  const auto model = bw::formats::ModelDefinition::parse(
      bw::resources::ResourceArchive::parse(model_fixture::archive(
          {.positions = positions, .shader = {0x80000010, 1}, .joint_palette = true})),
      domain);
  const auto mesh = renderer.upload_model(model);
  bw::formats::WorldTextureImage image{
      1, 1, bw::formats::WorldTextureFormat::rgba8,
      bw::memory::Vector<std::uint8_t>({192, 128, 64, 255},
                                       bw::memory::Allocator<std::uint8_t>(domain))};
  const auto texture = renderer.upload_world_texture(image);
  renderer.bind_model_textures(mesh, std::span(&texture, 1));
  std::array<bw::render::WorldJointMatrix, 2> joints{
      bw::render::WorldJointMatrix{{{.9731F, .0637F, .013F, .0281F},
                                    {-.0723F, .9817F, .023F, -.0391F},
                                    {.011F, .053F, .9917F, .0731F},
                                    {0, 0, 0, 1}}},
      bw::render::WorldJointMatrix{{{.9173F, -.1037F, .032F, -.0317F},
                                    {.0923F, .9417F, -.072F, .0511F},
                                    {-.013F, .083F, .9713F, -.0219F},
                                    {0, 0, 0, 1}}}};
  bw::render::WorldColorDraw draw{
      .model = mesh,
      .effective_shader_flags = 0x80800210,
      .lighting = bw::render::WorldLightingPolicy::without_direct_lighting,
      .joint_matrices = joints};
  draw.state.min_filter = bw::render::WorldTextureFilter::linear;
  draw.state.mag_filter = bw::render::WorldTextureFilter::linear;
  draw.state.address_u = bw::render::WorldTextureAddress::repeat;
  draw.state.address_v = bw::render::WorldTextureAddress::repeat;
  draw.state.mip_filter = bw::render::WorldMipFilter::linear;
  draw.state.cull = bw::render::WorldCull::none;
  draw.state.depth_compare = bw::render::WorldDepthCompare::greater_equal;
  draw.state.depth_write = true;
  draw.parameters.position_scale = {1, 1, 1, 1};
  draw.parameters.material_color = {1, 1, 1, 1};
  draw.parameters.indirect_color = {1, 1, 1, 1};
  draw.parameters.clip_rows = {{{1.0317F, .0371F, -.023F, .019F},
                                {.0473F, .9813F, .083F, -.031F},
                                {.1937F, .1371F, .047F, .671F},
                                {.137F, -.231F, .471F, 1.51F}}};
  for (std::size_t row = 0; row < 3; ++row) {
    draw.parameters.world_rows.at(row).at(row) = 1;
    draw.parameters.irradiance_coefficients.at(row).at(3) = 1;
  }
  renderer.set_world_draws(std::span(&draw, 1));
  bw::render::WorldViewUpdate view{};
  view.scene_passes = bw::render::WorldViewUpdate::ScenePasses{false, false, true, {}};
  renderer.update_world_view(view);
  const auto ordinary =
      render(renderer, queue, library, SceneTargets{.color = color.get(), .depth = depth.get()});
  require(std::ranges::any_of(ordinary, [](auto value) { return value != 0; }),
          "Varying-depth skinned fixture covers the target");
  view.scene_passes->opaque_preparation = true;
  view.scene_passes->masked_preparation = true;
  renderer.update_world_view(view);
  const auto replayed =
      render(renderer, queue, library, SceneTargets{.color = color.get(), .depth = depth.get()});
  std::size_t differing = 0;
  for (std::size_t pixel = 0; pixel < ordinary.size(); pixel += 4) {
    if (!std::ranges::equal(std::span(ordinary).subspan(pixel, 4),
                            std::span(replayed).subspan(pixel, 4))) {
      ++differing;
    }
  }
  if (differing != 0) {
    throw std::runtime_error("Identical skinned S/T replay changed " + std::to_string(differing) +
                             " pixels without shadows or varying material input");
  }
  renderer.release_model(mesh);
  renderer.release_world_texture(texture);
}
