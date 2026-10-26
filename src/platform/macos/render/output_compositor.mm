// Final Metal submission implementation. The accepted pass/blend/viewport
// order is preserved from WindowHost::draw; only its ownership moves here.
// Output construction follows the explicit frame boundary inspected in pinned
// Doom3 Session.cpp Draw/UpdateScreen; no reference engine behavior is copied.
// Source/reference details are in output-composition.md.
// Both design and implementation retrieved Core R.1 (#rr-raii), I.11
// (#ri-raw), performance COPY.5 (guidelines/copy-move/
// COPY.5-resource-owning-types-are-move-only.md), and GPU.1
// (guidelines/gpu/GPU.1-keep-data-on-device.md): stable resource owners,
// explicit borrowed encoders, no intermediate CPU copies. Native ARC handles
// and budgeted allocations release automatically after synchronous completion.
#include "output_compositor.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
namespace bw::platform::macos {
OutputCompositor::OutputCompositor(id<MTLCommandQueue> commands, id<MTLLibrary> library,
                                   CAMetalLayer* layer, WorldRenderer& world,
                                   const MovieRenderer& movie, const SpriteRenderer& sprites)
    : commands_(commands),
      library_(library),
      layer_(layer),
      world_(world),
      movie_(movie),
      sprites_(sprites) {
  if (commands_ == nil || library_ == nil) {
    throw std::invalid_argument("Missing output compositor dependency");
  }
}
void OutputCompositor::validate(const render::OutputFrame& frame) const {
  if (!std::isfinite(frame.opacity) || frame.opacity < 0 || frame.opacity > 1) {
    throw std::invalid_argument("Invalid output opacity");
  }
  if (frame.timing &&
      (!std::isfinite(frame.timing->presentation_seconds) ||
       frame.timing->presentation_seconds < 0 || !std::isfinite(frame.timing->wall_seconds) ||
       frame.timing->wall_seconds < 0)) {
    throw std::invalid_argument("Invalid output frame timing");
  }
}
bool OutputCompositor::present(const render::OutputFrame& frame) {
  validate(frame);
  @autoreleasepool {
    id<CAMetalDrawable> drawable = [layer_ nextDrawable];
    if (drawable == nil) return false;
    submit(drawable.texture, drawable, frame);
    return true;
  }
}
void OutputCompositor::compose(id<MTLTexture> target, const render::OutputFrame& frame) {
  validate(frame);
  @autoreleasepool {
    submit(target, nil, frame);
  }
}
// One submission owns every consumer through completion. Producer resources
// remain untouched on an unavailable drawable; no command survives this call.
void OutputCompositor::submit(id<MTLTexture> target, id<CAMetalDrawable> drawable,
                              const render::OutputFrame& frame) {
  if (target == nil || target.pixelFormat != MTLPixelFormatBGRA8Unorm ||
      target.device != commands_.device || target.sampleCount != 1 || target.width == 0 ||
      target.height == 0 || target.width > std::numeric_limits<std::uint32_t>::max() ||
      target.height > std::numeric_limits<std::uint32_t>::max()) {
    throw std::invalid_argument("Invalid output target");
  }
  MTLRenderPassDescriptor* pass = [MTLRenderPassDescriptor renderPassDescriptor];
  pass.colorAttachments[0].texture = target;
  pass.colorAttachments[0].loadAction = MTLLoadActionClear;
  pass.colorAttachments[0].storeAction = MTLStoreActionStore;
  pass.colorAttachments[0].clearColor = MTLClearColorMake(0, 0, 0, 1);
  id<MTLCommandBuffer> command = [commands_ commandBuffer];
  if (command == nil) throw std::runtime_error("Could not create output command");
  const double canvas_width = static_cast<double>(target.width);
  const double canvas_height = static_cast<double>(target.height);
  const double canvas_fit =
      std::min(canvas_width / render::kCanvasWidth, canvas_height / render::kCanvasHeight);
  const auto scene_width = static_cast<std::uint32_t>(render::kCanvasWidth * canvas_fit);
  const auto scene_height = static_cast<std::uint32_t>(render::kCanvasHeight * canvas_fit);
  // Fit the logical canvas even when only title/menu sprites are resident.
  // Pixel alignment also preserves the world pass's destination sample offset.
  const MTLViewport canvas_viewport{std::floor((canvas_width - scene_width) / 2),
                                    std::floor((canvas_height - scene_height) / 2),
                                    static_cast<double>(scene_width),
                                    static_cast<double>(scene_height),
                                    0,
                                    1};
  if (world_.has_color_draws()) {
    if (!frame.timing) throw std::logic_error("World presentation has no frame timing");
    world_.prepare_presentation(command, library_, scene_width, scene_height, *frame.timing);
  }
  id<MTLRenderCommandEncoder> encoder = [command renderCommandEncoderWithDescriptor:pass];
  if (encoder == nil) {
    throw std::runtime_error("Could not encode output composition");
  }
  if (world_.has_color_draws()) {
    // Preserve the inspection projection's logical aspect through the same
    // centered canvas fit used by menu presentation; clear retains black bars.
    [encoder setViewport:canvas_viewport];
    world_.encode_presentation(encoder, library_, frame.opacity);
    [encoder endEncoding];
    pass.colorAttachments[0].loadAction = MTLLoadActionLoad;
    pass.depthAttachment.texture = nil;
    encoder = [command renderCommandEncoderWithDescriptor:pass];
    if (encoder == nil) {
      throw std::runtime_error("Could not encode overlays after world presentation");
    }
  }
  movie_.encode(encoder, MTLSizeMake(target.width, target.height, 1), frame.opacity);
  sprites_.encode(encoder, canvas_viewport, frame.opacity);
  [encoder endEncoding];
  if (drawable != nil) [command presentDrawable:drawable];
  [command commit];
  // Retire all GPU consumers before uploads, releases or frame reuse follow.
  [command waitUntilCompleted];
  if (command.status == MTLCommandBufferStatusError) {
    throw std::runtime_error("Metal output command failed");
  }
}
}  // namespace bw::platform::macos
