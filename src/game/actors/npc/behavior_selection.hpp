// Shared authored AI child selection, reconstructed from 502e30/503cd0.
// Borrows prepared immutable nodes and a synchronous score capability only
// during select; owns no actor, Lua VM, action lifecycle, world clock or query.
// The action controller supplies completion/reconsideration facts, and keeps
// priority/label/stack transitions outside this one-level selection algorithm.
// Preserve continuation modes, original no-candidate/leaf results, single-child
// bypass and first-on-equal-score ordering. Inputs must be validated tree
// metadata; invalid caller indexes throw. Scoring failures propagate, never
// produce a pretend valid action. O(direct children), allocation-free here;
// the scorer owns its independent Lua budget. Core I.1 (#ri-explicit) states
// authority; CDSA.32 (guidelines/cpu-dsa/
// CDSA.32-transform-static-data-into-the-consumer-layout.md) uses prepared
// links rather than reparsing bytes. Both corpora consulted before design.
#pragma once
#include "bw/content/formats/behavior_tree.hpp"

namespace bw::game {
struct BehaviorSelectionRequest {
  std::size_t branch{};
  std::optional<std::size_t> current;
  bool action_completed{}, force_reconsideration{}, evaluate_single{};
};
struct BehaviorSelection {
  std::size_t node{};
  float score{};
  bool terminal{};
};
class BehaviorScoring {
 public:
  BehaviorScoring() = default;
  virtual ~BehaviorScoring() = default;
  BehaviorScoring(const BehaviorScoring&) = delete;
  BehaviorScoring& operator=(const BehaviorScoring&) = delete;
  BehaviorScoring(BehaviorScoring&&) = delete;
  BehaviorScoring& operator=(BehaviorScoring&&) = delete;
  // Runtime calls once before each full authored descent, even if a single
  // child bypasses score evaluation. Preliminary interrupt probes do not call.
  virtual void begin_level() = 0;
  virtual float evaluate(const formats::BehaviorNode&) = 0;
};
BehaviorSelection select_behavior_child(const formats::BehaviorTreeDefinition&,
                                        const BehaviorSelectionRequest&,
                                        BehaviorScoring&);
}  // namespace bw::game
