// Native pause flow contracts exercised in process with a real restricted
// MissionControl capability. Four cases verify user-pause admission/lifetime,
// child isolation, unsupported world operations and confirmation retirement.
// No resources, processes or source-derived pixels are substituted here. Core
// I.1/E.6 and performance MEM.9, retrieved for both phases, keep tests on
// public state transitions with bounded automatic storage and explicit owner
// lifetime.
#include "game/pause_menu.hpp"

#include <limits>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

#include "game/pause_options.hpp"

namespace {
using bw::game::MissionControl;
using bw::game::PauseMenuAction;
using bw::game::PauseMenuFlow;
using bw::game::PauseMenuPhase;
constexpr std::uint32_t kConfirm = 0x1000, kCancel = 0x2000, kDown = 0x02000000,
                        kLeft = 0x04000000;
void require(bool condition, const char* message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}
void open(PauseMenuFlow& menu) {
  require(menu.update(0, true, {}), "pause consumes admission edge");
  menu.update(1.5F, false, {});
  require(menu.presentation().phase == PauseMenuPhase::interactive,
          "opening completes from presentation time");
}
void select(PauseMenuFlow& menu, unsigned steps) {
  for (unsigned i = 0; i < steps; ++i) {
    menu.update(0, false, {0, kDown});
  }
  menu.update(0, false, {kConfirm, 0});
}
void pause_admission_and_resume() {
  MissionControl control;
  PauseMenuFlow menu(control.user_pause_control(), {});
  require(!menu.update(0, false, {}) && !menu.active(), "idle ignores input");
  open(menu);
  require(control.user_paused(), "opening retains user pause");
  menu.update(0, false, {kCancel, 0});
  require(menu.presentation().selected == PauseMenuAction::resume,
          "first Back selects resume");
  menu.update(0, false, {kCancel, 0});
  require(control.user_paused(), "closing retains pause until animation ends");
  require(
      menu.update(.7F, false, {}) && !control.user_paused() && !menu.active(),
      "last resume frame stays consumed while releasing user pause");
  bool rejected = false;
  try {
    menu.update(std::numeric_limits<float>::quiet_NaN(), true, {});
  } catch (const std::invalid_argument&) {
    rejected = true;
  }
  require(rejected && !control.user_paused(),
          "invalid time rejects before pause");
}
void child_isolation() {
  MissionControl control;
  PauseMenuFlow menu(control.user_pause_control(), {});
  open(menu);
  select(menu, 0);
  require(menu.request() == PauseMenuAction::help,
          "Help publishes typed request");
  menu.update(.5F, true, {kCancel, 0});
  require(control.user_paused() &&
              menu.presentation().phase == PauseMenuPhase::child,
          "root cannot resume underneath child");
  menu.complete_child();
  require(!menu.request() &&
              menu.presentation().phase == PauseMenuPhase::interactive,
          "child completion returns once to native menu");
}
void unavailable_world_operation() {
  MissionControl control;
  PauseMenuFlow menu(control.user_pause_control(), {});
  open(menu);
  select(menu, 2);
  require(menu.presentation().phase == PauseMenuPhase::unavailable &&
              !menu.presentation().message.empty() && !menu.request(),
          "missing checkpoint cannot fabricate success");
  menu.update(.4F, false, {});
  menu.update(0, false, {kConfirm, 0});
  menu.update(.3F, false, {});
  require(control.user_paused() && !menu.request(), "notice preserves mission");
}
void confirmed_retirement() {
  MissionControl control;
  PauseMenuFlow menu(control.user_pause_control(), {.retire = true});
  open(menu);
  select(menu, 4);
  menu.update(.4F, false, {});
  menu.update(0, false, {0, kLeft});
  require(menu.presentation().confirmation_yes, "horizontal selects Yes");
  menu.update(0, false, {kCancel, 0});
  require(!menu.presentation().confirmation_yes && !menu.request(),
          "Back selects No without accepting");
  menu.update(0, false, {0, kLeft});
  menu.update(0, false, {kConfirm, 0});
  require(!menu.request(), "retirement waits for dialog exit");
  menu.update(.3F, false, {});
  require(menu.request() == PauseMenuAction::retire && control.user_paused(),
          "retirement request retains pause for owning teardown");
}
class SettingsStorage final : public bw::platform::SaveStorage {
 public:
  bool fail{};
  std::string failed_file;
  std::map<std::string, std::vector<std::byte>> files;
  bw::platform::SaveRead read(std::string_view name, std::size_t) override {
    const auto it = files.find(std::string(name));
    return it == files.end()
               ? bw::platform::SaveRead{bw::platform::SaveStatus::missing, {}}
               : bw::platform::SaveRead{bw::platform::SaveStatus::ok,
                                        it->second};
  }
  bw::platform::SaveStatus write(std::string_view name,
                                 std::span<const std::byte> bytes) override {
    if (fail || name == failed_file) {
      return bw::platform::SaveStatus::io_error;
    }
    files[std::string(name)] = {bytes.begin(), bytes.end()};
    return bw::platform::SaveStatus::ok;
  }
};
void options_preserve_live_boundaries() {
  using bw::game::PauseOptionsPage;
  SettingsStorage storage;
  bw::game::Profile profile;
  bw::game::DeviceOptions device(storage);
  bw::game::PauseOptionsFlow options({profile.settings_control(),
                                      profile.settings_save_control(storage),
                                      device.control(), device.save_control()});
  options.start(2);
  options.update({kConfirm, 0}, .1F);
  require(options.presentation().opening,
          "options entry cannot accept a setting before its source gate");
  options.update({}, 1.8F);
  const auto previous = profile.subtitles_enabled();
  options.update({kConfirm, 0}, 0);
  require(profile.subtitles_enabled() == previous,
          "confirm does not toggle source horizontal row");
  options.update({0, kLeft}, 0);
  require(profile.subtitles_enabled() != previous,
          "horizontal edits authoritative setting live");
  options.update({kCancel, 0}, 0);
  require(options.presentation().page == PauseOptionsPage::discard &&
              !options.presentation().dialog_yes,
          "Back on dirty options asks discard with default No");
  options.update({}, .4F);
  options.update({0, kLeft}, 0);
  options.update({kConfirm, 0}, 0);
  options.update({}, .3F);
  require(profile.subtitles_enabled() == previous,
          "confirmed discard restores entry settings");
  options.update({}, 2.F);
  options.start(2);
  options.update({}, 2.F);
  for (unsigned i = 0; i < 4; ++i) {
    options.update({0, kDown}, 0);
  }
  options.update({kConfirm, 0}, 0);
  require(options.presentation().page == PauseOptionsPage::brightness,
          "Brightness opens the original calibration card");
  options.update({}, .3F);
  options.update({kCancel, 0}, 0);
  options.update({}, .3F);
  for (unsigned i = 0; i < 2; ++i) {
    options.update({0, kDown}, 0);
  }
  options.update({kConfirm, 0}, 0);
  options.update({0, kDown}, 0);
  options.update({kConfirm, 0}, 0);
  require(options.requested_costume() == 3,
          "costume selection submits an explicit mission transaction");
  options.complete_costume(false);
  require(options.presentation().settings.costume == 2 &&
              options.presentation().page == PauseOptionsPage::notice,
          "failed replacement preserves current costume");
}
void settings_snapshot_ownership() {
  SettingsStorage storage;
  bw::game::Profile profile, other;
  const auto settings = profile.settings_control();
  const auto entry = settings.checkpoint();
  bw::game::DeviceOptions device(storage);
  const auto device_entry = device.snapshot();
  auto modified_device = device_entry;
  modified_device.vibration = !modified_device.vibration;
  device.control().apply(modified_device);
  storage.failed_file = "BulletWitchDeviceOptions";
  bool rejected = false;
  try {
    other.settings_control().restore(entry);
  } catch (const std::invalid_argument&) {
    rejected = true;
  }
  require(rejected, "settings snapshot cannot mutate a different authority");
  const auto original = settings.music_volume();
  settings.set_music_volume(original == 0 ? 16 : 0);
  require(profile.settings_save_control(storage).save() ==
              bw::platform::SaveStatus::ok,
          "settings save succeeds before transaction rollback");
  require(
      device.save_control().save() == bw::platform::SaveStatus::io_error,
      "device write fails independently after successful System publication");
  device.control().apply(device_entry);
  storage.failed_file.clear();
  require(device.save_control().save() == bw::platform::SaveStatus::ok,
          "device rollback remains writable after partial failure");
  settings.restore(entry);
  require(profile.settings_save_control(storage).save() ==
              bw::platform::SaveStatus::ok,
          "rollback after partial persistence stays dirty for replacement");
  bw::game::Profile reloaded;
  require(reloaded.load(storage).status == bw::platform::SaveStatus::ok &&
              reloaded.music_volume() == original,
          "saved rollback replaces the previously published settings bytes");
}
void options_start_and_reset_policy() {
  for (const bool autosave : {false, true}) {
    SettingsStorage storage;
    bw::game::Profile profile;
    bw::game::DeviceOptions device(storage);
    profile.settings_control().set_autosave(autosave);
    bw::game::PauseOptionsFlow options(
        {profile.settings_control(), profile.settings_save_control(storage),
         device.control(), device.save_control()});
    options.start(0);
    options.update({}, 2.F);
    options.update({0, kDown}, 0);
    options.update({0, kLeft}, 0);
    options.update({0x1010, 0}, 0);
    require(options.presentation().page == bw::game::PauseOptionsPage::options,
            "ordinary confirm takes source priority over simultaneous Start");
    options.update({0x10, kDown}, 0);
    require(
        options.presentation().selected == bw::game::PauseOption::effects &&
            options.presentation().page == bw::game::PauseOptionsPage::options,
        "navigation takes source priority over simultaneous Start");
    options.update({0x10, 0}, 0);
    require(options.presentation().page ==
                (autosave ? bw::game::PauseOptionsPage::saved
                          : bw::game::PauseOptionsPage::save_prompt),
            "Start exits through save policy from a value row");
  }
  SettingsStorage storage;
  bw::game::Profile profile;
  bw::game::DeviceOptions device(storage);
  profile.settings_control().set_autosave(false);
  bw::game::PauseOptionsFlow options({profile.settings_control(),
                                      profile.settings_save_control(storage),
                                      device.control(), device.save_control()});
  options.start(0);
  options.update({}, 2.F);
  for (unsigned i = 0; i < 5; ++i) {
    options.update({0, kDown}, 0);
  }
  options.update({0x8000, 0}, 0);
  require(!profile.autosave_enabled() && options.take_sounds().size == 0,
          "Reset does not alter original autosave row or emit a cue");
}
void options_save_failure_and_retry() {
  SettingsStorage storage;
  bw::game::Profile profile;
  bw::game::DeviceOptions device(storage);
  profile.settings_control().set_autosave(true);
  bw::game::PauseOptionsFlow options({profile.settings_control(),
                                      profile.settings_save_control(storage),
                                      device.control(), device.save_control()});
  options.start(0);
  options.update({}, 2.F);
  options.update({0, kDown}, 0);
  options.update({0, kLeft}, 0);
  const auto volume = profile.music_volume();
  for (unsigned i = 0; i < 6; ++i) {
    options.update({0, kDown}, 0);
  }
  storage.fail = true;
  options.update({kConfirm, 0}, 0);
  require(
      options.presentation().page == bw::game::PauseOptionsPage::save_prompt &&
          profile.music_volume() == volume,
      "failed save retains live edits and reports recoverable retry dialog");
  storage.fail = false;
  options.update({}, .4F);
  options.update({kConfirm, 0}, 0);
  options.update({}, .3F);
  require(options.presentation().page == bw::game::PauseOptionsPage::saved,
          "successful save awaits original acknowledgment");
  options.update({}, .4F);
  options.update({kConfirm, 0}, 0);
  options.update({}, .3F);
  options.update({}, 2.F);
  require(options.completed() && storage.files.contains("BulletWitchSystem"),
          "successful retry exits after writing original System");
  bw::game::Profile reloaded;
  require(reloaded.load(storage).status == bw::platform::SaveStatus::ok &&
              reloaded.music_volume() == volume,
          "persisted source settings survive reloading");
}
}  // namespace
void pause_menu_contracts() {
  pause_admission_and_resume();
  child_isolation();
  unavailable_world_operation();
  confirmed_retirement();
  options_preserve_live_boundaries();
  options_save_failure_and_retry();
  options_start_and_reset_policy();
  settings_snapshot_ownership();
}
