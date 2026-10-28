// Synthetic wrapper contracts cover byte-offset ownership and malformed links;
// original-wrapper acceptance is separate research evidence. Core ES.103
// (#res-overflow) checks corrupt extents before use; performance CDSA.32 keeps
// prepared traversal metadata independent of runtime interpretation.
#include "bw/content/formats/behavior_tree.hpp"

#include "../../../../fixtures/behavior_fixture.hpp"
using behavior_fixture::fixture;
using camera_fixture::require;

void behavior_tree_tests() {
  auto domain =
      bw::memory::Domain::create(std::make_shared<camera_fixture::Backing>());
  const auto bytes = fixture();
  const auto tree = bw::formats::BehaviorTreeDefinition::parse(bytes, domain);
  require(tree.nodes().size() == 2 && tree.nodes()[0].first_child == 1 &&
              tree.nodes()[1].parent == 0 && !tree.nodes()[1].next_sibling &&
              tree.nodes()[1].subtree_end == 2,
          "Behavior preorder resolves child, parent and terminal sibling");
  require(tree.entries()[0] == 1 && tree.labels()[0] == 1,
          "Calls and labels retain distinct original offset conventions");
  require(tree.nodes()[1].action == 7 && tree.nodes()[1].priority == 3 &&
              tree.nodes()[1].action_name == "wait" &&
              tree.nodes()[1].evaluator == "ok" &&
              tree.nodes()[1].parameters.size() == 4 &&
              tree.nodes()[1].parameters[0] == 42 &&
              tree.bytecode().size() == 4,
          "Behavior definition preserves action, evaluator and bytecode");
  require(tree.string(-1).empty() && tree.string(1).empty(),
          "Original missing-string getter remains empty");
  require(tree.attached_actions(0).empty() && tree.attached_actions(1).empty(),
          "Empty companion lists gained actions");
  const std::array nodes{
      behavior_fixture::Node{.extent = 20, .parent = 0xffffffffU, .mode = 1},
      behavior_fixture::Node{.extent = 8,
                             .parent = 12,
                             .mode = 1,
                             .attached = {{.action = 41, .parameter = 11},
                                          {.action = 42, .parameter = 22}}}};
  auto companion_bytes = fixture(nodes);
  const auto companions =
      bw::formats::BehaviorTreeDefinition::parse(companion_bytes, domain);
  const auto actions = companions.attached_actions(1);
  require(actions.size() == 2 && actions[0].action == 41 &&
              actions[0].parameters[0] == 11 && actions[1].action == 42 &&
              actions[1].parameters[0] == 22 &&
              actions[0].action_name == "wait" &&
              companions.nodes().size() == 2,
          "Ordered companion actions became lost or selectable tree nodes");
  const std::array<std::uint8_t, 4> subd{'s', 'u', 'b', 'd'};
  auto directory = std::search(companion_bytes.begin(), companion_bytes.end(),
                               subd.begin(), subd.end());
  require(directory != companion_bytes.end(), "Fixture has no companion chunk");
  camera_fixture::word(
      companion_bytes,
      static_cast<std::size_t>(directory - companion_bytes.begin()) + 4,
      0xffffffffU);
  bool rejected_companions = false;
  try {
    (void)bw::formats::BehaviorTreeDefinition::parse(companion_bytes, domain);
  } catch (const bw::resources::Error&) {
    rejected_companions = true;
  }
  require(rejected_companions, "Companion envelope escaped its owning record");
  for (const auto& invalid :
       {fixture({.parent = 0}), fixture({.extent = 0xffffffffU}),
        fixture({.call = 0xffffffffU})}) {
    bool rejected = false;
    try {
      (void)bw::formats::BehaviorTreeDefinition::parse(invalid, domain);
    } catch (const bw::resources::Error&) {
      rejected = true;
    }
    require(rejected, "Behavior corrupt links and wide extents are rejected");
  }
}
