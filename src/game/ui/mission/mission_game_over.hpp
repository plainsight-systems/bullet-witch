// Original defeat task presentation and choice owner. Presentation seconds are
// supplied by the caller; this component cannot pause, restart or retire the
// mission. It publishes a copied outcome only after confirmation and the source
// fade complete. Separate linear entrance channels preserve original order;
// fixed state and sound batches allocate nothing in update. The surface borrows
// resource/output services, owns its leases and texture handles, and releases
// them before those services die. Core I.11 (#ri-raw) separates borrows from
// ownership; performance MEM.9 (guidelines/memory/
// MEM.9-embedded-allocate-at-init.md) confines allocation to preparation.
// Source: TaskGameover 4a4380/4a3bb0; first-combat-physics-boundary.md.
#pragma once
#include <memory>
#include <optional>

#include "bw/content/resources/service.hpp"
#include "bw/render/output/sprites.hpp"
#include "game/ui/audio/menu_audio.hpp"
#include "game/ui/frontend/menu_flow.hpp"

namespace bw::game {
enum class MissionGameOverOutcome : std::uint8_t { restart, return_to_menu };
enum class MissionGameOverPhase : std::uint8_t {
  inactive,
  background,
  prompt,
  choices,
  interactive,
  confirming,
  fading,
  complete
};
struct MissionGameOverPresentation {
  MissionGameOverPhase phase{MissionGameOverPhase::inactive};
  float background_alpha{}, prompt_alpha{}, choices_alpha{}, selection_pulse{},
      confirmation_pulse{}, fade_alpha{};
  bool restart{true};
};
class MissionGameOverFlow {
 public:
  void begin();
  void update(float presentation_seconds, MenuInput input);
  const MissionGameOverPresentation& presentation() const { return view_; }
  std::optional<MissionGameOverOutcome> outcome() const;
  MenuSoundRequests take_sounds();

 private:
  MissionGameOverPresentation view_;
  MenuSoundRequests sounds_;
  float phase_elapsed_{}, pulse_elapsed_{};
};
class MissionGameOverSurface {
 public:
  MissionGameOverSurface(resources::ResourceService&, render::SpriteOutput&);
  ~MissionGameOverSurface();
  MissionGameOverSurface(const MissionGameOverSurface&) = delete;
  MissionGameOverSurface& operator=(const MissionGameOverSurface&) = delete;
  MissionGameOverSurface(MissionGameOverSurface&&) = delete;
  MissionGameOverSurface& operator=(MissionGameOverSurface&&) = delete;
  void begin();
  bool ready();
  void show(const MissionGameOverPresentation&);
  void end() noexcept;

 private:
  struct State;
  std::shared_ptr<State> state_;
};
}  // namespace bw::game
