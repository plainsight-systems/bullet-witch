// Defeat UI contract: staged entrance consumes supplied presentation time;
// original input priority/cues and complete fade precede the copied outcome.
// Core I.11 and MEM.9: no mission authority or native process in this unit.
#include "game/ui/mission/mission_game_over.hpp"

#include <limits>
#include <stdexcept>

namespace {
void require(bool value, const char* message) {
  if (!value) throw std::runtime_error(message);
}
void open(bw::game::MissionGameOverFlow& flow) {
  flow.begin();
  constexpr float kFrame = 1.F / 60;
  for (unsigned frame = 0; frame < 240; ++frame) flow.update(kFrame, {});
  require(
      flow.presentation().phase == bw::game::MissionGameOverPhase::interactive,
      "Original game over entrance never completed");
}
}  // namespace
void mission_game_over_tests() {
  using bw::game::MenuSound;
  using bw::game::MissionGameOverFlow;
  using bw::game::MissionGameOverOutcome;
  using bw::game::MissionGameOverPhase;
  MissionGameOverFlow flow;
  require(!flow.outcome(), "Inactive defeat published a mission transition");
  flow.begin();
  flow.update(1, {.pressed = 0x1010});
  require(!flow.outcome() && flow.presentation().prompt_alpha == 0 &&
              flow.presentation().choices_alpha == 0,
          "Early accept bypassed authored entrance channels");
  const auto before = flow.presentation().background_alpha;
  bool rejected = false;
  try {
    flow.update(std::numeric_limits<float>::quiet_NaN(), {});
  } catch (const std::invalid_argument&) {
    rejected = true;
  }
  require(rejected && flow.presentation().background_alpha == before,
          "Invalid interval partially mutated defeat presentation");
  open(flow);
  flow.update(0, {.repeated = 1U << 27});
  require(flow.presentation().restart, "Zero interval consumed input");
  flow.update(.01F, {.repeated = 1U << 27});
  require(!flow.presentation().restart && flow.take_sounds().size == 1,
          "Right navigation did not select original No option");
  flow.update(.01F, {.repeated = 1U << 27});
  require(!flow.presentation().restart, "Original nonwrapping choice wrapped");
  flow.update(.01F, {.pressed = 0x2020});
  require(flow.presentation().phase == MissionGameOverPhase::interactive,
          "Invented cancel behavior left the defeat choice");
  flow.update(.01F, {.pressed = 0x1010, .repeated = 1U << 26});
  const auto sounds = flow.take_sounds();
  require(!flow.presentation().restart && sounds.size == 1 &&
              sounds.requests[0].sound == MenuSound::panel &&
              flow.take_sounds().size == 0 && !flow.outcome(),
          "Confirmation must precede direction and publish source cue once");
  for (unsigned frame = 0; frame < 20; ++frame) flow.update(.01F, {});
  require(!flow.outcome(), "Defeat completed before source confirmation pulse");
  for (unsigned frame = 0; frame < 70; ++frame) flow.update(.01F, {});
  require(flow.outcome() == MissionGameOverOutcome::return_to_menu &&
              flow.presentation().fade_alpha == 1,
          "No must publish return only after black fade completes");
  open(flow);
  flow.update(.01F, {.pressed = 0x1010});
  for (unsigned frame = 0; frame < 100; ++frame) flow.update(.01F, {});
  require(flow.outcome() == MissionGameOverOutcome::restart,
          "Initial original Yes choice did not request restart");
}
