// Source selection contracts exercised independently of Lua/actor policy.
// Core I.1 separates scoring side effects from selection; CDSA.32 consumes
// prepared nodes. Both phase corpora read with implementation.
#include "game/actors/npc/behavior_selection.hpp"

#include "../../../../fixtures/behavior_fixture.hpp"
#include "game/actors/npc/behavior_lua.hpp"
namespace {
using behavior_fixture::Node;
using bw::game::BehaviorLuaScoring;
using bw::game::BehaviorScoring;
using bw::game::select_behavior_child;
using camera_fixture::require;
class Scores final : public BehaviorScoring {
 public:
  void begin_level() override {}
  Scores(std::initializer_list<float> input) : values_(input) {}
  float evaluate(const bw::formats::BehaviorNode&) override {
    return values_.at(calls_++);
  }
  std::size_t calls() const { return calls_; }

 private:
  std::vector<float> values_;
  std::size_t calls_{};
};
class Level final : public bw::game::BehaviorLevelPreparation {
 public:
  std::size_t calls{};
  void prepare_level() override { ++calls; }
};
}  // namespace
void behavior_selection_tests() {
  const auto domain =
      bw::memory::Domain::create(std::make_shared<camera_fixture::Backing>());
  const auto bytes = behavior_fixture::fixture(
      std::array{Node{44, 0xffffffffU, 0}, Node{8, 12, 1}, Node{8, 24, 3},
                 Node{8, 36, 4}});
  const auto tree = bw::formats::BehaviorTreeDefinition::parse(bytes, domain);
  Scores tied{2, 2};
  const auto equal = select_behavior_child(tree, {}, tied);
  require(equal.node == 1 && equal.score == 2 && tied.calls() == 2,
          "Equal scores retain first eligible child; mode4 excluded");
  Scores greater{2, 3};
  require(select_behavior_child(tree, {}, greater).node == 2,
          "Strictly greater candidate replaces earlier selection");
  Scores unused{};
  require(select_behavior_child(tree, {.current = 1}, unused).node == 1 &&
              unused.calls() == 0,
          "Unfinished retained mode1 skips all score callbacks");
  const auto single = select_behavior_child(
      tree, {.current = 2, .action_completed = true}, unused);
  require(single.node == 1 && single.score == 0 && unused.calls() == 0,
          "Completed mode3 excluded; single eligible child bypasses score");
  Scores forced{9};
  require(select_behavior_child(
              tree,
              {.current = 2, .action_completed = true, .evaluate_single = true},
              forced)
                  .score == 9,
          "Single-child evaluation remains an explicit source gate");
  require(select_behavior_child(tree, {.branch = 1}, unused).terminal,
          "Leaf returns terminal without scoring");
  const auto excluded_bytes = behavior_fixture::fixture(
      std::array{Node{20, 0xffffffffU, 0}, Node{8, 12, 4}});
  const auto excluded =
      bw::formats::BehaviorTreeDefinition::parse(excluded_bytes, domain);
  const auto result = select_behavior_child(excluded, {}, unused);
  require(result.node == 1 && result.terminal && unused.calls() == 0,
          "No eligible candidate retains original first-child terminal result");
  bw::script::Context context(domain);
  context.load_source(
      "function ok(owner) return owner+1 end "
      "function text(owner) return '99' end "
      "function object(owner) return {} end");
  Level preparation;
  BehaviorLuaScoring lua(context, 42, preparation);
  lua.begin_level();
  require(preparation.calls == 1,
          "Level preparation runs through its narrow capability");
  require(lua.evaluate(tree.nodes()[1]) == 43,
          "Original evaluator receives exact actor context identity");
  auto nonnumeric = tree.nodes()[1];
  nonnumeric.evaluator = "text";
  require(lua.evaluate(nonnumeric) == 0, "Numeric strings are not scores");
  nonnumeric.evaluator = "object";
  require(lua.evaluate(nonnumeric) == 0, "Object returns are zero scores");
  require(preparation.calls == 1,
          "Sibling score calls never refresh level random");
}
