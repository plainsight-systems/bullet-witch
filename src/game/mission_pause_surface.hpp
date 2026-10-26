// Concrete pause presentation composition with mutually exclusive residency.
// The stable owner borrows ResourceService, SpriteOutput and the existing menu
// surface; all outlive it. Root/help/options resources are private members.
// activate retires previous publication before acquiring the next view, keeping
// the original bounded texture budget alongside the gameplay HUD. Failure
// leaves no ready view; destructors unwind partial loads. show never
// uploads/opens files. Constructor copies encoded icon/menu inputs once using
// their source allocator. All calls run on the presentation thread. Core
// I.1/E.6 and performance MEM.9, retrieved in both phases, enforce explicit
// state and ordered cleanup. Details: native-pause-menu.md and
// mission_pause_presentation.hpp.
#pragma once
#include "controller_options_surface.hpp"
#include "mission_pause_presentation.hpp"
#include "pause_help_surface.hpp"
#include "pause_menu_surface.hpp"
#include "pause_options_surface.hpp"
#include "startup_menu_surface.hpp"

namespace bw::game {
class MissionPauseSurface final : public MissionPauseOutput {
 public:
  MissionPauseSurface(resources::ResourceService& resources,
                      render::SpriteOutput& output,
                      StartupMenuSurface& auxiliary,
                      resources::ArchiveBytes input_icons,
                      resources::ArchiveBytes menu_text);
  ~MissionPauseSurface() override;
  MissionPauseSurface(const MissionPauseSurface&) = delete;
  MissionPauseSurface& operator=(const MissionPauseSurface&) = delete;
  MissionPauseSurface(MissionPauseSurface&&) = delete;
  MissionPauseSurface& operator=(MissionPauseSurface&&) = delete;
  bool activate(MissionPauseView view) override;
  const std::array<unsigned, help_topic_count>& help_line_counts()
      const override;
  void show(const PauseMenuPresentation& view) override;
  void show(const PauseHelpPresentation& view) override;
  void show(const PauseOptionsPresentation& view) override;
  void show(const ControllerOptionsPresentation& view) override;
  void show(const MenuPresentation& view) override;
  void end() noexcept;

 private:
  void require_view(MissionPauseView view) const;
  StartupMenuSurface& auxiliary_;
  PauseMenuSurface root_;
  PauseHelpSurface help_;
  PauseOptionsSurface options_;
  ControllerOptionsSurface controllers_;
  MissionPauseView view_{MissionPauseView::none};
};
}  // namespace bw::game
