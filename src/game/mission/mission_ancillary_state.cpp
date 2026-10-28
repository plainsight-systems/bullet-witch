// Source cancellation visits a separate real-time event actor registry;
// environment sound zero stops environmental voices. Both producer sets are
// explicitly absent here. Checked capabilities prevent an admission change
// silently inheriting these empty-owner operations. Core I.1 and performance
// MEM.9 retain explicit errors and zero producer storage on the supported path.
#include "mission_ancillary_state.hpp"

#include <stdexcept>

namespace bw::game {
void MissionAncillaryState::cancel_realtime_event_actors() noexcept {
  static_assert(
      !capabilities().realtime_event_actors,
      "Admitted real-time actors require their actual cancellation owner");
}
void MissionAncillaryState::set_environment_sound(std::uint32_t identifier) {
  static_assert(!capabilities().environmental_audio,
                "Admitted environmental audio requires its actual voice owner");
  if (identifier != 0) {
    throw std::logic_error("Environmental sound activation is not admitted");
  }
}
void MissionAncillaryState::stop_magic_after_defeat(bool player_dead) {
  static_assert(!capabilities().world_spells,
                "Admitted world spells require their retained teardown owner");
  if (!player_dead) {
    throw std::logic_error(
        "Living player magic-stop notification is not admitted");
  }
}
}  // namespace bw::game
