// Native movie resource and encoding implementation; see movie_renderer.hpp.
// Complete replacement storage is published after upload succeeds. Encoding
// borrows resident storage; the final compositor owns submission/completion.
// Both design and implementation retrieved Core R.1 (#rr-raii), I.11
// (#ri-raw), performance COPY.5 (guidelines/copy-move/
// COPY.5-resource-owning-types-are-move-only.md), and GPU.1
// (guidelines/gpu/GPU.1-keep-data-on-device.md): stable resource owners,
// explicit borrowed encoders, no intermediate CPU copies. Native ARC handles
// and budgeted allocations release automatically after synchronous completion.
#include "movie_renderer.hpp"
#include <algorithm>
#include <stdexcept>
#include <utility>
namespace bw::platform::macos {
MovieRenderer::MovieRenderer(id<MTLDevice> device, id<MTLLibrary> library,
                             std::shared_ptr<memory::DeviceBudget> budget)
    : device_(device), budget_(std::move(budget)) {
  if (device == nil || library == nil || !budget_) {
    throw std::invalid_argument("Missing movie renderer dependency");
  }
  NSError* error = nil;
  MTLRenderPipelineDescriptor* descriptor = [[MTLRenderPipelineDescriptor alloc] init];
  descriptor.vertexFunction = [library newFunctionWithName:@"movie_vertex"];
  descriptor.fragmentFunction = [library newFunctionWithName:@"movie_fragment"];
  descriptor.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
  pipeline_ = [device newRenderPipelineStateWithDescriptor:descriptor error:&error];
  if (pipeline_ == nil) throw std::runtime_error("Could not initialize Metal movie pipeline");
}
void MovieRenderer::upload(const video::VideoFrame& frame) {
  constexpr std::uint32_t max_dimension = 8192;
  constexpr std::size_t channels = 4;
  if (frame.width == 0 || frame.height == 0 || frame.width > max_dimension ||
      frame.height > max_dimension ||
      frame.bgra.size() != static_cast<std::size_t>(frame.width) * frame.height * channels) {
    throw std::runtime_error("Invalid BGRA video frame");
  }
  @autoreleasepool {
    if (video_.get() == nil || video_.get().width != frame.width ||
        video_.get().height != frame.height) {
      // Publish resized storage only after its upload succeeds.
      auto replacement = TextureAllocation::create(device_, budget_, frame.width, frame.height,
                                                   MTLPixelFormatBGRA8Unorm);
      replacement.upload(frame.bgra);
      video_ = std::move(replacement);
    } else {
      video_.upload(frame.bgra);
    }
  }
}
void MovieRenderer::clear() noexcept { video_.reset(); }
void MovieRenderer::encode(id<MTLRenderCommandEncoder> encoder, MTLSize output_extent,
                           float opacity) const {
  const double width = static_cast<double>(output_extent.width);
  const double height = static_cast<double>(output_extent.height);
  // Aspect-fit independently of the world/UI logical canvas.
  if (video_.get() != nil) {
    const double video_width = static_cast<double>(video_.get().width);
    const double video_height = static_cast<double>(video_.get().height);
    const double fit = std::min(width / video_width, height / video_height);
    [encoder
        setViewport:MTLViewport{(width - video_width * fit) / 2, (height - video_height * fit) / 2,
                                video_width * fit, video_height * fit, 0, 1}];
    [encoder setRenderPipelineState:pipeline_];
    [encoder setFragmentTexture:video_.get() atIndex:0];
    [encoder setFragmentBytes:&opacity length:sizeof(opacity) atIndex:0];
    [encoder drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4];
  }
}
}  // namespace bw::platform::macos
