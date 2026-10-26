// Concrete frontend Options residency adapter. Borrows resource/render services
// until destruction; owns the original Options and controller surfaces with
// mutually exclusive residency. No menu decisions, settings authority or time
// source live here. Core I.1 (#ri-explicit), MEM.9 (guidelines/memory/
// MEM.9-embedded-allocate-at-init.md), design and implementation: transition
// setup owns allocation, copied observations drive allocation-free drawing.
#pragma once
#include "controller_options_surface.hpp"
#include "frontend_options.hpp"
#include "options_surface.hpp"
namespace bw::game {
class FrontendOptionsSurface final : public FrontendOptionsOutput {
 public:
  FrontendOptionsSurface(resources::ResourceService& resources,
                         render::SpriteOutput& output,
                         resources::ArchiveBytes icons,
                         resources::ArchiveBytes text);
  ~FrontendOptionsSurface() override;
  FrontendOptionsSurface(const FrontendOptionsSurface&) = delete;
  FrontendOptionsSurface& operator=(const FrontendOptionsSurface&) = delete;
  FrontendOptionsSurface(FrontendOptionsSurface&&) = delete;
  FrontendOptionsSurface& operator=(FrontendOptionsSurface&&) = delete;
  void activate(FrontendOptionsView view) override;
  bool ready() override;
  void show(const OptionsPresentation& view) override;
  void show(const ControllerOptionsPresentation& view) override;
  void end() noexcept override;

 private:
  OptionsSurface options_;
  ControllerOptionsSurface controls_;
  FrontendOptionsView view_{FrontendOptionsView::none};
};
}  // namespace bw::game
