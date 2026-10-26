// Native storage for the PC executable's vibration, mouse-bias and turn-speed
// configuration, separate from original System save bytes. This stable owner
// borrows SaveStorage for its lifetime; callers get copied observations and a
// restricted control capability. Load validates before publication; save clears
// dirty only on successful atomic replacement. The named versioned 8-byte
// sidecar adapts the original Windows SystemSetting ownership without touching
// game data or importing unrelated renderer/window settings. Main-thread only.
// No allocation in update; I/O only at explicit load/save boundaries. Design
// Core I.1/C.9, implementation E.4 and both-phase performance MEM.9 (source
// references in controller_settings.hpp) enforce narrow authority and bounds.
#pragma once
#include "bw/platform/save_storage.hpp"
namespace bw::game {
struct DeviceOptionsSnapshot {
  bool vibration{true};
  static constexpr unsigned kDefaultSensitivity = 60;
  unsigned mouse_bias{kDefaultSensitivity}, turn_speed{kDefaultSensitivity};
  bool operator==(const DeviceOptionsSnapshot&) const = default;
};
class DeviceOptions;
class DeviceOptionsSaveControl {
 public:
  platform::SaveStatus save() const;

 private:
  friend class DeviceOptions;
  explicit DeviceOptionsSaveControl(DeviceOptions& owner) : owner_(&owner) {}
  DeviceOptions* owner_;
};
class DeviceOptionsControl {
 public:
  DeviceOptionsSnapshot snapshot() const;
  void apply(DeviceOptionsSnapshot value) const;

 private:
  friend class DeviceOptions;
  explicit DeviceOptionsControl(DeviceOptions& owner) : owner_(&owner) {}
  DeviceOptions* owner_;
};
class DeviceOptions {
 public:
  explicit DeviceOptions(platform::SaveStorage& storage) : storage_(storage) {}
  ~DeviceOptions() = default;
  DeviceOptions(const DeviceOptions&) = delete;
  DeviceOptions& operator=(const DeviceOptions&) = delete;
  DeviceOptions(DeviceOptions&&) = delete;
  DeviceOptions& operator=(DeviceOptions&&) = delete;
  DeviceOptionsSaveControl save_control() {
    return DeviceOptionsSaveControl(*this);
  }
  DeviceOptionsControl control() { return DeviceOptionsControl(*this); }
  DeviceOptionsSnapshot snapshot() const { return value_; }
  platform::SaveStatus load();
  platform::SaveStatus save();

 private:
  friend class DeviceOptionsControl;
  void apply(DeviceOptionsSnapshot value);
  platform::SaveStorage& storage_;
  DeviceOptionsSnapshot value_;
  bool dirty_{};
};
}  // namespace bw::game
