// Switch native pause resource owners only at explicit view boundaries. The
// previous owner's commands retire before texture release; a failed candidate
// remains unready and destructible. Publication checks the admitted view rather
// than silently drawing another task's assets. Both-phase Core E.6 (#re-raii)
// and performance MEM.9 (guidelines/memory/MEM.9-embedded-allocate-at-init.md)
// keep cleanup ordered and ordinary frames free of resource work.
#include "mission_pause_surface.hpp"

#include <stdexcept>
#include <utility>

namespace bw::game {
MissionPauseSurface::MissionPauseSurface(resources::ResourceService& resources,
                                         render::SpriteOutput& output,
                                         StartupMenuSurface& auxiliary,
                                         resources::ArchiveBytes input_icons,
                                         resources::ArchiveBytes menu_text)
    : auxiliary_(auxiliary),
      root_(resources, output),
      help_(resources, output, input_icons),
      options_(resources, output, input_icons, menu_text),
      controllers_(resources, output, std::move(input_icons),
                   std::move(menu_text)) {}
MissionPauseSurface::~MissionPauseSurface() { end(); }
void MissionPauseSurface::end() noexcept {
  switch (view_) {
    case MissionPauseView::root:
      root_.end();
      break;
    case MissionPauseView::help:
      help_.end();
      break;
    case MissionPauseView::options:
      options_.end();
      break;
    case MissionPauseView::controls:
      controllers_.end();
      break;
    case MissionPauseView::costume:
    case MissionPauseView::developer:
      auxiliary_.end();
      break;
    case MissionPauseView::none:
      break;
  }
  view_ = MissionPauseView::none;
}
bool MissionPauseSurface::activate(MissionPauseView view) {
  if (view_ == view) {
    return false;
  }
  end();
  // Record the candidate before setup so unwinding retires partial assets.
  view_ = view;
  try {
    switch (view) {
      case MissionPauseView::none:
        break;
      case MissionPauseView::root:
        root_.begin();
        while (!root_.ready()) {
        }
        break;
      case MissionPauseView::help:
        help_.prepare();
        while (!help_.ready()) {
        }
        break;
      case MissionPauseView::options:
        options_.begin();
        while (!options_.ready()) {
        }
        break;
      case MissionPauseView::controls:
        controllers_.begin();
        while (!controllers_.ready()) {
        }
        break;
      case MissionPauseView::costume:
      case MissionPauseView::developer:
        auxiliary_.begin_pause();
        while (!auxiliary_.ready()) {
        }
        break;
    }
  } catch (...) {
    end();
    throw;
  }
  return true;
}
void MissionPauseSurface::require_view(MissionPauseView view) const {
  if (view_ != view) {
    throw std::logic_error("Pause view is not resident");
  }
}
const std::array<unsigned, help_topic_count>&
MissionPauseSurface::help_line_counts() const {
  require_view(MissionPauseView::help);
  return help_.line_counts();
}
void MissionPauseSurface::show(const PauseMenuPresentation& view) {
  require_view(MissionPauseView::root);
  root_.show(view);
}
void MissionPauseSurface::show(const PauseHelpPresentation& view) {
  require_view(MissionPauseView::help);
  help_.show(view);
}
void MissionPauseSurface::show(const PauseOptionsPresentation& view) {
  require_view(MissionPauseView::options);
  options_.show(view);
}
void MissionPauseSurface::show(const ControllerOptionsPresentation& view) {
  require_view(MissionPauseView::controls);
  controllers_.show(view);
}
void MissionPauseSurface::show(const MenuPresentation& view) {
  if (view_ != MissionPauseView::costume &&
      view_ != MissionPauseView::developer) {
    throw std::logic_error("Auxiliary pause view is not resident");
  }
  auxiliary_.show(view);
}
}  // namespace bw::game
