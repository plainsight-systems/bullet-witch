// Original mission instruction surface, independent of pause-menu background.
// Borrow resource/device services for the stable owner's lifetime. Own tag26,
// font/system leases, encoded input icons, uploaded textures and fixed prepared
// glyph/command storage. begin requests assets; ready decodes/uploads once;
// show prepares a changed borrowed page then publishes dialog quads; hide
// clears this publication; end retires commands before textures and leases.
// Failures leave destructible partial state. Calls are presentation-thread
// only. Setup O(asset bytes), changed-page shaping O(text bytes), frames
// O(glyphs), no frame allocations. Core R.4 (#rr-ref) and performance MEM.6
// (guidelines/memory/ MEM.6-inject-allocators-behind-one-interface.md) preserve
// explicit borrows and resource-domain allocation. Evidence:
// mission-startup-source-trace.md.
#pragma once
#include "bw/content/resources/service.hpp"
#include "bw/render/output/sprites.hpp"
#include "game/ui/common/menu_prompt.hpp"
#include "mission_message.hpp"
namespace bw::game {
class MissionMessageSurface final : public MissionMessageOutput {
 public:
  MissionMessageSurface(resources::ResourceService& resources,
                        render::SpriteOutput& output,
                        resources::ArchiveBytes input_icons);
  ~MissionMessageSurface() override;
  MissionMessageSurface(const MissionMessageSurface&) = delete;
  MissionMessageSurface& operator=(const MissionMessageSurface&) = delete;
  MissionMessageSurface(MissionMessageSurface&&) = delete;
  MissionMessageSurface& operator=(MissionMessageSurface&&) = delete;
  void begin();
  bool ready();
  // Copy input observation only; no device or binding mutation authority.
  void set_input_device(MenuInputDevice device);
  void show(std::string_view text,
            const MenuDialogPresentation& presentation) override;
  void hide() noexcept override;
  void end() noexcept;

 private:
  struct State;
  std::shared_ptr<State> state_;
};
}  // namespace bw::game
