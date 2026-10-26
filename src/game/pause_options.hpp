// Original shared frontend/in-mission Options transaction. Profile and
// DeviceOptions retain authority; narrow controls apply live edits, entry
// snapshots support Back's discard confirmation, and explicit save capabilities
// persist on Exit. The flow owns UI state and source timing only. Controller
// input and costume replacement are child transactions. Ordinary updates use
// fixed storage; storage I/O is confined to the explicit save phase. Core E.4
// (#re-design-invariants) and performance MEM.9 (guidelines/memory/
// MEM.9-embedded-allocate-at-init.md), read in design and implementation,
// require validation before publication and bounded state. Source contract:
// research/engine/subsystems/native-pause-menu.md.
#pragma once
#include <optional>
#include <span>

#include "bw/math/scalar.hpp"
#include "controller_options.hpp"
#include "menu_audio.hpp"
#include "menu_dialog.hpp"
#include "menu_flow.hpp"
#include "menu_prompt.hpp"

namespace bw::game {
// Source mission flag removes frontend-only children; the approved mission
// costume addition remains separate. Context is immutable for each flow.
enum class OptionsContext : std::uint8_t { mission, frontend };
enum class OptionsChild : std::uint8_t { leaderboards, power_up, costume };
enum class PauseOptionsPage : std::uint8_t {
  options,
  brightness,
  controls,
  costume,
  notice,
  discard,
  save_prompt,
  saved,
  complete,
  external_child
};
enum class PauseOption : std::uint8_t {
  subtitles,
  music,
  effects,
  controls,
  brightness,
  autosave,
  costume,
  exit,
  leaderboards,
  power_up
};
// Source row order is context-specific. Numeric compatibility lives here,
// not in surface callers or the frontend coordinator.
std::span<const PauseOption> options_rows(OptionsContext context);
struct PauseOptionsServices {
  SettingsControl settings;
  SettingsSaveControl save_settings;
  DeviceOptionsControl device;
  DeviceOptionsSaveControl save_device;
  std::optional<RecordSaveControl> record_save{};
};
struct PauseOptionsSettings {
  bool subtitles{}, autosave{};
  unsigned music{}, effects{}, costume{};
};
struct PauseOptionsPresentation {
  MenuPrompts prompts{menu_prompts(true)};
  OptionsContext context{OptionsContext::mission};
  PauseOptionsPage page{PauseOptionsPage::complete};
  PauseOption selected{PauseOption::subtitles};
  PauseOptionsSettings settings;
  float elapsed{};
  bool opening{}, exiting{};
  float description_alpha{}, arrow_offset{}, marker_brightness{1}, opacity{1};
  std::string_view notice;
  bool dialog_yes{}, dialog_closing{};
  float dialog_progress{};
};
class PauseOptionsFlow {
 public:
  explicit PauseOptionsFlow(PauseOptionsServices services,
                            OptionsContext context = OptionsContext::mission);
  // External frontend tasks acquire their own authority/resources only after
  // conceal. The request remains stable until that owner reports completion.
  std::optional<OptionsChild> requested_child() const { return child_request_; }
  void complete_child(bool record_changed = false);
  void start(unsigned current_costume);
  void update(MenuInput input, float presentation_seconds,
              const input::PhysicalState& physical = {});
  const PauseOptionsPresentation& presentation() const { return view_; }
  bool completed() const { return view_.page == PauseOptionsPage::complete; }
  std::optional<unsigned> requested_costume() const { return costume_request_; }
  void complete_costume(bool success);
  const MenuPresentation& costume_presentation() const { return costume_; }

  const ControllerOptionsPresentation& controller_presentation() const {
    return controllers_.presentation();
  }

  // Consume only this update's native UI cues; no audio device authority is
  // exposed to navigation. The next update replaces an unconsumed batch.
  MenuSoundRequests take_sounds() {
    const auto result = sounds_;
    sounds_ = {};
    return result;
  }

 private:
  enum class ChildTransition : std::uint8_t {
    none,
    entering_controls,
    returning_controls,
    entering_external,
    returning_external
  };
  ChildTransition child_transition_{};
  float child_transition_seconds_{};
  bool record_pending_{};
  OptionsContext context_;
  std::optional<OptionsChild> child_request_, pending_child_;
  PauseOptionsServices services_;
  ControllerOptionsFlow controllers_;
  std::optional<SettingsCheckpoint> baseline_;
  DeviceOptionsSnapshot device_baseline_;
  MenuSoundRequests sounds_;
  MenuDialog dialog_;
  void observe_settings();
  bool changed() const;
  void begin_exit();
  void save();
  void dialog(PauseOptionsPage page, bool yes);
  void update_dialog(MenuInput input, float seconds);
  void change_setting(int direction, bool reset);
  void publish_costumes();
  void update_policy(MenuInput input, float presentation_seconds);
  PauseOptionsPresentation view_;
  MenuPresentation costume_;
  std::optional<unsigned> costume_request_;
  math::DampedResponseState<float> description_;
  float pulse_phase_{};
};
class PauseOptionsOutput {
 public:
  PauseOptionsOutput() = default;
  virtual ~PauseOptionsOutput() = default;
  PauseOptionsOutput(const PauseOptionsOutput&) = delete;
  PauseOptionsOutput& operator=(const PauseOptionsOutput&) = delete;
  PauseOptionsOutput(PauseOptionsOutput&&) = delete;
  PauseOptionsOutput& operator=(PauseOptionsOutput&&) = delete;
  virtual void show(const PauseOptionsPresentation& presentation) = 0;
};
}  // namespace bw::game
