// The ancillary boundary admits only legitimate empty-owner teardown. Exercise
// unsupported producer activation without claiming audio/event simulation.
// Core I.1 and performance MEM.9 keep this a pure in-process contract test.
#include "game/mission/mission_ancillary_state.hpp"

#include <initializer_list>
#include <limits>
#include <stdexcept>

void mission_ancillary_state_tests() {
  using bw::game::MissionAncillaryState;
  static_assert(!MissionAncillaryState::capabilities().realtime_event_actors);
  static_assert(!MissionAncillaryState::capabilities().environmental_audio);
  static_assert(!MissionAncillaryState::capabilities().world_spells);
  MissionAncillaryState::stop_magic_after_defeat(true);
  bool rejected_live_stop = false;
  try {
    MissionAncillaryState::stop_magic_after_defeat(false);
  } catch (const std::logic_error&) {
    rejected_live_stop = true;
  }
  if (!rejected_live_stop) {
    throw std::runtime_error("Unimplemented live magic-stop was acknowledged");
  }
  MissionAncillaryState::cancel_realtime_event_actors();
  MissionAncillaryState::set_environment_sound(0);
  for (const auto identifier :
       {1U, std::numeric_limits<std::uint32_t>::max()}) {
    bool rejected = false;
    try {
      MissionAncillaryState::set_environment_sound(identifier);
    } catch (const std::logic_error&) {
      rejected = true;
    }
    if (!rejected) {
      throw std::runtime_error("Unsupported environment sound was accepted");
    }
  }
  MissionAncillaryState::cancel_realtime_event_actors();
  MissionAncillaryState::set_environment_sound(0);
}
