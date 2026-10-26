// Transition-only residency publication. Record the candidate before begin so
// exceptions unwind partially prepared state. show requires the exact admitted
// view; it cannot silently draw a different task's resources. Core I.1 and
// MEM.9, read for both phases, retain the header's explicit lifetime and
// bounded ordinary-frame work.
#include "frontend_options_surface.hpp"

#include <stdexcept>
#include <utility>
namespace bw::game {
FrontendOptionsSurface::FrontendOptionsSurface(
    resources::ResourceService& resources, render::SpriteOutput& output,
    resources::ArchiveBytes icons, resources::ArchiveBytes text)
    : options_(resources, output, icons, text, OptionsContext::frontend),
      controls_(resources, output, std::move(icons), std::move(text)) {}
FrontendOptionsSurface::~FrontendOptionsSurface() { end(); }
void FrontendOptionsSurface::end() noexcept {
  if (view_ == FrontendOptionsView::options) options_.end();
  if (view_ == FrontendOptionsView::controls) controls_.end();
  view_ = FrontendOptionsView::none;
}
void FrontendOptionsSurface::activate(FrontendOptionsView view) {
  if (view_ == view) return;
  end();
  view_ = view;
  try {
    if (view_ == FrontendOptionsView::options) options_.begin();
    if (view_ == FrontendOptionsView::controls) controls_.begin();
  } catch (...) {
    end();
    throw;
  }
}
bool FrontendOptionsSurface::ready() {
  switch (view_) {
    case FrontendOptionsView::options:
      return options_.ready();
    case FrontendOptionsView::controls:
      return controls_.ready();
    case FrontendOptionsView::none:
      return false;
  }
  throw std::logic_error("Invalid frontend Options view");
}
void FrontendOptionsSurface::show(const PauseOptionsPresentation& view) {
  if (view_ != FrontendOptionsView::options) {
    throw std::logic_error("Options art not admitted");
  }
  options_.show(view);
}
void FrontendOptionsSurface::show(const ControllerOptionsPresentation& view) {
  if (view_ != FrontendOptionsView::controls) {
    throw std::logic_error("Controller art not admitted");
  }
  controls_.show(view);
}
}  // namespace bw::game
