// Original System controller section compatibility. Decode copied observations;
// validate an entire candidate before writing only known fields, preserving all
// unknown bytes. No persistence or input device authority is exposed. A zero
// controller identity selects the original XInput defaults; retained foreign
// mappings remain untouched until that device is edited. Fixed 16 button and
// 17 key arrays, no allocation. Main-thread callers serialize Profile changes.
// Design Core I.1/C.9 (#ri-explicit/#rc-private), implementation E.4
// (#re-design-invariants), and both-phase performance MEM.9
// (guidelines/memory/MEM.9-embedded-allocate-at-init.md) enforce explicit,
// bounded transactional updates. Provenance:
// native-pause/controller-options.md.
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
namespace bw::game {
inline constexpr std::size_t kControllerButtonCount = 16;
inline constexpr std::size_t kControllerKeyCount = 17;
inline constexpr std::uint32_t kSourceXinputIdentity = 0x028e045e;
inline constexpr std::array<std::uint8_t, kControllerButtonCount>
    kSourceXinputButtons{0, 1, 2,  3,  4,  255, 6,  7,
                         8, 9, 12, 13, 14, 15,  10, 11};
inline constexpr std::array<std::uint8_t, 11> kControllerEditSlots{
    15, 14, 6, 7, 10, 11, 12, 13, 8, 9, 4};
struct ControllerSettings {
  bool invert_vertical{}, invert_horizontal{};
  std::uint32_t controller_identity{};
  std::array<std::uint8_t, kControllerButtonCount> buttons{};
  std::array<std::uint16_t, kControllerKeyCount> keys{};
  bool operator==(const ControllerSettings&) const = default;
};
ControllerSettings controller_default_settings();
ControllerSettings decode_controller_settings(
    std::span<const std::byte> system);
void encode_controller_settings(std::span<std::byte> system,
                                const ControllerSettings& settings);
// Original capture replaces the selected binding and swaps the first collision.
void remap_controller_button(ControllerSettings& settings, std::size_t row,
                             std::uint8_t button);
void remap_controller_key(ControllerSettings& settings, std::size_t row,
                          std::uint16_t key);
}  // namespace bw::game
