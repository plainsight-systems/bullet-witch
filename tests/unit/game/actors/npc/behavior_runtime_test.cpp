// Exercise behavior ownership/cadence using authored-format synthetic records.
// Core E.4 checks failures and transitions; MEM.9 keeps host slots fixed.
// This fixture is not original content or native gameplay parity evidence.
// E.4 and MEM.9 reread for terminal selection: valid action-zero transitions
// retain the bounded action lifecycle instead of failing or descending forever.
#include "game/actors/npc/behavior_runtime.hpp"

#include "../../../../fixtures/behavior_fixture.hpp"
#include "bw/content/formats/binary_reader.hpp"
namespace {
using behavior_fixture::Node;
using bw::game::BehaviorActionFrame;
using bw::game::BehaviorActionRequest;
using bw::game::BehaviorActions;
using bw::game::BehaviorActionState;
using bw::game::BehaviorRuntime;
using bw::game::BehaviorSchedule;
using bw::game::BehaviorScoring;
using camera_fixture::require;
class Scores final : public BehaviorScoring {
 public:
  std::size_t levels{};
  void begin_level() override { ++levels; }
  bool second{};
  float evaluate(const bw::formats::BehaviorNode& node) override {
    const bool first = node.parameters[0] == 1;
    return first != second ? 2.F : 1.F;
  }
};
class Actions final : public BehaviorActions {
 public:
  std::array<BehaviorActionState, 4> states{};
  std::array<bool, 4> alive{};
  std::size_t enters{}, advances{}, exits{};
  float elapsed{};
  bool finish{};
  BehaviorActionState state(std::size_t slot) const override {
    return states.at(slot);
  }
  void enter(std::size_t slot, const BehaviorActionRequest&) override {
    require(!alive.at(slot), "Native action slot is entered only after exit");
    alive.at(slot) = true;
    states.at(slot) = {};
    ++enters;
  }
  void advance(std::size_t slot, BehaviorActionFrame frame) override {
    require(alive.at(slot), "Only resident native actions advance");
    ++advances;
    elapsed = frame.elapsed_seconds;
    if (finish) {
      states.at(slot).completed = true;
      states.at(slot).completion_code = 1;
    }
  }
  void exit(std::size_t slot) override {
    require(alive.at(slot), "Native action exits exactly once");
    alive.at(slot) = false;
    ++exits;
  }
  void suspend(std::size_t slot) override {
    require(alive.at(slot), "Suspend live action");
  }
  void resume(std::size_t slot) override {
    require(alive.at(slot), "Resume live action");
  }
};
void authored_call_return(const std::shared_ptr<bw::memory::Domain>& domain) {
  // Two authored entries plus the source final sentinel distinguish entry
  // restart from resuming the interrupted action object.
  const std::array nodes{Node{80, 0xffffffffU, 0, 0},
                         Node{32, 12, 0, 0},
                         Node{8, 12, 0, 7, 1, 0, 1},
                         Node{8, 24, 0, -2, 1, 0, 0},
                         Node{20, 48, 0, 0},
                         Node{8, 12, 0, 7, 1, 0, 2},
                         Node{8, 72, 0, 0}};
  auto bytes = behavior_fixture::fixture(nodes, 8, 48);
  std::size_t tree = 8;
  while (bw::resources::detail::Reader(bytes).tag_view(tree) != "tree") {
    tree += 8 + bw::resources::detail::Reader(bytes).u32(tree + 4);
  }
  const auto tree_size = bw::resources::detail::Reader(bytes).u32(tree + 4);
  const auto root_size = bw::resources::detail::Reader(bytes).u32(4);
  bytes.insert(bytes.begin() + static_cast<std::ptrdiff_t>(tree + 20), 8, 0);
  camera_fixture::word(bytes, 4, root_size + 8);
  camera_fixture::word(bytes, tree + 4, tree_size + 8);
  camera_fixture::word(bytes, tree + 12, 12);
  camera_fixture::word(bytes, tree + 20, 44);
  camera_fixture::word(bytes, tree + 24, 68);
  const auto definition =
      bw::formats::BehaviorTreeDefinition::parse(bytes, domain);
  bw::world::WorldClock clock;
  BehaviorSchedule schedule;
  Actions actions;
  Scores scores;
  BehaviorRuntime runtime(clock, schedule, actions);
  runtime.start(definition, scores, 1);
  scores.second = true;
  actions.finish = true;
  runtime.advance(clock.publish(.1F, {}));
  require(
      runtime.current_node() == 5 && actions.enters == 2,
      "Authored gosub selects called entry while retaining interrupted owner");
  scores.second = false;
  runtime.advance(clock.publish(.1F, {}));
  runtime.advance(clock.publish(.1F, {}));
  require(runtime.current_node() == 2 && actions.enters == 3 &&
              actions.exits == 2 && runtime.elapsed_seconds() == 0,
          "Completed source call reconstructs interrupted entry instead of "
          "resuming action");
  runtime.stop();

  // Event admission exits the old action but preserves its copied frame for
  // history. Completion restarts entry one; it never resumes elapsed action.
  actions.finish = false;
  runtime.start(definition, scores, 1);
  require(runtime.request_event(2, false) && !runtime.request_event(41, false),
          "Higher pending source entry priority rejects weaker event");
  runtime.advance(clock.publish(.1F, {false, false}));
  require(runtime.current_node() == 2,
          "Pause must retain pending event without dispatch");
  runtime.advance(clock.publish(.1F, {}));
  require(runtime.current_node() == 5 && runtime.previous_labels({1, 24}),
          "Event preserves exited parent history while entering new action");
  actions.finish = true;
  for (int frame = 0; frame < 4 && runtime.current_node() != 2; ++frame) {
    runtime.advance(clock.publish(.1F, {}));
  }
  require(runtime.current_node() == 2 && runtime.elapsed_seconds() == 0,
          "Completed event resets original root entry without stale elapsed");
  actions.finish = false;
  require(!runtime.request_event(42, true) && runtime.request_event(41, true),
          "Known enemy applies original priority floor to event admission");
  runtime.advance(clock.publish(.1F, {}));
  require(runtime.current_node() == 2,
          "Absent authored event entry is a no-op, not a fabricated action");
  runtime.stop();
}
void terminal_action(const std::shared_ptr<bw::memory::Domain>& domain) {
  const std::array nodes{Node{44, 0xffffffffU, 0, 0}, Node{32, 12, 0, 0},
                         Node{8, 12, 1, 7, 1, 0, 1},
                         Node{8, 24, 1, 0, 1, 0, 2}};
  const auto bytes = behavior_fixture::fixture(nodes);
  const auto definition =
      bw::formats::BehaviorTreeDefinition::parse(bytes, domain);
  bw::world::WorldClock clock;
  BehaviorSchedule schedule;
  Actions actions;
  Scores scores;
  BehaviorRuntime runtime(clock, schedule, actions);
  runtime.start(definition, scores, 1);
  require(runtime.current_node() == 2, "Initial concrete action selected");
  scores.second = true;
  actions.finish = true;
  runtime.advance(clock.publish(.1F, {}));
  require(runtime.current_node() == 3 && actions.enters == 2 &&
              actions.exits == 1 && runtime.elapsed_seconds() == 0,
          "Completed action must transition to authored action-zero terminal");
  actions.finish = false;
  for (int frame = 0; frame < 4; ++frame) {
    runtime.advance(clock.publish(.1F, {}));
  }
  require(runtime.current_node() == 3 && actions.enters == 2 &&
              actions.exits == 1 && runtime.elapsed_seconds() > 0,
          "Unfinished terminal owner must survive ordinary reconsideration");
  const auto elapsed = runtime.elapsed_seconds();
  runtime.advance(clock.publish(1, {false, false}));
  require(runtime.elapsed_seconds() == elapsed,
          "Paused terminal action must retain local elapsed");
  runtime.stop();
  require(actions.exits == 2, "Terminal action must retire once");

  // Source selection also returns its first child as terminal when every
  // sibling is excluded. That child can itself have descendants.
  const std::array excluded_nodes{Node{44, 0xffffffffU, 0, 0},
                                  Node{32, 12, 0, 0}, Node{20, 12, 4, 0},
                                  Node{8, 12, 4, 7}};
  const auto excluded_bytes = behavior_fixture::fixture(excluded_nodes);
  const auto excluded =
      bw::formats::BehaviorTreeDefinition::parse(excluded_bytes, domain);
  runtime.start(excluded, scores, 1);
  require(runtime.current_node() == 2,
          "Terminal selection must stop before excluded child descendants");
  for (int frame = 0; frame < 10; ++frame) {
    runtime.advance(clock.publish(.1F, {}));
  }
  require(runtime.current_node() == 2 && actions.enters == 3,
          "Current terminal action must preserve source completion gates");
  runtime.stop();

  // The completed mode-3 branch first continues into its label transfer.
  // That transfer forces parent reconsideration, excluding the completed
  // branch and selecting its terminal sibling instead of restoring it.
  const std::array completed_branch_nodes{
      Node{56, 0xffffffffU, 0, 0}, Node{44, 12, 0, 0}, Node{8, 12, 4, 0},
      Node{20, 24, 3, 7}, Node{8, 12, 4, -1, 1, 0, 0}};
  const auto completed_bytes =
      behavior_fixture::fixture(completed_branch_nodes);
  const auto completed_branch =
      bw::formats::BehaviorTreeDefinition::parse(completed_bytes, domain);
  runtime.start(completed_branch, scores, 1);
  require(runtime.current_node() == 3,
          "Eligible action with children must be initially selected");
  actions.finish = true;
  for (int frame = 0; frame < 2; ++frame) {
    runtime.advance(clock.publish(.1F, {}));
  }
  require(
      runtime.current_node() == 2,
      "Terminal sibling fallback must bypass completed-branch reassignment");
  runtime.stop();
}
}  // namespace
void behavior_runtime_tests() {
  const auto domain =
      bw::memory::Domain::create(std::make_shared<camera_fixture::Backing>());
  std::array nodes{Node{44, 0xffffffffU, 0, 0}, Node{32, 12, 0, 0},
                   Node{8, 12, 0, 7, 1, 0, 1}, Node{8, 24, 0, 7, 1, 0, 2}};
  const auto bytes = behavior_fixture::fixture(nodes);
  const auto tree = bw::formats::BehaviorTreeDefinition::parse(bytes, domain);
  bw::world::WorldClock clock;
  BehaviorSchedule schedule;
  Actions actions;
  Scores scores;
  BehaviorRuntime runtime(clock, schedule, actions);
  runtime.start(tree, scores, 1);
  require(
      runtime.current_node() == 2 && actions.enters == 1,
      "One-based entry descends through authored selection to native action");
  runtime.advance(clock.publish(0.25F, {}));
  runtime.advance(clock.publish(0.25F, {}));
  require(actions.advances == 1 && runtime.elapsed_seconds() == 0.5F,
          "Action dispatch alternates while world-owned local elapsed advances "
          "every frame");
  runtime.advance(clock.publish(2, {false, false}));
  require(actions.advances == 1 && runtime.elapsed_seconds() == 0.5F,
          "Paused frame preserves action state and local elapsed");
  scores.second = true;
  actions.finish = true;
  runtime.advance(clock.publish(0.25F, {}));
  require(runtime.current_node() == 3 && actions.exits == 1 &&
              actions.enters == 2 && runtime.elapsed_seconds() == 0,
          "Completion reevaluates original branch and replaces native action "
          "owner");
  bool rejected = false;
  bw::world::WorldClock other;
  try {
    runtime.advance(other.publish(0.1F, {}));
  } catch (const std::invalid_argument&) {
    rejected = true;
  }
  require(rejected, "Foreign clock cannot advance NPC actions");
  runtime.stop();
  runtime.stop();
  require(!runtime.current_node() && actions.exits == 2,
          "Explicit retirement closes each resident action once");
  authored_call_return(domain);
  terminal_action(domain);
  BehaviorSchedule stagger;
  require(stagger.next_evaluation() == 1 && stagger.next_evaluation() == 2,
          "Original evaluation scheduler preserves low-slot ties");
}
