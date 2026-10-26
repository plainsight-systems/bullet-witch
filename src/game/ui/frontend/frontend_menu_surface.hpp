// Localized-text repair: design I.1 and implementation E.4, with MEM.9
// retrieved in both phases. Policy publishes content IDs; the surface owns
// prepared purchased text/art. No English fallback or frame-time decoding.
// Memory migration: native owners propagate the ResourceService CPU domain to
// final pixel/glyph storage and retained presentation state/strings. Owning
// allocators retain the domain through copies and partial-construction cleanup;
// defaults are legacy parser/test compatibility, never exhaustion fallback.
// PNG writes directly to its final owned destination. Fixed draw storage adds
// no per-frame allocations; native texture lifetime stays with SpriteOutput.
// Design R.1 (#rr-raii), implementation E.6 (#re-raii), both-phase MEM.6
// (guidelines/memory/MEM.6-inject-allocators-behind-one-interface.md) and MEM.9
// (guidelines/memory/MEM.9-embedded-allocate-at-init.md) retain owners and
// fixed presentation capacity. Pinned source/external libpng exclusions are
// recorded in research/engine/subsystems/memory-image-presentation.md. Concrete
// game menu resource composition; see frontend-presentation.md. Borrows
// ResourceService and SpriteOutput, both outliving this object. Owns exclusive
// tag 12, retained archive leases and renderer texture handles. Algorithm:
// request groups -> parse original sprite IDs/font -> upload once -> render
// bounded ordered quads -> clear commands/release textures and interests.
// Main-thread only; begin/end are idempotent lifecycle resets. Failures throw;
// destruction releases partially constructed assets. show borrows strings only
// for its call and allocates no command storage. Setup cost is decoded pixels;
// each frame is O(displayed glyphs + sprites), bounded to 511 commands. Design
// MCP E.6 (#re-raii) owns partial cleanup; MEM.9
// (guidelines/memory/MEM.9-embedded-allocate-at-init.md) fixes frame storage.
#pragma once
#include <memory>

#include "bw/content/resources/service.hpp"
#include "bw/render/output/sprites.hpp"
#include "menu_surface.hpp"
namespace bw::game {
class FrontendMenuSurface final : public MenuSurface {
 public:
  FrontendMenuSurface(resources::ResourceService& resources,
                      render::SpriteOutput& renderer,
                      resources::ArchiveBytes menu_text,
                      resources::ArchiveBytes input_icons);
  ~FrontendMenuSurface() override;
  FrontendMenuSurface(const FrontendMenuSurface&) = delete;
  FrontendMenuSurface& operator=(const FrontendMenuSurface&) = delete;
  FrontendMenuSurface(FrontendMenuSurface&&) = delete;
  FrontendMenuSurface& operator=(FrontendMenuSurface&&) = delete;
  void begin() override;
  // Retain only pause/costume/font groups while the gameplay HUD is resident.
  // Title and stage-selection assets are released, not duplicated or loaded
  // against a larger global texture budget. Same ready/end lifecycle.
  void begin_pause();
  // Pump encoded resources and construct textures before reporting readiness.
  bool ready() override;
  void show(const MenuPresentation& presentation) override;
  // Replace sprite commands with a bounded lower-corner text overlay only.
  // Prepared font resources must remain ready; empty text clears the overlay.
  // I.1 and MEM.9 preserve explicit publication and fixed command storage.
  void show_status(std::string_view text);
  void end() noexcept override;

 private:
  struct State;
  std::shared_ptr<State> state_;
};
}  // namespace bw::game
