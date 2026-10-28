// Instance publication uses design I.11 (#ri-raw)/MEM.9 and implementation
// C.85/GPU.1: borrowed inputs, scene-owned membership and retained native
// storage. See animated-instance-publication.md for the complete contract.
// Host is the window/input/audio composition adapter. World, movie and UI
// resources are owned by sibling native renderers; final
// composition/presentation and synchronous GPU retirement belong to
// OutputCompositor. This compatibility facade forwards producer operations
// without retaining a second registry. Both-phase R.1/I.11/COPY.5/GPU.1
// ownership rules below apply to the delegated owners; no scene snapshots or
// mutable compositor capabilities escape. Texture construction policy is
// captured once before any DDS sprite upload. Enabled uploads build a complete
// portable mip candidate, then native residency; video frame uploads remain
// separate. No mutable launch settings are borrowed. Both-phase E.6/I.11 and
// COPY.5 preserve candidate cleanup, bounded borrowing and stable host identity
// (source references below). Mip bytes count against both sprite residency
// limits and complete device accounting. Presentation timing is supplied by the
// engine frame publisher, including while world simulation is paused. No
// target-local wall clock drives exposure. Both-phase Core I.1 (#ri-explicit)
// exposes this dependency; MEM.9
// (guidelines/memory/MEM.9-embedded-allocate-at-init.md) keeps the snapshot
// inline. Native input publishes physical observations only; game mapping owns
// UI/action interpretation and repeats. Focus loss clears held state. Core I.1
// (#ri-explicit) and MEM.9 keep that boundary explicit and allocation-free.
// Physical shoulder/face observations apply both-phase Core I.1 (#ri-explicit):
// the platform publishes sampled positions without interpreting game gestures.
// MEM.9 keeps these copied booleans and edge history inline.
// Component draw replacement applies I.1 (#ri-explicit), C.85
// (#rc-swap-noexcept), and GPU.1 (guidelines/gpu/GPU.1-keep-data-on-device.md):
// prepare discrete changes before nonthrowing publication, retaining unrelated
// state and resident resources. See model-draw-replacement.md; both MCP phases.
// View updates forward atomically to the retained renderer (both-phase
// I.1/C.121 and GPU.1), without copying geometry or allocating host storage.
// Material animation forwards an explicit borrowed constant-update batch
// (both-phase I.1 #ri-explicit and GPU.1 retained-device-data guidance).
// The host preserves the native owner and main-thread access boundary.
// Authored world display settings forward to the same retained renderer used
// by draw(). C.121 (#rh-abstract) makes this adapter obligation explicit;
// GPU.9 (guidelines/gpu/GPU.9-suballocate-and-alias-transient-gpu-memory.md)
// keeps postprocess resources in the existing device pool. Implementation also
// applies C.128 (#rh-override) to the explicit forwarding override.
// World geometry extends the device owner through render/world.hpp. Upload is
// transactional and does not claim original material readiness. E.6 (#re-raii)
// and GPU.1 (guidelines/gpu/GPU.1-keep-data-on-device.md), retrieved in both
// phases, retain immutable native buffers and their charges until retirement.
// Memory ownership: host State/control and path scratch use the injected CPU
// domain. Native texture owners separately admit/reconcile logical and reported
// Metal sizes, retain charges through use, and release after synchronous
// command completion. Existing sprite cap is preserved. Cocoa/pipeline/drawable
// storage remains external. E.6 (#re-raii), SL.con.3 (#rsl-bounds), GPU.1
// (guidelines/gpu/GPU.1-keep-data-on-device.md), COPY.5
// (guidelines/copy-move/COPY.5-resource-owning-types-are-move-only.md) apply in
// both consulted phases. See device-memory-ownership.md.
// InputSource owns physical sampling/edges; game input owns repeat policy.
// Window options request windowed content points, clamped to the
// visible display. Native full screen is the default; construction waits for
// the completed native transition before media can begin, or throws on
// failure/stall. Sprite upload/submission follows render/sprites.hpp; all
// sprites share the video fade. Design MCP R.2 (#rr-use-ptr) uses spans;
// performance MEM.9 (guidelines/memory/MEM.9-embedded-allocate-at-init.md)
// bounds sprite storage. Native startup window, input and media output behind
// one platform-neutral header; see docs/native-startup-host.md.
// Construct/use/destroy on the main thread. All calls are single-owner, not
// thread-safe. The private native owner synchronously retires GPU/audio work
// before destruction. Errors throw; pump returns close requests and copied
// neutral physical observations. Algorithm: pump native events and sample
// controller -> shared game update -> video Output operations. Input edges are
// consumed once per pump. Native callbacks cannot call game objects. Costs
// scale with events/pixels/samples; bounded audio buffering and one retained
// video texture. Design/implementation MCP: cpp-guidelines R.1 (#rr-raii) owns
// paired platform handles; cpp-performance COPY.5 (guidelines/copy-move/
// COPY.5-resource-owning-types-are-move-only.md) fixes callback-owner identity.
#pragma once
#include <cstdint>
#include <memory>

#include "bw/input/input_frame.hpp"
#include "bw/memory/device_budget.hpp"
#include "bw/render/output/sprites.hpp"
#include "bw/render/scene/world.hpp"
#include "bw/timing/frame_clock.hpp"
#include "bw/video/output.hpp"

namespace bw::platform {
inline constexpr std::uint32_t kDefaultWindowWidth = 1280;
inline constexpr std::uint32_t kDefaultWindowHeight = 720;
struct WindowOptions {
  std::uint32_t width{kDefaultWindowWidth};
  std::uint32_t height{kDefaultWindowHeight};
  bool fullscreen{true};
};
class WindowHost final : public video::Output,
                         public render::SpriteOutput,
                         public render::WorldOutput {
 public:
  WindowHost(const std::shared_ptr<memory::Domain>& cpu_domain,
             std::shared_ptr<memory::DeviceBudget> device_budget,
             WindowOptions options = {}, bool texture_mips = false);
  ~WindowHost() override;
  WindowHost(const WindowHost&) = delete;
  WindowHost& operator=(const WindowHost&) = delete;
  WindowHost(WindowHost&&) = delete;
  WindowHost& operator=(WindowHost&&) = delete;
  // Consume physical edges once per pump. No game actions or input clocks live
  // here; game input interprets this copied sample with its supplied timing.
  input::InputFrame pump(bool window_close_enabled);
  // Publish before drawing; presentation time continues through world pause.
  void set_frame_timing(timing::FrameTiming frame);
  // Retain uploaded pixels independently of the caller's decoded frame.
  void upload(const video::VideoFrame& frame) override;
  void clear_video() noexcept override;
  // Compose world output, movie, then ordered UI; retire GPU use before return.
  bool draw(float opacity) override;
  video::AudioOutput& audio() noexcept override;
  // Sprite resources and commands share the portable SpriteOutput contract.
  render::TextureHandle upload_texture(
      std::uint32_t width, std::uint32_t height,
      std::span<const std::uint8_t> rgba) override;
  render::TextureHandle upload_texture(
      const formats::WorldTextureImage& image) override;
  void release_texture(render::TextureHandle texture) noexcept override;
  void set_sprites(std::span<const render::Sprite> sprites) override;
  void append_sprites(std::span<const render::Sprite> sprites) override;
  void clear_sprites() noexcept override;
  // Immutable world geometry has device ownership distinct from color
  // readiness.
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
  void set_world_output_parameters(
      const render::WorldOutputParameters& parameters) override;
  void set_world_draws(std::span<const render::WorldColorDraw> draws) override;
  void replace_world_model_draws(
      render::WorldModelHandle previous,
      std::span<const render::WorldColorDraw> replacement) override;
  void add_world_instance_draws(
      std::span<const render::WorldColorDraw> draws) override;
  void remove_world_instance_draws(render::WorldModelHandle model,
                                   std::uint64_t instance) override;
  void update_world_texture_offsets(
      std::span<const render::WorldTextureOffsetUpdate> updates) override;
  void configure_world_sprites(const render::WorldSpriteCapacity&) override;
  void update_world_sprites(const render::WorldSpriteUpdate&) override;
  void update_world_lighting(
      std::span<const render::WorldLightingUpdate> updates) override;
  void update_world_poses(
      std::span<const render::WorldPoseUpdate> updates) override;
  void update_shape_positions(
      std::span<const render::WorldShapePositionUpdate> updates) override;
  void update_world_visibility(
      std::span<const render::WorldVisibilityUpdate> updates) override;
  void update_world_view(const render::WorldViewUpdate& update) override;
  void clear_world_draws() noexcept override;

 private:
  struct State;
  std::shared_ptr<State> state_;
};
}  // namespace bw::platform
