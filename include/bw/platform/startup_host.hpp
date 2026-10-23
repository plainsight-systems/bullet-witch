// Memory ownership: host State/control and path scratch use the injected CPU
// domain. Native texture owners separately admit/reconcile logical and reported
// Metal sizes, retain charges through use, and release after synchronous
// command completion. Existing sprite cap is preserved. Cocoa/pipeline/drawable
// storage remains external. E.6 (#re-raii), SL.con.3 (#rsl-bounds), GPU.1
// (guidelines/gpu/GPU.1-keep-data-on-device.md), COPY.5
// (guidelines/copy-move/COPY.5-resource-owning-types-are-move-only.md) apply in
// both consulted phases. See device-memory-ownership.md.
// Input repeats follow recovered per-bit 0.5s/0.125s arithmetic; pressed
// actions remain edges. Arrow keys/controller directions map to bits 24-27.
// Fixed 32-timer storage applies retrieved MEM.9; E.6 preserves native
// lifetime. Window options request windowed content points, clamped to the
// visible display. Native full screen is the default; construction waits for
// the completed native transition before media can begin, or throws on
// failure/stall. Sprite upload/submission follows render/sprites.hpp; all
// sprites share the video fade. Design MCP R.2 (#rr-use-ptr) uses spans;
// performance MEM.9 (guidelines/memory/MEM.9-embedded-allocate-at-init.md)
// bounds sprite storage. Native startup window, input and media output behind
// one platform-neutral header; see docs/native-startup-host.md.
// Construct/use/destroy on the main thread. All calls are single-owner, not
// thread-safe. The private native owner synchronously retires GPU/audio work
// before destruction. Errors throw; pump returns close requests and freshly
// pressed compatibility action bits. Algorithm: pump native events and sample
// controller -> shared game update -> video Output operations. Input edges are
// consumed once per pump. Native callbacks cannot call game objects. Costs
// scale with events/pixels/samples; bounded audio buffering and one retained
// video texture. Design/implementation MCP: cpp-guidelines R.1 (#rr-raii) owns
// paired platform handles; cpp-performance COPY.5 (guidelines/copy-move/
// COPY.5-resource-owning-types-are-move-only.md) fixes callback-owner identity.
#pragma once
#include <cstdint>
#include <memory>

#include "bw/memory/device_budget.hpp"
#include "bw/render/sprites.hpp"
#include "bw/video/output.hpp"

namespace bw::platform {
struct StartupInput {
  std::uint32_t pressed_actions{};
  std::uint32_t repeated_actions{};
  bool close_requested{};
};
inline constexpr std::uint32_t kDefaultWindowWidth = 1280;
inline constexpr std::uint32_t kDefaultWindowHeight = 720;
struct WindowOptions {
  std::uint32_t width{kDefaultWindowWidth};
  std::uint32_t height{kDefaultWindowHeight};
  bool fullscreen{true};
};
class StartupHost final : public video::Output, public render::SpriteOutput {
 public:
  StartupHost(const std::shared_ptr<memory::Domain>& cpu_domain,
              std::shared_ptr<memory::DeviceBudget> device_budget,
              WindowOptions options = {});
  ~StartupHost() override;
  StartupHost(const StartupHost&) = delete;
  StartupHost& operator=(const StartupHost&) = delete;
  StartupHost(StartupHost&&) = delete;
  StartupHost& operator=(StartupHost&&) = delete;
  StartupInput pump(bool window_close_enabled);
  void upload(const video::VideoFrame& frame) override;
  void clear_video() noexcept override;
  bool draw(float opacity) override;
  video::AudioOutput& audio() noexcept override;
  render::TextureHandle upload_texture(
      std::uint32_t width, std::uint32_t height,
      std::span<const std::uint8_t> rgba) override;
  void release_texture(render::TextureHandle texture) noexcept override;
  void set_sprites(std::span<const render::Sprite> sprites) override;
  void clear_sprites() noexcept override;

 private:
  struct State;
  std::shared_ptr<State> state_;
};
}  // namespace bw::platform
