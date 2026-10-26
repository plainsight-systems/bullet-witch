// Game-owned mission-frame sequencing. Compose distinct interaction, input
// arbitration, resource authority, actor simulation and cockpit presentation.
// begin consumes UI/transactions, publishes exactly one world frame, then
// admits ring/action input; advance runs the actor, finishes resource recovery
// once, and publishes cockpit observations. The host may advance media between
// these phases but cannot inspect/mask game commands or interpret HUD fields. A
// repeated/out-of-order phase rejects before mutation. No OS time, Profile,
// resource setters, script-success fabrication or allocation in normal frames.
// All borrowed owners outlive this stable coordinator. Resources outlive Stage,
// whose restricted action handle is bound once here. Design Core I.1
// (#ri-explicit), C.9 (#rc-private); performance MEM.9 (guidelines/memory/
// MEM.9-embedded-allocate-at-init.md) keeps per-frame publication inline.
#pragma once
#include "menu_surface.hpp"
#include "mission_interaction.hpp"
#include "mission_transition.hpp"
#include "player_cockpit.hpp"
#include "player_input_routing.hpp"

namespace bw::game {
struct MissionFrameDisposition {
  bool ui_consumed{}, resource_transaction{};
  MissionTransition transition{MissionTransition::none};
};
class MissionGameplay {
 public:
  MissionGameplay(world::WorldClock& clock, MissionControl& control,
                  MissionPlayerState& player, PlayerResources& resources,
                  MissionLoading& loading, StageInspection& stage,
                  GameplayHudOutput& hud, MissionPauseOutput& pause_output,
                  SourceRandom& random, audio::CueOutput& audio,
                  HelpControl help, PauseOptionsServices settings,
                  CockpitActivation activation);
  MissionGameplay(const MissionGameplay&) = delete;
  MissionGameplay& operator=(const MissionGameplay&) = delete;
  MissionGameplay(MissionGameplay&&) = delete;
  MissionGameplay& operator=(MissionGameplay&&) = delete;
  ~MissionGameplay() = default;
  MissionFrameDisposition begin(float presentation_seconds,
                                const MissionInput& input);
  void advance();
  void present();
  float presentation_opacity() const {
    return interaction_.presentation_opacity();
  }

 private:
  world::WorldClock& clock_;
  MissionControl& control_;
  const MissionPlayerState& player_;
  PlayerResources& resources_;
  StageInspection& stage_;
  MissionInteraction interaction_;
  PlayerInputRouting input_;
  PlayerCockpit cockpit_;
  std::optional<world::WorldFrame> frame_;
  GameplayInput actions_;
};
}  // namespace bw::game
