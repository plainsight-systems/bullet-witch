// Original one-level candidate traversal and score selection. Core I.1 keeps
// lifecycle observations explicit; performance CDSA.32 consumes the immutable
// prepared hierarchy with no temporary allocation. Both implementation corpora
// queried/read. Full contracts and source provenance are in the header.
#include "behavior_selection.hpp"

#include <stdexcept>

namespace bw::game {
namespace {
bool contains(std::span<const formats::BehaviorNode> nodes, std::size_t branch,
              std::size_t candidate) {
  return candidate >= branch && candidate < nodes[branch].subtree_end;
}
std::optional<std::size_t> eligible(
    std::span<const formats::BehaviorNode> nodes,
    std::optional<std::size_t> candidate,
    const BehaviorSelectionRequest& request) {
  while (candidate) {
    const auto& node = nodes[*candidate];
    // Mode4 is never offered. Completed mode3 is excluded only when it owns
    // the current action subtree; unrelated mode3 branches remain candidates.
    if (node.eligibility_mode != 4 &&
        !(request.action_completed && node.eligibility_mode == 3 &&
          request.current && contains(nodes, *candidate, *request.current))) {
      return candidate;
    }
    candidate = node.next_sibling;
  }
  return {};
}
}  // namespace
BehaviorSelection select_behavior_child(
    const formats::BehaviorTreeDefinition& definition,
    const BehaviorSelectionRequest& request, BehaviorScoring& scoring) {
  const auto nodes = definition.nodes();
  if (request.branch >= nodes.size() ||
      (request.current && *request.current >= nodes.size())) {
    throw std::out_of_range("Behavior selection node is outside definition");
  }
  const auto first = nodes[request.branch].first_child;
  if (!first) return {request.branch, 0, true};
  // Existing mode1/3 branch continuation can avoid evaluating Lua altogether.
  for (auto child = first; child; child = nodes[*child].next_sibling) {
    if (!request.current || !contains(nodes, *child, *request.current)) {
      continue;
    }
    const auto mode = nodes[*child].eligibility_mode;
    if ((mode == 1 || mode == 3) && !request.force_reconsideration &&
        (!request.action_completed || nodes[*request.current].first_child)) {
      return {*child, 0, false};
    }
    break;
  }
  const auto selected = eligible(nodes, first, request);
  if (!selected) return {*first, 0, true};
  auto next = eligible(nodes, nodes[*selected].next_sibling, request);
  BehaviorSelection result{*selected, 0, false};
  if (next || request.evaluate_single) {
    result.score = scoring.evaluate(nodes[*selected]);
  }
  while (next) {
    const auto score = scoring.evaluate(nodes[*next]);
    // The original uses strict comparison; equal scores retain earlier nodes.
    if (result.score < score) result = {*next, score, false};
    next = eligible(nodes, nodes[*next].next_sibling, request);
  }
  return result;
}
}  // namespace bw::game
