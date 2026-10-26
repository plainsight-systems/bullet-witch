// Native gameplay pause task: navigation/animation and typed operation
// requests. The caller owns mission actions and child tasks. This owner borrows
// only the stable UserPauseControl capability, never full MissionControl or
// world state. Supplied presentation time advances UI while simulation remains
// paused. Opening -> interactive -> child/confirmation -> closing preserves
// user pause until exit animation completes. Unavailable actions report a
// reason without mutation; confirmed retire remains paused until the caller
// tears down mission. Presentation is a copied bounded value. All calls are on
// the mission thread, no ordinary frame allocations. Core I.1/C.9/I.11
// (ri-explicit/rc-private/ ri-raw) and performance MEM.9 (guidelines/memory/
// MEM.9-embedded-allocate-at-init.md), retrieved during design, enforce narrow
// command authority, stable lifetime, explicit errors and bounded storage.
// Original tasks and transformation evidence: native-pause-menu.md.
#pragma once
#include <array>
#include <optional>
#include <string_view>

#include "menu_audio.hpp"
#include "menu_dialog.hpp"
#include "menu_flow.hpp"
#include "menu_prompt.hpp"
#include "mission_control.hpp"

namespace bw::game {
inline constexpr std::size_t pause_menu_item_count = 6;
enum class PauseMenuAction : std::uint8_t {
  help,
  options,
  checkpoint,
  restart_stage,
  retire,
  resume
};
enum class PauseMenuPhase : std::uint8_t {
  inactive,
  opening,
  interactive,
  child,
  confirmation,
  unavailable,
  closing,
  retiring
};
struct PauseOperationAvailability {
  bool checkpoint{}, restart_stage{}, retire{};
};
struct PauseMenuItem {
  float x{}, y{}, scale{1}, alpha{}, brightness{1};
};
struct PauseMenuPresentation {
  MenuPrompts prompts{menu_prompts(true)};
  PauseMenuPhase phase{PauseMenuPhase::inactive};
  PauseMenuAction selected{PauseMenuAction::help};
  std::array<PauseMenuItem, pause_menu_item_count> items{};
  std::array<float, 4> background_x{};
  std::array<float, 4> background_alpha{};
  float shade_alpha{};
  float dialog_progress{};
  bool confirmation_yes{};
  std::string_view message;
};
class PauseMenuOutput {
 public:
  PauseMenuOutput() = default;
  virtual ~PauseMenuOutput() = default;
  PauseMenuOutput(const PauseMenuOutput&) = delete;
  PauseMenuOutput& operator=(const PauseMenuOutput&) = delete;
  PauseMenuOutput(PauseMenuOutput&&) = delete;
  PauseMenuOutput& operator=(PauseMenuOutput&&) = delete;
  virtual void show(const PauseMenuPresentation& presentation) = 0;
};
class PauseMenuFlow {
 public:
  PauseMenuFlow(UserPauseControl control,
                PauseOperationAvailability availability);
  PauseMenuFlow(const PauseMenuFlow&) = delete;
  PauseMenuFlow& operator=(const PauseMenuFlow&) = delete;
  PauseMenuFlow(PauseMenuFlow&&) = delete;
  PauseMenuFlow& operator=(PauseMenuFlow&&) = delete;
  ~PauseMenuFlow() = default;
  // Returns true for every consumed frame, including final resume transition.
  bool update(float presentation_seconds, bool pause_pressed, MenuInput input);
  bool active() const noexcept { return phase_ != PauseMenuPhase::inactive; }
  const PauseMenuPresentation& presentation() const noexcept { return output_; }
  std::optional<PauseMenuAction> request() const noexcept { return request_; }
  // Only child task completion may return from the child phase. Retire is a
  // separate mission lifetime operation and cannot be acknowledged as a child.
  void complete_child();

  // Consume only this update's native UI cues; no audio device authority is
  // exposed to navigation. The next update replaces an unconsumed batch.
  MenuSoundRequests take_sounds() {
    const auto result = sounds_;
    sounds_ = {};
    return result;
  }

 private:
  MenuSoundRequests sounds_;
  MenuDialog dialog_;
  void enter(PauseMenuPhase phase);
  void activate_selection();
  void publish();
  UserPauseControl control_;
  const PauseOperationAvailability availability_;
  PauseMenuPhase phase_{PauseMenuPhase::inactive};
  PauseMenuAction selected_{PauseMenuAction::help};
  std::optional<PauseMenuAction> request_;
  float elapsed_{};
  std::array<float, pause_menu_item_count> item_elapsed_{};
  bool confirmation_yes_{};
  bool dialog_closing_{};
  float dialog_progress_{};
  PauseMenuPresentation output_;
};
}  // namespace bw::game
