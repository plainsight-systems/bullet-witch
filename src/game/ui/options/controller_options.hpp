// Original native input-configuration task. This owner holds UI selection,
// entry rollback observations and capture phases; Profile and DeviceOptions
// remain authoritative through narrow capabilities. UI edits update these
// owners, Exit retains changes, Back offers source discard confirmation, and
// Default restores original mappings/options. Persistence belongs to parent
// Options. Physical capture waits for release both before and after rebinding;
// duplicates swap through the compatibility module. Main-thread only, caller
// supplied presentation time, fixed snapshots and sound batches, no frame
// allocation. Core I.1/C.9 (design), E.4 (implementation), performance MEM.9
// (both phases) apply as cited in controller_settings.hpp. Source/design:
// research/engine/subsystems/native-pause/controller-options.md.
#pragma once
#include "bw/input/physical_state.hpp"
#include "bw/math/scalar.hpp"
#include "game/profile/profile.hpp"
#include "game/settings/controller_settings.hpp"
#include "game/settings/device_options.hpp"
#include "game/ui/audio/menu_audio.hpp"
#include "game/ui/common/menu_dialog.hpp"
#include "game/ui/common/menu_prompt.hpp"
#include "game/ui/frontend/menu_flow.hpp"
namespace bw::game {
enum class ControllerSection : std::uint8_t {
  keyboard,
  gamepad,
  miscellaneous,
  defaults,
  exit
};
enum class ControllerDialog : std::uint8_t { none, defaults, discard };
struct ControllerOptionsPresentation {
  MenuPrompts prompts{menu_prompts(true)};
  ControllerSettings settings;
  DeviceOptionsSnapshot device;
  ControllerSection selected{ControllerSection::keyboard},
      displayed{ControllerSection::keyboard};
  unsigned row{};
  bool active{}, editing{}, capturing{}, opening{}, exiting{}, dialog_yes{};
  ControllerDialog dialog{};
  MenuDialogPresentation dialog_view;
  float elapsed{}, description_alpha{}, marker_brightness{1}, opacity{1};
};
class ControllerOptionsFlow {
 public:
  ControllerOptionsFlow(SettingsControl settings, DeviceOptionsControl device)
      : settings_(settings), device_(device) {}
  void start();
  void update(MenuInput menu, const input::PhysicalState& physical,
              float seconds);
  bool completed() const { return !view_.active; }
  const ControllerOptionsPresentation& presentation() const { return view_; }
  MenuSoundRequests take_sounds() {
    const auto result = sounds_;
    sounds_ = {};
    return result;
  }

 private:
  enum class Capture : std::uint8_t {
    none,
    await_release,
    wait,
    after_release
  };
  void update_policy(MenuInput menu, const input::PhysicalState& physical);
  void refresh();
  void close();
  void change_misc(int direction);
  SettingsControl settings_;
  DeviceOptionsControl device_;
  ControllerSettings baseline_;
  DeviceOptionsSnapshot device_baseline_;
  ControllerOptionsPresentation view_;
  Capture capture_{};
  MenuSoundRequests sounds_;
  MenuDialog dialog_;
  math::DampedResponseState<float> description_;
  float pulse_phase_{};
};
class ControllerOptionsOutput {
 public:
  ControllerOptionsOutput() = default;
  virtual ~ControllerOptionsOutput() = default;
  ControllerOptionsOutput(const ControllerOptionsOutput&) = delete;
  ControllerOptionsOutput& operator=(const ControllerOptionsOutput&) = delete;
  ControllerOptionsOutput(ControllerOptionsOutput&&) = delete;
  ControllerOptionsOutput& operator=(ControllerOptionsOutput&&) = delete;
  virtual void show(const ControllerOptionsPresentation& view) = 0;
};
}  // namespace bw::game
