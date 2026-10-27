// Native HUD resource/presentation owner. ResourceService and SpriteOutput
// outlive this noncopyable/nonmovable main-thread object. prepare acquires
// original cockpit/magic-ring/font archives, uploads selected textures once,
// then show resolves the HUD's const commands into one bounded submission.
// Optional developer text shares the prepared original atlas and cannot replace
// HUD commands. end releases only this owner's interests/textures; destruction
// unwinds partial setup. No player or clock authority is borrowed. Setup cost
// is selected encoded/decoded bytes; frame work is bounded commands/glyphs with
// no allocations. Core R.4 (#rr-ref), F.16 (#rf-in), SL.con.3 (#rsl-bounds),
// performance MEM.9 (guidelines/memory/MEM.9-embedded-allocate-at-init.md) and
// GPU.1 (guidelines/gpu/GPU.1-keep-data-on-device.md), both phases consulted.
// Source and lifetime mapping: gameplay-hud.md.
#pragma once
#include <memory>
#include <string_view>

#include "bw/content/formats/text_table.hpp"
#include "bw/content/resources/service.hpp"
#include "gameplay_hud.hpp"

namespace bw::game {
class GameplayHudSurface final : public GameplayHudOutput {
 public:
  GameplayHudSurface(resources::ResourceService& resources,
                     render::SpriteOutput& output);
  ~GameplayHudSurface() override;
  GameplayHudSurface(const GameplayHudSurface&) = delete;
  GameplayHudSurface& operator=(const GameplayHudSurface&) = delete;
  GameplayHudSurface(GameplayHudSurface&&) = delete;
  GameplayHudSurface& operator=(GameplayHudSurface&&) = delete;
  void prepare();
  bool prepared() const noexcept;
  const formats::TextTable& system_text_table() const;
  void show(const GameplayHud& hud, std::string_view diagnostic) override;
  void show(const GameplayHud& hud) { show(hud, {}); }
  void show_system_message(SystemMessageView message) override;
  void hide() noexcept override;
  void end() noexcept;

 private:
  struct State;
  std::shared_ptr<State> state_;
};
}  // namespace bw::game
