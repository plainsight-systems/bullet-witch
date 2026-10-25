// Game pause/equipment interaction owner. PauseFlow owns menu decisions;
// MissionPlayerState owns equipment availability/selection; StageInspection
// owns actor/resource replacement. This adapter applies their transactions and
// rebinds borrowed review catalogs only after publication. It never owns time,
// resource arithmetic, HUD lanes or permanent Profile data. UI frames consume
// input; bounded loading transactions report a pacing-rebase fact to the host.
// Stable borrowed owners outlive this noncopyable owner. No steady-state heap
// work; costume/review resource operations remain explicit bounded requests.
// Design Core I.1 (#ri-explicit), C.9 (#rc-private); performance MEM.9
// (guidelines/memory/MEM.9-embedded-allocate-at-init.md).
#pragma once
#include "pause_flow.hpp"
#include "stage_inspection.hpp"

namespace bw::game {
enum class SpellRequestStatus : std::uint8_t;
struct MissionInput {
  GameplayInput player;
  bool pause_pressed{};
  MenuInput menu;
};
struct InteractionResult {
  GameplayInput actions;
  bool ui_consumed{}, resource_transaction{};
};
class MissionInteraction {
 public:
  MissionInteraction(MissionControl& control, MissionPlayerState& player,
                     MissionLoading& loading, StageInspection& stage,
                     bool developer_review);
  MissionInteraction(const MissionInteraction&) = delete;
  MissionInteraction& operator=(const MissionInteraction&) = delete;
  MissionInteraction(MissionInteraction&&) = delete;
  MissionInteraction& operator=(MissionInteraction&&) = delete;
  ~MissionInteraction() = default;
  InteractionResult admit(const MissionInput& input);
  void cycle_weapon();
  bool paused() const { return pause_.paused(); }
  const MenuPresentation& presentation() const { return pause_.presentation(); }
  std::string_view notice() const { return notice_; }
  // Typed feedback selects static game text; no caller string is retained.
  void observe_spell(SpellRequestStatus status);

 private:
  void bind_review_catalogs();
  MissionControl& control_;
  MissionPlayerState& player_;
  MissionLoading& loading_;
  StageInspection& stage_;
  PauseFlow pause_;
  std::string_view notice_;
};
}  // namespace bw::game
