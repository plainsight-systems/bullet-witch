// Shared elapsed-time action repeat from the original input owner. Device
// adapters supply held/pressed masks and the engine presentation interval;
// gameplay pause does not stop this UI/control-domain consumer. For each held
// bit, add the supplied float delta (including its first sample), emit once at
// 0.5 seconds and subtract 0.125; released bits reset. Press edges are always
// included. This is one source 32-bit bank, not a new device or binding layer.
// Single owner thread, 32 inline timers, constant bounded work, no allocation.
// Invalid/nonfinite/negative intervals reject before mutation. Both-phase Core
// I.1 (#ri-explicit) exposes timing rather than sampling it; performance MEM.9
// (guidelines/memory/MEM.9-embedded-allocate-at-init.md) retains fixed hot
// storage.
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace bw::input {
struct ActionButtons {
  std::uint32_t held{}, pressed{};
};
class ActionRepeat {
 public:
  std::uint32_t advance(ActionButtons buttons, float presentation_seconds);

 private:
  static constexpr std::size_t kActionBits = 32;
  std::array<float, kActionBits> elapsed_{};
};
}  // namespace bw::input
