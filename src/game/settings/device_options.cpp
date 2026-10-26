// Decode a complete versioned candidate, then publish. Persist only on explicit
// command, preserving dirty state after write failure. Original defaults and
// inclusive 1..100 UI bounds are retained. Core E.4 and performance MEM.9 were
// retrieved for implementation; header describes lifetime and cost contracts.
#include "device_options.hpp"

#include <algorithm>
#include <array>
#include <stdexcept>
namespace bw::game {
namespace {
constexpr std::string_view kName = "BulletWitchDeviceOptions";
constexpr std::size_t kBytes = 8, kVibration = 5, kMouse = 6, kTurn = 7;
constexpr unsigned kMaximum = 100;
constexpr std::array<std::byte, 5> kHeader{std::byte{'B'}, std::byte{'W'},
                                           std::byte{'I'}, std::byte{'O'},
                                           std::byte{1}};
bool valid(DeviceOptionsSnapshot value) {
  return value.mouse_bias >= 1 && value.mouse_bias <= kMaximum &&
         value.turn_speed >= 1 && value.turn_speed <= kMaximum;
}
}  // namespace
DeviceOptionsSnapshot DeviceOptionsControl::snapshot() const {
  return owner_->snapshot();
}
void DeviceOptionsControl::apply(DeviceOptionsSnapshot value) const {
  owner_->apply(value);
}
platform::SaveStatus DeviceOptionsSaveControl::save() const {
  return owner_->save();
}
void DeviceOptions::apply(DeviceOptionsSnapshot value) {
  if (!valid(value)) {
    throw std::invalid_argument("Invalid device input settings");
  }
  if (value_ != value) {
    value_ = value;
    dirty_ = true;
  }
}
platform::SaveStatus DeviceOptions::load() {
  const auto result = storage_.read(kName, kBytes);
  if (result.status == platform::SaveStatus::missing) {
    value_ = {};
    dirty_ = true;
    return platform::SaveStatus::ok;
  }
  if (result.status != platform::SaveStatus::ok) return result.status;
  if (result.bytes.size() != kBytes ||
      !std::equal(kHeader.begin(), kHeader.end(), result.bytes.begin()) ||
      std::to_integer<unsigned>(result.bytes[kVibration]) > 1) {
    return platform::SaveStatus::invalid;
  }
  const DeviceOptionsSnapshot candidate{
      result.bytes[kVibration] != std::byte{0},
      std::to_integer<unsigned>(result.bytes[kMouse]),
      std::to_integer<unsigned>(result.bytes[kTurn])};
  if (!valid(candidate)) return platform::SaveStatus::invalid;
  value_ = candidate;
  dirty_ = false;
  return platform::SaveStatus::ok;
}
platform::SaveStatus DeviceOptions::save() {
  if (!dirty_) return platform::SaveStatus::ok;
  std::array<std::byte, kBytes> bytes{};
  std::copy(kHeader.begin(), kHeader.end(), bytes.begin());
  bytes[kVibration] = static_cast<std::byte>(value_.vibration);
  bytes[kMouse] = static_cast<std::byte>(value_.mouse_bias);
  bytes[kTurn] = static_cast<std::byte>(value_.turn_speed);
  const auto result = storage_.write(kName, bytes);
  if (result == platform::SaveStatus::ok) dirty_ = false;
  return result;
}
}  // namespace bw::game
