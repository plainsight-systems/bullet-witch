// Retained ordered UI output, sibling to world and movie renderers. Texture
// uploads admit complete candidates before publishing handles; submissions
// validate and resolve all slots before replacing the previous fixed list.
// Release removes queued users before reclaiming storage. encode preserves
// submitted order/blend/fade and borrows its encoder; only the compositor
// submits and completes GPU work. Main-thread use after completed composition.
// Bounded 128 textures/128MiB and 511 quads; no per-frame heap allocation.
// Mip policy is immutable construction state; UI bypasses world postprocess.
// Both design and implementation retrieved Core R.1 (#rr-raii), I.11
// (#ri-raw), performance COPY.5 (guidelines/copy-move/
// COPY.5-resource-owning-types-are-move-only.md), and GPU.1
// (guidelines/gpu/GPU.1-keep-data-on-device.md): stable resource owners,
// explicit borrowed encoders, no intermediate CPU copies. Native ARC handles
// and budgeted allocations release automatically after synchronous completion.
#pragma once
#import <Metal/Metal.h>

#include <array>
#include <memory>

#include "bw/render/sprites.hpp"
#include "platform/macos/texture_allocation.hpp"
namespace bw::platform::macos {
class SpriteRenderer final : public render::SpriteOutput {
 public:
  SpriteRenderer(id<MTLDevice> device, id<MTLLibrary> library,
                 std::shared_ptr<memory::DeviceBudget> budget,
                 bool generate_mips);
  ~SpriteRenderer() override = default;
  SpriteRenderer(const SpriteRenderer&) = delete;
  SpriteRenderer& operator=(const SpriteRenderer&) = delete;
  SpriteRenderer(SpriteRenderer&&) = delete;
  SpriteRenderer& operator=(SpriteRenderer&&) = delete;
  render::TextureHandle upload_texture(
      std::uint32_t width, std::uint32_t height,
      std::span<const std::uint8_t> rgba) override;
  render::TextureHandle upload_texture(
      const formats::WorldTextureImage& image) override;
  void release_texture(render::TextureHandle texture) noexcept override;
  void set_sprites(std::span<const render::Sprite> sprites) override;
  void clear_sprites() noexcept override;
  void encode(id<MTLRenderCommandEncoder> encoder, MTLViewport viewport,
              float opacity) const;

 private:
  id<MTLDevice> device;
  std::shared_ptr<memory::DeviceBudget> device_budget;
  id<MTLRenderPipelineState> sprite_pipeline;
  id<MTLRenderPipelineState> sprite_additive_pipeline;
  struct Texture {
    render::TextureHandle handle;
    TextureAllocation native;
    std::size_t bytes{};
  };
  struct SubmittedSprite {
    render::Sprite sprite;
    std::size_t texture_slot{};
  };
  std::array<Texture, render::kMaximumTextures> textures;
  std::array<SubmittedSprite, render::kMaximumSprites> sprites;
  std::size_t sprite_count{};
  std::size_t texture_bytes{};
  std::uint64_t next_texture{1};
  const bool texture_mips;
  render::TextureHandle upload_sprite(std::uint32_t width, std::uint32_t height,
                                      MTLPixelFormat format,
                                      std::uint32_t mip_levels,
                                      std::span<const std::uint8_t> pixels);
};
}  // namespace bw::platform::macos
