// Public perception contracts: source candidate ranking, visibility, retained
// knowledge and admitted world scheduling. I.11 keeps the fixture query-only;
// MEM.9 uses fixed candidate storage. These are synthetic contract checks,
// not evidence of a working original encounter.
#include "game/actors/npc/perception.hpp"

#include <type_traits>

#include "../../../../fixtures/camera_fixture.hpp"
namespace {
using bw::game::NpcPerception;
using bw::game::PerceptionActor;
using bw::game::PerceptionInput;
using bw::game::PerceptionSchedule;
using bw::game::PerceptionSettings;
using bw::game::PerceptionSight;
using bw::game::TargetKnowledge;
using camera_fixture::require;
class Sight final : public PerceptionSight {
 public:
  bool visible{true};
  bool reject_nearest{};
  mutable std::size_t calls{};
  bool clear(const bw::math::Vector3<float>&,
             const bw::math::Vector3<float>& target,
             std::uint32_t flags) const override {
    require(flags == 0xf200,
            "Knowledge rays preserve original exclusion flags");
    ++calls;
    return visible && (!reject_nearest || target[2] > 3);
  }
};
constexpr PerceptionSettings kSettings{0.7F, 2, 20, 0, 0.1F, 20};
constexpr PerceptionActor kPlayer{1, {0, 0, 3}, 1, 1, true, false};
void advance_scan(NpcPerception& perception, bw::world::WorldClock& clock,
                  const PerceptionInput& input, Sight& sight) {
  const auto calls = sight.calls;
  for (int i = 0; i < 10 && sight.calls == calls; ++i) {
    perception.advance(clock.publish(0.2F, {}), input, sight);
  }
  require(sight.calls > calls,
          "Bounded original scan schedule revisits target");
}
}  // namespace
void perception_tests() {
  static_assert(!std::is_copy_constructible_v<NpcPerception>);
  static_assert(!std::is_move_constructible_v<NpcPerception>);
  PerceptionSchedule distribution;
  for (std::uint8_t slot = 9; slot > 0; --slot) {
    require(distribution.next() == slot,
            "Equal-load scheduling preserves high slot");
  }
  require(distribution.next() == 9, "Schedule cycles through original bank");
  bw::world::WorldClock clock;
  PerceptionSchedule schedule;
  NpcPerception perception(clock, schedule, kSettings);
  std::array actors{kPlayer};
  PerceptionInput input{{}, {0, 0, 1}, {}, 2, actors};
  Sight sight;
  const auto first = clock.publish(0.2F, {});
  const auto acquired = perception.advance(first, input, sight);
  require(acquired && acquired->source_message == 0x7532 &&
              perception.state().knowledge == TargetKnowledge::known &&
              perception.state().target == 1,
          "Original visible player acquisition publishes known state");
  bool duplicate = false;
  try {
    perception.advance(first, input, sight);
  } catch (const std::invalid_argument&) {
    duplicate = true;
  }
  require(duplicate, "Repeated world interval rejected");
  const auto before_pause = sight.calls;
  for (int i = 0; i < 20; ++i) {
    perception.advance(clock.publish(0.2F, {false, false}), input, sight);
  }
  require(sight.calls == before_pause && perception.state().target == 1,
          "Pause neither scans nor expires retained target");
  actors[0].position = {0, 0, -3};
  advance_scan(perception, clock, input, sight);
  require(perception.state().visible && perception.state().target == 1,
          "Current target bypasses view-cone gate behind actor");
  sight.visible = false;
  actors[0].position = {0, 0, -15};
  advance_scan(perception, clock, input, sight);
  require(perception.state().knowledge == TargetKnowledge::searching,
          "Occlusion timeout and distance from last seen position lose target");
  actors[0].position = {0, 0, -100};
  for (int i = 0; i < 10; ++i) {
    perception.advance(clock.publish(0.2F, {}), input, sight);
  }
  require(perception.state().knowledge == TargetKnowledge::unknown &&
              perception.state().target == 1,
          "No nearby candidates forget knowledge but preserve source target "
          "handle");

  bw::world::WorldClock ranked_clock;
  PerceptionSchedule ranked_schedule;
  NpcPerception ranked(ranked_clock, ranked_schedule, kSettings);
  const std::array ranked_actors{
      kPlayer, PerceptionActor{2, {0, 0, 4}, 4, 1, false, false},
      PerceptionActor{3, {0, 0, 5}, 4, 1, false, false},
      PerceptionActor{4, {0, 0, 6}, 4, 1, false, false}};
  const PerceptionInput ranked_input{{}, {0, 0, 1}, {}, 2, ranked_actors};
  Sight ranked_sight;
  ranked_sight.reject_nearest = true;
  ranked.advance(ranked_clock.publish(0.2F, {}), ranked_input, ranked_sight);
  require(ranked.state().target == 2 && ranked_sight.calls == 2,
          "Occluded highest score yields to next visible ranked target");
  bw::world::WorldClock hidden_clock;
  PerceptionSchedule hidden_schedule;
  NpcPerception hidden(hidden_clock, hidden_schedule, kSettings);
  Sight hidden_sight;
  hidden_sight.visible = false;
  hidden.advance(hidden_clock.publish(0.2F, {}), ranked_input, hidden_sight);
  require(hidden_sight.calls == 3 && !hidden.state().target &&
              hidden.state().knowledge == TargetKnowledge::unknown,
          "Visibility tests stop at native top-three and do not acquire hidden "
          "targets");
  perception.receive_target(1, TargetKnowledge::searching, actors);
  require(
      perception.state().target == 1 &&
          perception.state().knowledge == TargetKnowledge::searching &&
          perception.state().last_seen_position == actors[0].position &&
          !perception.state().visible,
      "Target delivery refreshes remembered identity without a sight claim");
  perception.receive_target(std::nullopt, TargetKnowledge::known, actors);
  require(perception.state().target == 1,
          "Null source replacement preserves current target identity");
}
