// Original Help art/text resource owner. ResourceService and SpriteOutput
// outlive this stable main-thread object. Prepare acquires immutable assets;
// ready decodes original font, Unicode markup and semantic icon markers once;
// show substitutes copied action-icon runs and publishes bounded sprites; end
// releases this owner's textures and resource interests. No Profile, input or
// clock authority. Setup is O(content bytes), frames O(prepared glyphs) without
// allocation. Core I.1/C.9 and performance MEM.9
// (guidelines/memory/MEM.9-embedded-allocate-at-init.md), both phases
// consulted. Source, ownership and original content limits:
// native-pause-help.md.
#pragma once
#include <memory>

#include "bw/render/sprites.hpp"
#include "bw/resources/service.hpp"
#include "pause_help.hpp"
namespace bw::game {
class PauseHelpSurface final : public PauseHelpOutput {
 public:
  PauseHelpSurface(resources::ResourceService& resources,
                   render::SpriteOutput& output,
                   resources::ArchiveBytes input_icons);
  ~PauseHelpSurface() override;
  PauseHelpSurface(const PauseHelpSurface&) = delete;
  PauseHelpSurface& operator=(const PauseHelpSurface&) = delete;
  PauseHelpSurface(PauseHelpSurface&&) = delete;
  PauseHelpSurface& operator=(PauseHelpSurface&&) = delete;
  void prepare();
  bool ready();
  const std::array<unsigned, help_topic_count>& line_counts() const;
  void show(const PauseHelpPresentation& presentation) override;
  void end() noexcept;

 private:
  struct State;
  std::shared_ptr<State> state_;
};
}  // namespace bw::game
