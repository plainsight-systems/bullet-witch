// Native in-mission Options and display-calibration presentation. A stable
// main-thread owner borrows resource/render capabilities, owns prepared art,
// archive bytes and bounded draw commands, and receives only copied settings.
// No input, profile mutation or audio/display settings authority is retained.
// begin/ready prepare tag22 residency; show performs no uploads/allocations;
// end retires partial or complete residency. Native animated panel positions,
// labels and calibration card come from the original menu data. Costume is the
// deliberate added row and is rendered by the caller's existing costume child.
// Core E.6/R.14 and performance MEM.9/GPU.1 retrieved for both design and
// implementation require bounded views, RAII cleanup and resident textures.
#pragma once
#include <memory>

#include "bw/render/sprites.hpp"
#include "bw/resources/service.hpp"
#include "pause_options.hpp"
namespace bw::game {
class PauseOptionsSurface final : public PauseOptionsOutput {
 public:
  PauseOptionsSurface(resources::ResourceService& resources,
                      render::SpriteOutput& output,
                      resources::ArchiveBytes input_icons,
                      resources::ArchiveBytes menu_text);
  ~PauseOptionsSurface() override;
  PauseOptionsSurface(const PauseOptionsSurface&) = delete;
  PauseOptionsSurface& operator=(const PauseOptionsSurface&) = delete;
  PauseOptionsSurface(PauseOptionsSurface&&) = delete;
  PauseOptionsSurface& operator=(PauseOptionsSurface&&) = delete;
  void begin();
  bool ready();
  void end() noexcept;
  void show(const PauseOptionsPresentation& presentation) override;

 private:
  struct State;
  std::shared_ptr<State> state_;
};
}  // namespace bw::game
