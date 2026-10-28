// Native ownership for original sprite shader 0. Configure allocates bounded
// CPU batches and one accounted shared GPU buffer. publish validates the whole
// packet and resolved textures before copying; callers may then discard input.
// Encodes ordered source alpha/additive triangles, source fog/ColorRate and
// greater-equal depth without writes. Encoders/library are borrowed. Caller
// completes prior GPU work before
// publication/reconfiguration/clear/destruction. No gameplay, pack lookup,
// simulation, time or resource-registry ownership. Both design/implementation
// corpora: Core I.11 (#ri-raw) names borrows; GPU.1
// (guidelines/gpu/GPU.1-keep-data-on-device.md) retains device storage and
// avoids readback. Source contracts: first-combat-effects.md.
#pragma once
#import <Metal/Metal.h>

#include "bw/memory/device_budget.hpp"
#include "bw/memory/domain.hpp"
#include "bw/render/scene/world.hpp"

namespace bw::platform::macos {
class EffectSpritePass {
 public:
  EffectSpritePass(id<MTLDevice> device,
                   const std::shared_ptr<memory::Domain>& domain,
                   const std::shared_ptr<memory::DeviceBudget>& budget);
  ~EffectSpritePass();
  EffectSpritePass(const EffectSpritePass&) = delete;
  EffectSpritePass& operator=(const EffectSpritePass&) = delete;
  EffectSpritePass(EffectSpritePass&&) = delete;
  EffectSpritePass& operator=(EffectSpritePass&&) = delete;
  void configure(const render::WorldSpriteCapacity& capacity);
  void publish(const render::WorldSpriteUpdate& update,
               std::span<const id<MTLTexture>> textures);
  void encode(id<MTLRenderCommandEncoder> encoder, id<MTLLibrary> library,
              MTLPixelFormat format);
  void clear() noexcept;
  bool empty() const noexcept;

 private:
  struct State;
  std::shared_ptr<State> state_;
};
}  // namespace bw::platform::macos
