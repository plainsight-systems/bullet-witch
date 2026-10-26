// Native sibling output contracts inside the existing memory-tests scenario.
// Authored world geometry, movie pixels and UI quads exercise the production
// compositor without a window. Completed readback establishes layering/fade,
// logical-canvas placement, admission rollback and resource retirement. It is
// not game appearance/audio acceptance. No production readback is introduced.
// Both design/implementation retrieved R.1 (#rr-raii), I.11 (#ri-raw), COPY.5
// (guidelines/copy-move/COPY.5-resource-owning-types-are-move-only.md), GPU.1
// (guidelines/gpu/GPU.1-keep-data-on-device.md): stable native owners outlive
// completion; bounded diagnostic reads inspect actual device output.
#include <array>
#include <cmath>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <type_traits>
#include "bw/formats/model.hpp"
#include "bw/formats/world_texture.hpp"
#include "model_fixture.hpp"
#include "platform/macos/cpu_backing.hpp"
#include "platform/macos/output_compositor.hpp"

namespace {
using bw::platform::macos::MovieRenderer;
using bw::platform::macos::OutputCompositor;
using bw::platform::macos::SpriteRenderer;
using bw::platform::macos::WorldRenderer;
using Pixel = std::array<std::uint8_t, 4>;
static_assert(!std::is_copy_constructible_v<MovieRenderer>);
static_assert(!std::is_move_constructible_v<MovieRenderer>);
static_assert(!std::is_copy_constructible_v<SpriteRenderer>);
static_assert(!std::is_move_constructible_v<SpriteRenderer>);
static_assert(!std::is_copy_constructible_v<OutputCompositor>);
static_assert(!std::is_move_constructible_v<OutputCompositor>);
void require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}
Pixel pixel(id<MTLTexture> target, std::uint32_t x = 64, std::uint32_t y = 64) {
  Pixel result{};
  [target getBytes:result.data()
       bytesPerRow:4
        fromRegion:MTLRegionMake2D(x, y, 1, 1)
       mipmapLevel:0];
  return result;
}
bool near(Pixel actual, Pixel expected) {
  for (std::size_t index = 0; index != actual.size(); ++index) {
    if (std::abs(static_cast<int>(actual.at(index)) - expected.at(index)) > 1) return false;
  }
  return true;
}
// Seed an actual scene consumer so the composition check crosses the world
// postprocess boundary, not just an empty-world branch or fake encoder.
void publish_world(WorldRenderer& world, const std::shared_ptr<bw::memory::Domain>& domain) {
  auto model = bw::formats::ModelDefinition::parse(
      bw::resources::ResourceArchive::parse(model_fixture::archive({.normal_mapped = true})),
      domain);
  const auto mesh = world.upload_model(model);
  bw::formats::WorldTextureImage image{
      1, 1, bw::formats::WorldTextureFormat::rgba8,
      bw::memory::Vector<std::uint8_t>({64, 128, 192, 255},
                                       bw::memory::Allocator<std::uint8_t>(domain))};
  const auto base = world.upload_world_texture(image);
  image.pixels = {128, 128, 255, 255};
  const auto normal = world.upload_world_texture(image);
  const std::array bindings{base, normal, bw::render::WorldTextureHandle{}};
  world.bind_model_textures(mesh, bindings);
  bw::render::WorldColorDraw draw{
      .model = mesh,
      .effective_shader_flags = 0x00e20882,
      .lighting = bw::render::WorldLightingPolicy::without_direct_lighting};
  draw.parameters.position_scale = {1, 1, 1, 1};
  draw.parameters.material_color = {1, 1, 1, 1};
  draw.parameters.indirect_color = {1, 1, 1, 1};
  for (std::size_t index = 0; index < 4; ++index) draw.parameters.clip_rows.at(index).at(index) = 1;
  for (std::size_t index = 0; index < 3; ++index) {
    draw.parameters.world_rows.at(index).at(index) = 1;
    draw.parameters.irradiance_coefficients.at(index).at(3) = 1;
  }
  draw.state.min_filter = bw::render::WorldTextureFilter::linear;
  draw.state.mag_filter = bw::render::WorldTextureFilter::linear;
  draw.state.address_u = bw::render::WorldTextureAddress::repeat;
  draw.state.address_v = bw::render::WorldTextureAddress::repeat;
  draw.state.mip_filter = bw::render::WorldMipFilter::linear;
  draw.state.cull = bw::render::WorldCull::none;
  draw.state.depth_compare = bw::render::WorldDepthCompare::greater_equal;
  draw.state.depth_write = true;
  world.set_world_draws(std::span(&draw, 1));
}
void layer_contract(id<MTLTexture> target, OutputCompositor& output, MovieRenderer& movie,
                    SpriteRenderer& sprites, const std::shared_ptr<bw::memory::Domain>& domain) {
  const bw::render::OutputFrame full{.timing = bw::timing::FrameTiming{}};
  output.compose(target, full);
  const auto world = pixel(target);
  require(world != Pixel{0, 0, 0, 255}, "World output reaches the final target");
  require(pixel(target, 0, 0) == Pixel{0, 0, 0, 255}, "World canvas leaves black letterbox bars");
  bw::video::VideoFrame frame{.width = 1,
                              .height = 1,
                              .bgra = bw::memory::Vector<std::uint8_t>(
                                  {0, 0, 255, 255}, bw::memory::Allocator<std::uint8_t>(domain))};
  movie.upload(frame);
  output.compose(target, full);
  require(pixel(target) == Pixel{0, 0, 255, 255} && pixel(target, 0, 0) == Pixel{0, 0, 255, 255},
          "Movie overlays world using its own aspect ratio");
  const auto green = sprites.upload_texture(1, 1, Pixel{0, 255, 0, 255});
  const auto blue = sprites.upload_texture(1, 1, Pixel{0, 0, 255, 255});
  const std::array quads{
      bw::render::Sprite{
          .texture = green, .destination = {0, 0, 1280, 720}, .color = {1, 1, 1, .5F}},
      bw::render::Sprite{.texture = blue,
                         .destination = {0, 0, 1280, 720},
                         .color = {1, 1, 1, .25F},
                         .blend = bw::render::SpriteBlend::additive}};
  sprites.set_sprites(quads);
  output.compose(target, full);
  require(near(pixel(target), {64, 128, 128, 255}),
          "Ordered alpha then additive UI overlays movie");
  require(pixel(target, 0, 0) == Pixel{0, 0, 255, 255},
          "UI uses the logical canvas, not movie viewport");
  output.compose(target, {.opacity = .5F, .timing = full.timing});
  require(near(pixel(target), {32, 64, 64, 255}), "Global fade applies once to each sibling's RGB");
  // Invalid submissions preserve the prior list; a missing drawable also
  // leaves every sibling resident for retry.
  auto invalid = quads;
  invalid.at(1).texture = {99999};
  bool rejected = false;
  try {
    sprites.set_sprites(invalid);
  } catch (const std::runtime_error&) {
    rejected = true;
  }
  require(rejected && !output.present(full), "Stale UI rejects and absent display target retries");
  output.compose(target, full);
  require(near(pixel(target), {64, 128, 128, 255}),
          "Failed UI replacement preserves prior composition");
  sprites.release_texture(green);
  sprites.release_texture(blue);
  output.compose(target, full);
  require(pixel(target) == Pixel{0, 0, 255, 255},
          "Retired textures remove their queued UI consumers");
  frame.width = 0;
  rejected = false;
  try {
    movie.upload(frame);
  } catch (const std::runtime_error&) {
    rejected = true;
  }
  require(rejected, "Invalid movie replacement is rejected");
  output.compose(target, full);
  require(pixel(target) == Pixel{0, 0, 255, 255},
          "Invalid movie replacement preserves resident pixels");
  movie.clear();
  output.compose(target, full);
  require(pixel(target) == world, "Retiring movie and UI reveals unchanged world output");
}
}  // namespace

void BWTestOutputCompositor(id<MTLDevice> device) {
  auto domain = bw::memory::Domain::create(bw::platform::macos::make_cpu_backing());
  auto budget = bw::memory::DeviceBudget::create(domain);
  {
    id<MTLCommandQueue> queue = [device newCommandQueue];
    NSError* error = nil;
    id<MTLLibrary> library =
        [device newLibraryWithURL:[NSURL fileURLWithPath:@BW_WORLD_SHADER_LIBRARY] error:&error];
    require(library != nil, "Output composition shader library available");
    WorldRenderer world(device, queue, domain, budget);
    MovieRenderer movie(device, library, budget);
    SpriteRenderer sprites(device, library, budget, false);
    OutputCompositor output(queue, library, nil, world, movie, sprites);
    auto target = bw::platform::macos::TextureAllocation::create(
        device, budget, 128, 128, MTLPixelFormatBGRA8Unorm, MTLTextureType2D,
        MTLTextureUsageRenderTarget);
    output.compose(target.get(), {});
    require(pixel(target.get()) == Pixel{0, 0, 0, 255}, "Empty output clears opaque black");
    publish_world(world, domain);
    bool rejected = false;
    try {
      output.compose(target.get(), {});
    } catch (const std::logic_error&) {
      rejected = true;
    }
    require(rejected, "World output requires an explicit engine timing snapshot");
    layer_contract(target.get(), output, movie, sprites, domain);
  }
  require(budget->snapshot().live_resources == 0,
          "Completed output owners retire every device charge");
}
