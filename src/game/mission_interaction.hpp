// Game pause/equipment interaction owner. Native pause/help/options own
// normal decisions; DeveloperReviewFlow is reserved for the build-enabled
// developer view. MissionPlayerState owns equipment availability/selection;
// StageInspection owns actor/resource replacement. This adapter applies their
// transactions and rebinds borrowed review catalogs only after publication. It
// never owns time, resource arithmetic, HUD lanes or permanent Profile data. UI
// frames consume input; bounded loading transactions report a pacing-rebase
// fact to the host. Stable borrowed owners and presentation capability outlive
// this noncopyable owner. Mission Help merges saved and effective concept
// availability without writing inherited unlocks; its flow borrows that stable
// read-mark owner. Settings
// use narrow edit/save capabilities over their stable owners. Explicit view
// switches report resource work. Help consumes the copied fixed UI axis, not
// remapped gameplay motion, matching the original receiver ownership. No
// settings authority is added for inline Help controls: copied action-icon
// bindings travel from the input observation to presentation unchanged. No
// steady-state heap work; costume/review resource operations remain explicit
// bounded requests. Design Core I.1
// (#ri-explicit), C.9 (#rc-private); performance MEM.9
// (guidelines/memory/MEM.9-embedded-allocate-at-init.md).
#pragma once
#include "developer_review_flow.hpp"
#include "help_input_icons.hpp"
#include "mission_pause_presentation.hpp"
#include "mission_transition.hpp"
#include "stage_inspection.hpp"

namespace bw::game {
enum class SpellRequestStatus : std::uint8_t;
struct MissionInput {
  GameplayInput player;
  bool pause_pressed{};
  MenuInput menu;
  input::PhysicalState physical;
  HelpInputIcons help_icons{
      help_input_icons(controller_default_settings(), false)};
};
struct InteractionResult {
  GameplayInput actions;
  bool ui_consumed{}, resource_transaction{};
};
class MissionInteraction {
 public:
  MissionInteraction(MissionControl& control, MissionPlayerState& player,
                     MissionLoading& loading, StageInspection& stage,
                     MissionPauseOutput& pause_output, audio::CueOutput& audio,
                     HelpControl help, PauseOptionsServices settings);
  MissionInteraction(const MissionInteraction&) = delete;
  MissionInteraction& operator=(const MissionInteraction&) = delete;
  MissionInteraction(MissionInteraction&&) = delete;
  MissionInteraction& operator=(MissionInteraction&&) = delete;
  ~MissionInteraction() = default;
  InteractionResult admit(float presentation_seconds,
                          const MissionInput& input);
  void cycle_weapon(int direction = 1);
  bool paused() const { return control_.user_paused(); }
  MissionTransition transition_requested() const;
  void present();
  float presentation_opacity() const;
  std::string_view notice() const { return notice_; }
  // Typed feedback selects static game text; no caller string is retained.
  void observe_spell(SpellRequestStatus status);

 private:
  void bind_review_catalogs();
  MissionControl& control_;
  MissionPlayerState& player_;
  MissionLoading& loading_;
  StageInspection& stage_;
  MissionPauseOutput& pause_output_;
  MenuAudio menu_audio_;
  PauseMenuFlow pause_;
  MissionHelpState help_state_;
  PauseHelpFlow help_;
  PauseOptionsFlow options_;
  DeveloperReviewFlow developer_;
  MissionPauseView view_{MissionPauseView::none};
  bool developer_active_{};
  std::string_view notice_;
  MenuPrompts prompts_;
  HelpInputIcons help_icons_{
      help_input_icons(controller_default_settings(), false)};
};
}  // namespace bw::game
