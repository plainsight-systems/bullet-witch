// Observe original numeric Lua contracts with a query-only synthetic owner.
// E.4 checks foreign actor rejection; MEM.9 keeps fixture observations fixed.
#include "game/actors/npc/behavior_observations.hpp"

#include "../../../../fixtures/camera_fixture.hpp"
namespace {
using camera_fixture::require;
class Source final : public bw::game::BehaviorObservationSource {
 public:
  double scalar(bw::game::BehaviorObservation observation) const override {
    if (observation == bw::game::BehaviorObservation::completion_code) {
      return 255;
    }
    if (observation == bw::game::BehaviorObservation::level_random) {
      return 0.375;
    }
    return 3;
  }
  bool callback_on(std::string_view name) const override {
    return name == "meteor";
  }
  bw::game::BehaviorScriptSettings script_settings() const override {
    return {};
  }
  bool previous_labels(std::int32_t depth, std::int32_t label) const override {
    return depth == 1 && label == 3;
  }
  double path_distance_squared(std::int32_t point) const override {
    return point == -1 ? 16 : 9;
  }
  double path_angle(std::int32_t) const override { return -45; }
};
}  // namespace
void behavior_observations_tests() {
  using bw::game::behavior_script_data;
  require(behavior_script_data({}, "attack_range_near", 2) == 9 &&
              behavior_script_data({}, "ATTACK_RANGE_FAR", 2) == 400 &&
              behavior_script_data({}, "move_range_suffix", 1) == 50 &&
              behavior_script_data({}, "unknown", 2) == -1,
          "Original ScriptData defaults, prefix matching and power behavior");
  require(behavior_script_data({.group_number = 7, .attack_range_middle = 12},
                               "group_number", 0) == 7 &&
              behavior_script_data({.attack_range_middle = 12},
                                   "attack_range_middle", 2) == 144,
          "Authored values override range defaults and power clamps at one");
  const auto domain =
      bw::memory::Domain::create(std::make_shared<camera_fixture::Backing>());
  bw::script::Context context(domain);
  Source source;
  bw::game::bind_behavior_observations(context, 42, source);
  context.load_source(
      "function check() return getSequenceEndState(42) + getLevelRandom(42) + "
      "getScriptData(42,'attack_range_near',2) + "
      "isCallBackOn(42,'meteor') end "
      "function foreign() return getEnemyKnowledgeLevel(99) end");
  context.start("check");
  require(
      context.pump().state == bw::script::RunState::complete &&
          context.numeric_result() == 265.375,
      "Native completion outcome and numeric predicates reach Lua unchanged");
  context.start("foreign");
  require(context.pump().state == bw::script::RunState::failed,
          "Read-only binding rejects a different actor identity");
}
