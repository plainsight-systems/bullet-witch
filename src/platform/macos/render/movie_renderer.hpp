// Retained movie pixels and their native draw pipeline. Upload validates a
// complete BGRA frame, prepares changed-size storage, then publishes; existing
// same-size storage updates in place after the compositor's synchronous fence.
// encode borrows an encoder for this call only and aspect-fits into the given
// output dimensions. It does not submit, own a clock, service audio or render
// world/UI. Use on one main thread, never during an outstanding composition.
// Upload costs width*height*4 bytes; encode reuses resident texture/pipeline.
// Both design and implementation retrieved Core R.1 (#rr-raii), I.11
// (#ri-raw), performance COPY.5 (guidelines/copy-move/
// COPY.5-resource-owning-types-are-move-only.md), and GPU.1
// (guidelines/gpu/GPU.1-keep-data-on-device.md): stable resource owners,
// explicit borrowed encoders, no intermediate CPU copies. Native ARC handles
// and budgeted allocations release automatically after synchronous completion.
#pragma once
#import <Metal/Metal.h>

#include <memory>

#include "bw/video/decoder.hpp"
#include "texture_allocation.hpp"
namespace bw::platform::macos {
class MovieRenderer final {
 public:
  MovieRenderer(id<MTLDevice> device, id<MTLLibrary> library,
                std::shared_ptr<memory::DeviceBudget> budget);
  ~MovieRenderer() = default;
  MovieRenderer(const MovieRenderer&) = delete;
  MovieRenderer& operator=(const MovieRenderer&) = delete;
  MovieRenderer(MovieRenderer&&) = delete;
  MovieRenderer& operator=(MovieRenderer&&) = delete;
  void upload(const video::VideoFrame& frame);
  void clear() noexcept;
  void encode(id<MTLRenderCommandEncoder> encoder, MTLSize output_extent,
              float opacity) const;

 private:
  id<MTLDevice> device_;
  std::shared_ptr<memory::DeviceBudget> budget_;
  id<MTLRenderPipelineState> pipeline_;
  TextureAllocation video_;
};
}  // namespace bw::platform::macos
