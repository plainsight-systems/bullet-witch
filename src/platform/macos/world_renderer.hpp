// The presentation composition now renders scene-linear HDR, then delegates
// luminance, bloom and display transfer to the native WorldPostprocess owner.
// Authored output parameters cross the common WorldOutput interface; no UI
// texture is tone mapped. The caller completes each buffer before owner reuse.
// Native stage geometry, texture residency and selected original surface/depth
// passes. Construction retains device/queue and injected CPU/device
// owners. upload copies buffers and bounded metadata transactionally; failure
// does not replace existing identities. get status never implies original color
// readiness. draw_depth takes explicitly selected draw ordinals and shader
// constants plus caller-owned matching R32Float/Depth32Float attachments; it
// validates the full selection, encodes in order, commits and waits before
// returning. No implicit group selection, gameplay camera, lighting or
// color-material substitution. Destruction/release occur only outside a draw;
// all calls are exclusive-owner. Borrowed attachments survive the synchronous
// call. Setup O(bytes+draws), each draw O(selected records) without geometry
// copies. Native pipeline/command metadata remain external. See
// world-renderer-foundation.md. Both MCP phases: E.6 (#re-raii) transactional
// owners and GPU.1 (guidelines/gpu/GPU.1-keep-data-on-device.md) retained
// geometry. Missing authored LENV binds the original lazily created
// opaque-black one-texel cube; explicit invalid texture bindings
// still fail. One accounted device allocation is retained across submissions
// and retired when the last model is released or the renderer is destroyed,
// after GPU completion.
#pragma once

#import <Metal/Metal.h>

#include <memory>
#include <span>

#include "bw/memory/device_budget.hpp"
#include "bw/render/world.hpp"

namespace bw::platform::macos {
class WorldRenderer final : public render::WorldOutput {
 public:
  WorldRenderer(id<MTLDevice> device, id<MTLCommandQueue> queue,
                const std::shared_ptr<memory::Domain>& cpu_domain,
                std::shared_ptr<memory::DeviceBudget> device_budget);
  ~WorldRenderer() override;
  WorldRenderer(const WorldRenderer&) = delete;
  WorldRenderer& operator=(const WorldRenderer&) = delete;
  WorldRenderer(WorldRenderer&&) = delete;
  WorldRenderer& operator=(WorldRenderer&&) = delete;
  render::WorldModelHandle upload_model(
      const formats::ModelDefinition& model) override;
  render::WorldTextureHandle upload_world_texture(
      const formats::WorldTextureImage& image) override;
  void release_world_texture(
      render::WorldTextureHandle texture) noexcept override;
  void bind_model_textures(
      render::WorldModelHandle model,
      std::span<const render::WorldTextureHandle> textures) override;
  render::WorldGeometryStatus geometry_status(
      render::WorldModelHandle model) const override;
  void release_model(render::WorldModelHandle model) noexcept override;
  void set_world_draws(std::span<const render::WorldColorDraw> draws) override;
  void clear_world_draws() noexcept override;
  bool has_color_draws() const noexcept;
  // Borrowed encoder uses selected BGRA8 or RGBA16Float color and Depth32. The
  // caller commits and retires GPU work before another owner operation
  // (StartupHost waits).
  void encode_color(id<MTLRenderCommandEncoder> encoder, id<MTLLibrary> library,
                    float presentation_opacity = 1.0F,
                    MTLPixelFormat format = MTLPixelFormatBGRA8Unorm);
  void set_world_output_parameters(
      const render::WorldOutputParameters& parameters) override;
  // Encode scene HDR and recovered output passes before opening the display
  // encoder. The caller completes the command before the next submission.
  // Scene draws require greater/greater-equal depth with near=1, far=0;
  // non-writing always draws support the sky. Display viewport extent must
  // equal the supplied width/height for original fullscreen sample mapping.
  void prepare_presentation(id<MTLCommandBuffer> command,
                            id<MTLLibrary> library, std::uint32_t width,
                            std::uint32_t height);
  void encode_presentation(id<MTLRenderCommandEncoder> encoder,
                           id<MTLLibrary> library, float opacity);
  // Original static depth equations; caller owns pass policy and visible
  // diagnostics.
  void draw_depth(render::WorldModelHandle model,
                  std::span<const std::size_t> draws,
                  const render::WorldDepthParameters& parameters,
                  id<MTLLibrary> library, id<MTLTexture> color,
                  id<MTLTexture> depth);

 private:
  struct State;
  std::shared_ptr<State> state_;
};
}  // namespace bw::platform::macos
