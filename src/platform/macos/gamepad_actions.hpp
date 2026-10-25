// Native GameController action sampling and one-pump press ownership. Sample
// all controllers before deriving edges; keyboard taps may add explicit edges.
// A release rearms the next press. No repeats, game state, callbacks or heap
// allocation belong here. Caller serializes on the main thread and clears
// sampled holds on focus loss. I.1/E.4 make that boundary explicit; MEM.9 keeps
// the edge history fixed. Physical-position publication applies Core I.1
// (#ri-explicit); MEM.9 (guidelines/memory/MEM.9-embedded-allocate-at-init.md)
// retains fixed state. Both MCP phases keep game gestures out of sampling.
#pragma once
#include <cstdint>
@class GCExtendedGamepad;
namespace bw::platform::macos {
inline constexpr std::uint32_t kReloadButton = 1U << 9;
inline constexpr std::uint32_t kAttackButton = 1U << 10;
inline constexpr std::uint32_t kWeaponButton = 1U << 11;
inline constexpr std::uint32_t kTargetButton = 1U << 12;
inline constexpr std::uint32_t kShoulderLeftButton = 1U << 13;
inline constexpr std::uint32_t kShoulderRightButton = 1U << 14;
inline constexpr std::uint32_t kFaceNorthButton = 1U << 15;
// The existing baseline action bits share the same physical button edges.
inline constexpr std::uint32_t kFaceEastButton = kWeaponButton;
inline constexpr std::uint32_t kFaceSouthButton = kAttackButton;
inline constexpr std::uint32_t kFaceWestButton = kReloadButton;
std::uint32_t gamepad_action_holds(GCExtendedGamepad* pad) noexcept;
class GameplayButtonEdges {
 public:
  std::uint32_t sample(std::uint32_t held,
                       std::uint32_t explicit_edges = 0) noexcept {
    const auto pressed = explicit_edges | (held & ~previous_);
    previous_ = held;
    return pressed;
  }

 private:
  std::uint32_t previous_{};
};
}  // namespace bw::platform::macos
