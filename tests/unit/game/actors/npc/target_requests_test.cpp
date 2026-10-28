// Exercise native request ordering/threshold/reset, not authored content
// parity. I.1 checks command outcomes; MEM.9 keeps fixed test data.
#include "game/actors/npc/target_requests.hpp"

#include "../../../../fixtures/camera_fixture.hpp"
void target_requests_tests() {
  using bw::game::NpcTargetRequests;
  using bw::game::TargetKnowledge;
  using camera_fixture::require;
  NpcTargetRequests requests;
  requests.submit({1, TargetKnowledge::known, .5F, 3});
  requests.submit({1, TargetKnowledge::known, .5F, 3});
  require(!requests.consume(),
          "Equal payloads do not combine subthreshold strength");
  requests.submit({1, TargetKnowledge::known, 1, 3});
  requests.submit({2, TargetKnowledge::searching, 1, 4});
  const auto first = requests.consume();
  require(first && first->target == 1 && first->duration == 3,
          "Stable equal-strength order retains first request");
  require(!requests.consume(), "Consumption clears all remaining requests");
  requests.submit({1, TargetKnowledge::known, 1, 3});
  requests.submit({2, TargetKnowledge::searching, 2, 4});
  const auto stronger = requests.consume();
  require(stronger && stronger->target == 2 &&
              stronger->knowledge == TargetKnowledge::searching,
          "Strictly stronger request is inserted ahead");
}
