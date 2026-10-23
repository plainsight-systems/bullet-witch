// Concrete game menu resource composition; see frontend-presentation.md.
// Borrows ResourceService and SpriteOutput, both outliving this object. Owns
// exclusive tag 12, retained archive leases and renderer texture handles.
// Algorithm: request groups -> parse original sprite IDs/font -> upload once ->
// render bounded ordered quads -> clear commands/release textures and
// interests. Main-thread only; begin/end are idempotent lifecycle resets.
// Failures throw; destruction releases partially constructed assets. show
// borrows strings only for its call and allocates no command storage. Setup
// cost is decoded pixels; each frame is O(displayed glyphs + sprites), bounded
// to 511 commands. Design MCP E.6 (#re-raii) owns partial cleanup; MEM.9
// (guidelines/memory/MEM.9-embedded-allocate-at-init.md) fixes frame storage.
#pragma once
#include <memory>

#include "bw/render/sprites.hpp"
#include "bw/resources/service.hpp"
#include "menu_surface.hpp"
namespace bw::game {
class StartupMenuSurface final : public MenuSurface {
 public:
  StartupMenuSurface(resources::ResourceService& resources,
                     render::SpriteOutput& renderer);
  ~StartupMenuSurface() override;
  StartupMenuSurface(const StartupMenuSurface&) = delete;
  StartupMenuSurface& operator=(const StartupMenuSurface&) = delete;
  StartupMenuSurface(StartupMenuSurface&&) = delete;
  StartupMenuSurface& operator=(StartupMenuSurface&&) = delete;
  void begin() override;
  bool ready() override;
  void show(const MenuPresentation& presentation) override;
  void end() noexcept override;

 private:
  struct State;
  std::unique_ptr<State> state_;
};
}  // namespace bw::game
