// Native UI resource/submission implementation; see sprite_renderer.hpp.
// The source pass remains straight-alpha or additive after movie/world output.
// Validation and candidate upload precede publication; release removes queued
// users. All GPU reuse occurs after the compositor's synchronous completion.
// Both design and implementation retrieved Core R.1 (#rr-raii), I.11
// (#ri-raw), performance COPY.5 (guidelines/copy-move/
// COPY.5-resource-owning-types-are-move-only.md), and GPU.1
// (guidelines/gpu/GPU.1-keep-data-on-device.md): stable resource owners,
// explicit borrowed encoders, no intermediate CPU copies. Native ARC handles
// and budgeted allocations release automatically after synchronous completion.
#include "sprite_renderer.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>
#include "bw/content/formats/texture_mips.hpp"
namespace bw::platform::macos {
namespace {
constexpr std::size_t kTextureByteBudget = std::size_t{128} * 1024 * 1024;
struct SpriteParameters {
  std::array<float, 4> rectangle;
  std::array<float, 4> uv;
  std::array<float, 4> color;
  std::array<float, 4> sampling;
  std::array<float, 4> transform;
};
bool valid_rectangle(const render::Rect& rect) {
  return std::isfinite(rect.x) && std::isfinite(rect.y) && std::isfinite(rect.width) &&
         std::isfinite(rect.height) && rect.width > 0 && rect.height > 0 &&
         std::isfinite(rect.x + rect.width) && std::isfinite(rect.y + rect.height);
}
bool unit_value(float value) { return std::isfinite(value) && value >= 0 && value <= 1; }
}  // namespace
SpriteRenderer::SpriteRenderer(id<MTLDevice> native_device, id<MTLLibrary> library,
                               std::shared_ptr<memory::DeviceBudget> budget, bool generate_mips)
    : device(native_device), device_budget(std::move(budget)), texture_mips(generate_mips) {
  if (device == nil || library == nil || !device_budget) {
    throw std::invalid_argument("Missing sprite renderer dependency");
  }
  NSError* error = nil;
  MTLRenderPipelineDescriptor* descriptor = [[MTLRenderPipelineDescriptor alloc] init];
  descriptor.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
  // Sprites composite straight-alpha color after the opaque movie pass.
  descriptor.vertexFunction = [library newFunctionWithName:@"sprite_vertex"];
  descriptor.fragmentFunction = [library newFunctionWithName:@"sprite_fragment"];
  descriptor.colorAttachments[0].blendingEnabled = YES;
  descriptor.colorAttachments[0].sourceRGBBlendFactor = MTLBlendFactorSourceAlpha;
  descriptor.colorAttachments[0].destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
  descriptor.colorAttachments[0].sourceAlphaBlendFactor = MTLBlendFactorOne;
  descriptor.colorAttachments[0].destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
  sprite_pipeline = [device newRenderPipelineStateWithDescriptor:descriptor error:&error];
  if (sprite_pipeline == nil) {
    throw std::runtime_error("Could not initialize Metal sprite pipeline");
  }
  descriptor.colorAttachments[0].destinationRGBBlendFactor = MTLBlendFactorOne;
  descriptor.colorAttachments[0].sourceAlphaBlendFactor = MTLBlendFactorSourceAlpha;
  descriptor.colorAttachments[0].destinationAlphaBlendFactor = MTLBlendFactorOne;
  sprite_additive_pipeline = [device newRenderPipelineStateWithDescriptor:descriptor error:&error];
  if (sprite_additive_pipeline == nil) {
    throw std::runtime_error("Could not initialize additive sprite pipeline");
  }
}
render::TextureHandle SpriteRenderer::upload_sprite(std::uint32_t width, std::uint32_t height,
                                                    MTLPixelFormat format, std::uint32_t mip_levels,
                                                    std::span<const std::uint8_t> pixels) {
  auto& state = *this;
  const auto slot = std::ranges::find_if(
      textures, [](const auto& texture) { return texture.native.get() == nil; });
  if (slot == textures.end() || pixels.size() > kTextureByteBudget - texture_bytes ||
      next_texture == std::numeric_limits<std::uint64_t>::max()) {
    throw std::runtime_error("Sprite texture residency limit exceeded");
  }
  @autoreleasepool {
    auto texture = TextureAllocation::create(state.device, state.device_budget, width, height,
                                             format, MTLTextureType2D, MTLTextureUsageShaderRead,
                                             {.count = mip_levels});
    texture.upload(pixels);
    // Publish only after the entire candidate chain has uploaded successfully.
    slot->handle = {next_texture++};
    slot->native = std::move(texture);
    slot->bytes = pixels.size();
    texture_bytes += pixels.size();
    return slot->handle;
  }
}
render::TextureHandle SpriteRenderer::upload_texture(std::uint32_t width, std::uint32_t height,
                                                     std::span<const std::uint8_t> rgba) {
  return this->upload_sprite(width, height, MTLPixelFormatRGBA8Unorm, 1, rgba);
}
render::TextureHandle SpriteRenderer::upload_texture(const formats::WorldTextureImage& image) {
  if (image.shape != formats::WorldTextureShape::two_dimensional || image.mip_levels != 1 ||
      (image.format != formats::WorldTextureFormat::rgba8 &&
       image.format != formats::WorldTextureFormat::rgba4 &&
       image.format != formats::WorldTextureFormat::rgba16_float)) {
    throw std::invalid_argument("Invalid decoded DDS sprite texture");
  }
  std::optional<formats::WorldTextureImage> chain;
  if (this->texture_mips) {
    chain = formats::generate_texture_mips(image, image.pixels.get_allocator());
  }
  const auto& prepared = chain ? chain.value() : image;
  const auto format = prepared.format == formats::WorldTextureFormat::rgba16_float
                          ? MTLPixelFormatRGBA16Float
                          : MTLPixelFormatRGBA8Unorm;
  return this->upload_sprite(prepared.width, prepared.height, format, prepared.mip_levels,
                             prepared.pixels);
}
void SpriteRenderer::release_texture(render::TextureHandle texture) noexcept {
  auto& state = *this;
  const auto slot = std::ranges::find_if(state.textures, [texture](const auto& entry) {
    return entry.handle == texture && entry.native.get() != nil;
  });
  if (slot == state.textures.end()) {
    return;
  }
  state.texture_bytes -= slot->bytes;
  *slot = {};
  // Remove queued users of the retired identity before the next draw.
  auto commands = std::span(state.sprites).first(state.sprite_count);
  const auto removed = std::ranges::remove_if(
      commands, [texture](const auto& entry) { return entry.sprite.texture == texture; });
  state.sprite_count -= static_cast<std::size_t>(removed.size());
}
void SpriteRenderer::set_sprites(std::span<const render::Sprite> sprites) {
  submit_sprites(sprites, 0);
}
void SpriteRenderer::append_sprites(std::span<const render::Sprite> sprites) {
  submit_sprites(sprites, sprite_count);
}
// Both-phase I.1 and MEM.9: replacement and append share the complete validator;
// fixed storage commits only after every suffix identity/geometry is accepted.
void SpriteRenderer::submit_sprites(std::span<const render::Sprite> sprites,
                                    std::size_t retained_count) {
  if (sprites.size() > render::kMaximumSprites - retained_count) {
    throw std::runtime_error("Sprite command limit exceeded");
  }
  auto& state = *this;
  // Validate and resolve every texture before mutating the previous submission.
  std::array<std::size_t, render::kMaximumSprites> slots{};
  std::size_t index = 0;
  for (const auto& sprite : sprites) {
    const auto& uv = sprite.uv;
    const auto& color = sprite.color;
    if (!std::isfinite(sprite.rotation_radians) || !std::isfinite(sprite.rotation_origin_x) ||
        !std::isfinite(sprite.rotation_origin_y) || !valid_rectangle(sprite.destination) ||
        !valid_rectangle(uv) || !unit_value(uv.x) || !unit_value(uv.y) ||
        !unit_value(uv.x + uv.width) || !unit_value(uv.y + uv.height) || !unit_value(color.red) ||
        !unit_value(color.green) || !unit_value(color.blue) || !unit_value(color.alpha) ||
        sprite.channel > render::TextureChannel::alpha ||
        sprite.blend > render::SpriteBlend::additive) {
      throw std::runtime_error("Invalid sprite geometry or color");
    }
    const auto texture = std::ranges::find_if(state.textures, [&sprite](const auto& entry) {
      return entry.handle == sprite.texture && entry.native.get() != nil;
    });
    if (texture == state.textures.end()) {
      throw std::runtime_error("Unknown sprite texture");
    }
    slots.at(index++) = static_cast<std::size_t>(texture - state.textures.begin());
  }
  // Commit the complete command list into fixed storage after validation succeeds.
  index = 0;
  for (const auto& sprite : sprites) {
    state.sprites.at(retained_count + index) = {.sprite = sprite, .texture_slot = slots.at(index)};
    ++index;
  }
  state.sprite_count = retained_count + sprites.size();
}
void SpriteRenderer::clear_sprites() noexcept { this->sprite_count = 0; }

void SpriteRenderer::encode(id<MTLRenderCommandEncoder> encoder, MTLViewport viewport,
                            float opacity) const {
  // Overlay ordered quads in the shared logical canvas with the same fade.
  if (sprite_count != 0) {
    [encoder setViewport:viewport];
    for (const auto& submitted : std::span(sprites).first(sprite_count)) {
      const auto& sprite = submitted.sprite;
      [encoder setRenderPipelineState:sprite.blend == render::SpriteBlend::additive
                                          ? sprite_additive_pipeline
                                          : sprite_pipeline];
      const SpriteParameters parameters{
          .rectangle = {sprite.destination.x, sprite.destination.y, sprite.destination.width,
                        sprite.destination.height},
          .uv = {sprite.uv.x, sprite.uv.y, sprite.uv.width, sprite.uv.height},
          .color = {sprite.color.red * opacity, sprite.color.green * opacity,
                    sprite.color.blue * opacity, sprite.color.alpha},
          .sampling = {static_cast<float>(sprite.channel), 0, 0, 0},
          .transform = {std::cos(sprite.rotation_radians), std::sin(sprite.rotation_radians),
                        sprite.rotation_origin_x, sprite.rotation_origin_y}};
      [encoder setVertexBytes:&parameters length:sizeof(parameters) atIndex:0];
      [encoder setFragmentBytes:&parameters length:sizeof(parameters) atIndex:0];
      [encoder setFragmentTexture:textures.at(submitted.texture_slot).native.get() atIndex:0];
      [encoder drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4];
    }
  }
}
}  // namespace bw::platform::macos
