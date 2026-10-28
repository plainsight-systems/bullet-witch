// Compatibility boundary for mission producers not yet admitted by this slice.
// The original cancellation and sound-stop commands are valid on empty owners;
// they do not clear mission callback registrations. Capabilities are explicit
// and immutable, with no fabricated voices, event actors or mutable counters.
// Nonzero environmental sound activation throws until a real source producer
// replaces this boundary. Core I.1 (#ri-explicit) makes the limitation visible;
// performance MEM.9 (guidelines/memory/MEM.9-embedded-allocate-at-init.md)
// avoids allocating unused producer storage. No resources are borrowed.
#pragma once
#include <cstdint>

namespace bw::game {
struct MissionAncillaryCapabilities {
  bool realtime_event_actors{};
  bool environmental_audio{};
  bool world_spells{};
};
class MissionAncillaryState {
 public:
  static constexpr MissionAncillaryCapabilities capabilities() noexcept {
    return {};
  }
  static void cancel_realtime_event_actors() noexcept;
  static void set_environment_sound(std::uint32_t identifier);
  // Defeat skips the original player notification; there are no live spell
  // producers to retire. Living-player stop admission needs its source gates.
  static void stop_magic_after_defeat(bool player_dead);
};
}  // namespace bw::game
