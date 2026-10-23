// Game front-end presentation capability. The session borrows this object;
// implementations own their assets. begin requests resources; ready observes
// device construction; show borrows presentation strings for that call only;
// end clears commands and releases assets, including partially loaded state.
// Main-thread only. Invalid state/data or resource failure throws. Repeated end
// is harmless. No menu policy or persistence lives here. Cost is bounded by the
// MenuPresentation row capacity and the renderer's 511-command capacity.
// Design MCP E.6 (#re-raii) defines cleanup lifetime; performance MEM.9
// (guidelines/memory/MEM.9-embedded-allocate-at-init.md) bounds frame storage.
#pragma once
#include "menu_flow.hpp"
namespace bw::game {
class MenuSurface {
 public:
  MenuSurface() = default;
  virtual ~MenuSurface() = default;
  MenuSurface(const MenuSurface&) = delete;
  MenuSurface& operator=(const MenuSurface&) = delete;
  MenuSurface(MenuSurface&&) = delete;
  MenuSurface& operator=(MenuSurface&&) = delete;
  virtual void begin() = 0;
  virtual bool ready() = 0;
  virtual void show(const MenuPresentation& presentation) = 0;
  virtual void end() noexcept = 0;
};
}  // namespace bw::game
