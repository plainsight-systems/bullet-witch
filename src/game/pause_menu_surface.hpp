// Original gameplay pause artwork and confirmation presentation. This stable
// main-thread owner borrows ResourceService and SpriteOutput, which must
// outlive it. It owns tag 20, archive leases, decoded font and uploaded texture
// handles; it receives copied transforms and no input, mission or progression
// authority. begin requests resources, ready prepares art once, show emits
// ordered bounded quads, end clears publication and releases its assets. Setup
// failure unwinds partial residency; malformed snapshots throw before replacing
// visible draws. Scaling preserves each sprite's center as the original matrix
// builder does. Setup scales with encoded image bytes, draw with visible
// sprites/glyphs; neither uploads nor ordinary allocations occur in show. Core
// E.6 (#re-raii) and performance MEM.9 (guidelines/memory/
// MEM.9-embedded-allocate-at-init.md), retrieved during design, enforce
// lifecycle cleanup and fixed command capacity. See native-pause-menu.md.
#pragma once
#include <memory>

#include "bw/render/sprites.hpp"
#include "bw/resources/service.hpp"
#include "pause_menu.hpp"

namespace bw::game {
class PauseMenuSurface final : public PauseMenuOutput {
 public:
  PauseMenuSurface(resources::ResourceService& resources,
                   render::SpriteOutput& output);
  ~PauseMenuSurface() override;
  PauseMenuSurface(const PauseMenuSurface&) = delete;
  PauseMenuSurface& operator=(const PauseMenuSurface&) = delete;
  PauseMenuSurface(PauseMenuSurface&&) = delete;
  PauseMenuSurface& operator=(PauseMenuSurface&&) = delete;
  void begin();
  bool ready();
  void show(const PauseMenuPresentation& presentation) override;
  void end() noexcept;

 private:
  struct State;
  std::shared_ptr<State> state_;
};
}  // namespace bw::game
