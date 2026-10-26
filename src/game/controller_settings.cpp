// Validate the retained System section before candidate publication. Known
// inversion bits, device identity, mapping bytes and keyboard scan codes are
// the only writes. Duplicate assignment swaps the displaced binding, exactly
// as native capture does. Bounded 17-record operations allocate nothing.
// Both phases Core I.1/C.9/E.4 and performance MEM.9 apply as documented in
// controller_settings.hpp; source offsets stay at this compatibility boundary.
#include "controller_settings.hpp"

#include <algorithm>
#include <stdexcept>
namespace bw::game {
namespace {
constexpr std::size_t kSystemSize = 72, kFlags = 12, kIdentity = 16,
                      kButtons = 20, kKeys = 36;
constexpr unsigned kByteBits = 8, kByteMask = 255;
template <std::size_t Count>
std::uint32_t read(std::span<const std::byte> bytes, std::size_t offset) {
  std::uint32_t result = 0;
  for (std::size_t i = 0; i < Count; ++i) {
    result |= std::to_integer<std::uint32_t>(bytes[offset + i])
              << (i * kByteBits);
  }
  return result;
}
template <std::size_t Count>
void write(std::span<std::byte> bytes, std::size_t offset,
           std::uint32_t value) {
  for (std::size_t i = 0; i < Count; ++i) {
    bytes[offset + i] =
        static_cast<std::byte>((value >> (i * kByteBits)) & kByteMask);
  }
}
}  // namespace
ControllerSettings controller_default_settings() {
  ControllerSettings result;
  static constexpr std::array<std::uint16_t, kControllerKeyCount> kDefaultKeys{
      0x11, 0x1f,  0x1e,  0x20,  0x12c, 0x12d, 0x13, 0x39, 0x2a,
      0x12, 0x12e, 0x135, 0x134, 0x2c,  0x2d,  0x2e, 1};
  result.keys = kDefaultKeys;
  return result;
}
ControllerSettings decode_controller_settings(
    std::span<const std::byte> bytes) {
  if (bytes.size() != kSystemSize) {
    throw std::invalid_argument("Invalid controller System size");
  }
  ControllerSettings result;
  const auto flags = read<4>(bytes, kFlags);
  result.invert_vertical = (flags & 1U) != 0;
  result.invert_horizontal = (flags & 2U) != 0;
  result.controller_identity = read<4>(bytes, kIdentity);
  for (std::size_t i = 0; i < result.buttons.size(); ++i) {
    result.buttons.at(i) = std::to_integer<std::uint8_t>(bytes[kButtons + i]);
  }
  for (std::size_t i = 0; i < result.keys.size(); ++i) {
    result.keys.at(i) =
        static_cast<std::uint16_t>(read<2>(bytes, kKeys + 2 * i));
  }
  return result;
}
void encode_controller_settings(std::span<std::byte> bytes,
                                const ControllerSettings& value) {
  if (bytes.size() != kSystemSize) {
    throw std::invalid_argument("Invalid controller System size");
  }
  // Values loaded from another supported PC controller retain their original
  // byte codes. Capture itself admits only the native device's supported codes.
  const auto flags = (read<4>(bytes, kFlags) & ~3U) |
                     static_cast<unsigned>(value.invert_vertical) |
                     (static_cast<unsigned>(value.invert_horizontal) << 1U);
  write<4>(bytes, kFlags, flags);
  write<4>(bytes, kIdentity, value.controller_identity);
  for (std::size_t i = 0; i < value.buttons.size(); ++i) {
    bytes[kButtons + i] = static_cast<std::byte>(value.buttons.at(i));
  }
  for (std::size_t i = 0; i < value.keys.size(); ++i) {
    write<2>(bytes, kKeys + 2 * i, value.keys.at(i));
  }
}
void remap_controller_button(ControllerSettings& settings, std::size_t row,
                             std::uint8_t button) {
  if (row >= kControllerEditSlots.size() || button >= kControllerButtonCount) {
    throw std::invalid_argument("Invalid controller binding");
  }
  auto candidate = settings;
  if (candidate.controller_identity != kSourceXinputIdentity) {
    candidate.buttons = kSourceXinputButtons;
    candidate.controller_identity = kSourceXinputIdentity;
  }
  const auto slot = kControllerEditSlots.at(row);
  const auto collision = std::ranges::find(candidate.buttons, button);
  if (collision != candidate.buttons.end()) {
    *collision = candidate.buttons.at(slot);
  }
  candidate.buttons.at(slot) = button;
  settings = candidate;
}
void remap_controller_key(ControllerSettings& settings, std::size_t row,
                          std::uint16_t key) {
  constexpr std::uint16_t kUnbound = 0x100, kLastDeviceCode = 0x135;
  if (row >= settings.keys.size() || key == 0 || key == kUnbound ||
      key > kLastDeviceCode) {
    throw std::invalid_argument("Invalid keyboard binding");
  }
  const auto collision = std::ranges::find(settings.keys, key);
  if (collision != settings.keys.end()) *collision = settings.keys.at(row);
  settings.keys.at(row) = key;
}
}  // namespace bw::game
