// Original key-configuration surface. ResourceService and SpriteOutput outlive
// this stable main-thread owner. Prepare authored art/font/localized text and
// one atlas of all original input icons; show consumes copied observations
// only. End releases this view's resources. No Profile/input/timing mutation
// authority. Setup O(asset bytes), frame O(visible glyphs), bounded commands
// and no frame allocations. Core I.1/C.9/E.4 and performance MEM.9, retrieved
// in both phases, preserve responsibility, transaction cleanup and fixed
// runtime work. Source mapping: native-pause/controller-options.md.
#pragma once
#include <memory>

#include "bw/content/resources/service.hpp"
#include "bw/render/output/sprites.hpp"
#include "controller_options.hpp"
namespace bw::game {
class ControllerOptionsSurface final : public ControllerOptionsOutput {
 public:
  ControllerOptionsSurface(resources::ResourceService& resources,
                           render::SpriteOutput& output,
                           resources::ArchiveBytes input_icons,
                           resources::ArchiveBytes menu_text);
  ~ControllerOptionsSurface() override;
  ControllerOptionsSurface(const ControllerOptionsSurface&) = delete;
  ControllerOptionsSurface& operator=(const ControllerOptionsSurface&) = delete;
  ControllerOptionsSurface(ControllerOptionsSurface&&) = delete;
  ControllerOptionsSurface& operator=(ControllerOptionsSurface&&) = delete;
  void begin();
  bool ready();
  void end() noexcept;
  void show(const ControllerOptionsPresentation& presentation) override;

 private:
  struct State;
  std::shared_ptr<State> state_;
};
}  // namespace bw::game
