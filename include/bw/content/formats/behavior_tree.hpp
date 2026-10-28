// Original behavior resource schema; execution and game action policy are
// separate consumers. Caller retains immutable wrapper bytes for this value
// and all published name/parameter/Lua views. Domain-owned metadata resolves
// original byte-offset links into node indexes once. No VM or actor is created.
// Validate chunk/ref tables, preorder containment, labels and entry points;
// then publish const nodes, original action/mode values and embedded bytecode.
// Malformed bounds/links throw resources::Error before publication. Missing
// source action names stay empty; unknown actions are not classified here.
// O(bytes + nodes + references) setup, O(1) indexed access, no runtime decode
// or allocation. Immutable views permit concurrent reads while backing lives.
// Retain each node's ordered subd companion actions separately from selectable
// children. One companion level matches native preparation; nested lists fail
// explicitly instead of disappearing. Companion metadata uses the same domain.
// Design Core I.1 (#ri-explicit), ES.103 (#res-overflow) makes ownership and
// subtraction-first bounds explicit. Performance MEM.6 (guidelines/memory/
// MEM.6-inject-allocators-behind-one-interface.md) injects metadata storage;
// CDSA.32 (guidelines/cpu-dsa/
// CDSA.32-transform-static-data-into-the-consumer-layout.md) resolves the
// traversal layout once for all actors. See ai-runtime.md native getters.
#pragma once
#include "bw/content/formats/common.hpp"
#include "bw/memory/allocator.hpp"

namespace bw::formats {
struct BehaviorAttachedAction {
  std::int32_t action{};
  std::string_view action_name;
  std::span<const std::uint8_t> parameters;
};
struct BehaviorNode {
  std::optional<std::size_t> parent, first_child, next_sibling;
  std::size_t subtree_end{};
  std::int32_t action{}, eligibility_mode{}, evaluation_mode{};
  std::int16_t priority{};
  std::string_view action_name, evaluator;
  std::span<const std::uint8_t> parameters;
  std::size_t attached_begin{}, attached_count{};
};
class BehaviorTreeDefinition {
 public:
  static BehaviorTreeDefinition parse(std::span<const std::uint8_t>,
                                      const std::shared_ptr<memory::Domain>&);
  std::span<const BehaviorNode> nodes() const { return nodes_; }
  // Ordered companion actions execute alongside the selected primary action;
  // they do not become tree-selection candidates. Views live with this owner.
  std::span<const BehaviorAttachedAction> attached_actions(
      std::size_t node) const;
  // Source call numbers are one-based; this span stores their resolved targets
  // in order, so callers validate before subtracting one.
  std::span<const std::size_t> entries() const { return entries_; }
  std::span<const std::size_t> labels() const { return labels_; }
  std::span<const std::uint8_t> bytecode() const { return bytecode_; }
  // String indexes also occur in native action parameters. The source returns
  // an empty string for a negative or out-of-range index.
  std::string_view string(std::int32_t index) const;

 private:
  explicit BehaviorTreeDefinition(const std::shared_ptr<memory::Domain>&);
  memory::Vector<BehaviorNode> nodes_;
  memory::Vector<BehaviorAttachedAction> attached_;
  memory::Vector<std::size_t> entries_, labels_;
  memory::Vector<std::string_view> strings_;
  std::span<const std::uint8_t> bytecode_;
};
}  // namespace bw::formats
