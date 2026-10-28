// Execute prepared original control/selection records and native action slots.
// Core E.4 (#re-design-invariants) rejects malformed transfers before action
// mutation. MEM.9 keeps scheduling and suspended actions bounded;
// implementation corpora reread. Native actions own concrete behavior; this
// file owns cadence.
// A terminal selection is a valid source result, including registered action
// zero (noname); it ends descent before priority and lifecycle dispatch. Core
// E.4 and performance MEM.9 were reread for this transition: preserve that
// invariant without extra storage or weakening bounds/control-cycle checks.
#include "behavior_runtime.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>

#include "bw/content/formats/binary_reader.hpp"
namespace bw::game {
namespace {
constexpr std::int32_t kPositiveScoreInterrupt = 5;
// Original fixed event-entry arbitration priorities, separate from authored
// per-node priority. The source table has exactly entries zero through 44.
constexpr std::array<int, 45> kEventPriority{
    -1,  0,   500, 0,   0,   0,   0,   0,   131, 132, 133, 140, 122, 400, 50,
    0,   120, 0,   400, 390, 380, 10,  50,  0,   900, 800, 500, 500, 500, 450,
    450, 141, 600, 250, 400, 250, 700, 250, 420, 400, 400, 150, 100, 300, 130};
std::size_t restart_branch(std::span<const formats::BehaviorNode> nodes,
                           std::size_t current) {
  auto branch = current;
  while (true) {
    const auto parent = nodes[branch].parent;
    if (!parent || *parent == 0) {
      break;
    }
    if (nodes[branch].eligibility_mode == 2 ||
        nodes[branch].eligibility_mode == 4) {
      break;
    }
    branch = *parent;
  }
  return branch;
}
std::size_t label_target(const formats::BehaviorTreeDefinition& definition,
                         const formats::BehaviorNode& node) {
  if (node.parameters.size() < sizeof(std::uint32_t)) {
    throw std::invalid_argument("Behavior control has no label parameter");
  }
  const auto label = resources::detail::Reader(node.parameters).u32(0);
  if (label >= definition.labels().size()) {
    throw std::out_of_range("Behavior control label is outside definition");
  }
  return definition.labels()[label];
}
}  // namespace
void BehaviorSchedule::advance() {
  for (std::size_t i = 0; i + 1 < counts_.size(); ++i) {
    counts_.at(i) = counts_.at(i + 1);
  }
  counts_.back() = 0;
}
std::uint8_t BehaviorSchedule::next_evaluation() {
  std::size_t best = 1;
  for (auto i = counts_.size() - 2; i > 0; --i) {
    if (counts_.at(i) <= counts_.at(best)) {
      best = i;
    }
  }
  if (counts_.at(best) == std::numeric_limits<std::uint32_t>::max()) {
    throw std::overflow_error("Behavior schedule exhausted");
  }
  ++counts_.at(best);
  return static_cast<std::uint8_t>(best);
}
std::uint8_t BehaviorSchedule::next_dispatch() noexcept {
  const auto result = dispatch_;
  dispatch_ = dispatch_ == 0 ? 1 : 0;
  return result;
}
BehaviorRuntime::BehaviorRuntime(const world::WorldClock& clock,
                                 BehaviorSchedule& schedule,
                                 BehaviorActions& actions)
    : clock_(clock),
      schedule_(schedule),
      actions_(actions),
      dispatch_skip_(schedule.next_dispatch()) {
  for (std::size_t i = 0; i < slots_.size(); ++i) {
    slots_.at(i).host_slot = i;
  }
}
void BehaviorRuntime::start(const formats::BehaviorTreeDefinition& definition,
                            BehaviorScoring& scoring, std::size_t entry) {
  if (entry == 0 || entry > definition.entries().size()) {
    throw std::out_of_range("Behavior entry is outside definition");
  }
  stop();
  definition_ = &definition;
  scoring_ = &scoring;
  select(definition.entries()[entry - 1], true, {});
}
void BehaviorRuntime::stop() {
  // Exit from the active action down through suspended owners before releasing
  // their borrowed resource. The actor explicitly retires this before its host.
  for (auto i = depth_ + 1; i > 0; --i) {
    auto& slot = slots_.at(i - 1);
    if (slot.entered) {
      actions_.exit(slot.host_slot);
    }
    slot = {.host_slot = i - 1};
  }
  for (std::size_t i = 0; i < slots_.size(); ++i) {
    slots_.at(i).host_slot = i;
  }
  depth_ = 0;
  pending_event_.reset();
  definition_ = nullptr;
  scoring_ = nullptr;
}
std::optional<std::size_t> BehaviorRuntime::current_node() const noexcept {
  if (!definition_ || !std::span{slots_}[depth_].entered) {
    return {};
  }
  return std::span{slots_}[depth_].node;
}
float BehaviorRuntime::elapsed_seconds() const noexcept {
  return std::span{slots_}[depth_].elapsed;
}
bool BehaviorRuntime::previous_labels(BehaviorLabelQuery query) const noexcept {
  const auto [depth, label] = query;
  if (!definition_ || label < 0) return false;
  const auto prior = depth > 0 ? static_cast<std::size_t>(depth) : 0;
  if (prior > depth_) return false;
  const auto& slot = std::span{slots_}[depth_ - prior];
  if (!slot.entered && !slot.retained) return false;
  // Original first compares the stored native node byte offset, then resolves
  // a label index and checks containment within its prepared subtree.
  constexpr std::size_t kOriginalNodeBytes = 12;
  if (static_cast<std::size_t>(label) == slot.node * kOriginalNodeBytes) {
    return true;
  }
  const auto labels = definition_->labels();
  if (static_cast<std::size_t>(label) >= labels.size()) return false;
  const auto branch = labels[static_cast<std::size_t>(label)];
  return branch <= slot.node &&
         slot.node < definition_->nodes()[branch].subtree_end;
}
std::size_t BehaviorRuntime::current_entry() const {
  const auto current = current_node();
  if (!current) return 0;
  const auto entries = definition_->entries();
  for (std::size_t i = 0; i + 1 < entries.size(); ++i) {
    if (*current < entries[i]) return i;
  }
  return entries.empty() ? 0 : entries.size() - 1;
}
bool BehaviorRuntime::request_event(std::size_t entry, bool target_known) {
  if (!definition_ || entry == 0 || entry >= kEventPriority.size()) {
    throw std::invalid_argument("Invalid original behavior event entry");
  }
  const auto current = current_entry();
  if (current >= kEventPriority.size()) {
    throw std::logic_error("Current behavior entry lacks source priority");
  }
  constexpr int kKnownTargetPriorityFloor = 121;
  auto threshold = kEventPriority.at(current);
  if (const auto node = current_node()) {
    threshold = std::max(
        threshold, static_cast<int>(definition_->nodes()[*node].priority));
  }
  if (target_known) threshold = std::max(threshold, kKnownTargetPriorityFloor);
  if (pending_event_) {
    threshold = std::max(threshold, kEventPriority.at(*pending_event_));
  }
  if (entry == current || kEventPriority.at(entry) <= threshold) return false;
  pending_event_ = entry;
  return true;
}
void BehaviorRuntime::enter_event(std::size_t entry) {
  const auto entries = definition_->entries();
  if (entry > entries.size()) return;
  const auto branch = entries[entry - 1];
  const auto& root = definition_->nodes()[branch];
  if (!root.first_child) return;
  auto& current = slots_.at(depth_);
  const bool inside = branch <= current.node && current.node < root.subtree_end;
  if (current.entered) {
    actions_.exit(current.host_slot);
    current.entered = false;
    current.retained = true;
  }
  if (!inside) {
    if (depth_ + 1 == slots_.size()) {
      const auto reused = slots_.front().host_slot;
      if (slots_.front().entered) actions_.exit(reused);
      for (std::size_t i = 1; i < slots_.size(); ++i) {
        slots_.at(i - 1) = slots_.at(i);
      }
      slots_.at(depth_) = {.host_slot = reused};
    } else {
      ++depth_;
    }
  }
  const auto host = slots_.at(depth_).host_slot;
  slots_.at(depth_) = {.host_slot = host, .event = true};
  select(branch, true, {});
}
void BehaviorRuntime::select(std::size_t branch, bool force,
                             BehaviorActionState state) {
  const auto nodes = definition_->nodes();
  auto current = current_node();
  auto minimum_priority = current ? nodes[*current].priority : 0;
  // Control-only cycles are malformed for this synchronous evaluator. Bound
  // traversal by prepared nodes and stack capacity instead of hanging a frame.
  const auto budget = nodes.size() * slots_.size();
  bool direct_label_action = false;
  bool terminal_selection = false;
  for (std::size_t steps = 0; steps < budget; ++steps) {
    if (branch >= nodes.size()) {
      throw std::out_of_range("Behavior branch outside tree");
    }
    const auto& node = nodes[branch];
    bool descend = node.action == 0 && !terminal_selection;
    if (current && branch == *current && !force) {
      if (!node.first_child) {
        return;
      }
      minimum_priority = 0;
      if (node.evaluation_mode == kPositiveScoreInterrupt) {
        if (!state.can_reconsider) {
          return;
        }
        const auto candidate = select_behavior_child(
            *definition_, {branch, current, state.completed, true, true},
            *scoring_);
        if (!(candidate.score > 0)) {
          return;
        }
      } else if (node.evaluation_mode >= 1 && node.evaluation_mode <= 3) {
        if (!state.completed) {
          return;
        }
      } else if (!state.can_reconsider) {
        return;
      }
      descend = true;
    } else if (current && !force && branch < *current &&
               *current < node.subtree_end) {
      descend = true;
    }
    if (descend) {
      scoring_->begin_level();
      const auto child = select_behavior_child(
          *definition_, {branch, current, state.completed, force, false},
          *scoring_);
      if (child.node == branch && !child.terminal) {
        throw std::invalid_argument("Behavior descent made no progress");
      }
      branch = child.node;
      if (!child.terminal && current && state.completed &&
          nodes[*current].first_child &&
          (nodes[*current].eligibility_mode == 1 ||
           nodes[*current].eligibility_mode == 3) &&
          nodes[branch].parent == nodes[*current].parent) {
        branch = *current;
      }
      direct_label_action = false;
      terminal_selection = child.terminal;
      continue;
    }
    if (node.action < 0) {
      if (node.action != -1 && node.action != -2 && node.action != -3) {
        throw std::invalid_argument("Unsupported original behavior control");
      }
      const auto target = label_target(*definition_, node);
      if (node.action == -2) {
        if (std::span{slots_}[depth_].entered) {
          actions_.suspend(active_slot());
        }
        if (depth_ + 1 == slots_.size()) {
          // Source bounded stack evicts the oldest suspended owner. Keep host
          // slot identities stable while shifting only our value metadata.
          const auto reused = slots_.at(0).host_slot;
          if (slots_.at(0).entered) {
            actions_.exit(reused);
          }
          for (std::size_t i = 1; i < slots_.size(); ++i) {
            slots_.at(i - 1) = slots_.at(i);
          }
          std::span{slots_}[depth_] = {.host_slot = reused};
        } else {
          ++depth_;
        }
        current.reset();
      } else if (node.action == -3 && depth_ != 0) {
        for (auto i = depth_ + 1; i > 0; --i) {
          if (slots_.at(i - 1).entered) {
            actions_.exit(slots_.at(i - 1).host_slot);
          }
          slots_.at(i - 1) = {.host_slot = i - 1};
        }
        depth_ = 0;
        current.reset();
      }
      branch = target;
      direct_label_action = nodes[target].action > 0;
      terminal_selection = false;
      force = true;
      continue;
    }
    if (node.priority < minimum_priority && !direct_label_action) {
      return;
    }
    if (current && *current == branch && !state.completed && !force) {
      return;
    }
    auto& slot = std::span{slots_}[depth_];
    if (slot.entered) {
      actions_.exit(active_slot());
    }
    slot = {.node = branch, .host_slot = slot.host_slot, .event = slot.event};
    actions_.enter(active_slot(), {definition_, branch});
    slot.entered = true;
    return;
  }
  throw std::invalid_argument("Behavior control transfer cycle");
}
void BehaviorRuntime::advance(const world::WorldFrame& frame) {
  if (!frame.belongs_to(clock_) || frame.sequence() <= sequence_) {
    throw std::invalid_argument("Invalid behavior world frame");
  }
  sequence_ = frame.sequence();
  if (!frame.admission().simulation || !current_node()) {
    return;
  }
  auto& slot = std::span{slots_}[depth_];
  slot.elapsed += frame.delta_seconds();
  const auto before = actions_.state(active_slot());
  if (slot.evaluation_skip > 0) {
    --slot.evaluation_skip;
  } else {
    slot.evaluation_skip = schedule_.next_evaluation();
  }
  if (dispatch_skip_ == 0 || before.force_every_frame) {
    actions_.advance(active_slot(), {frame.delta_seconds(), slot.elapsed});
  }
  const auto after = actions_.state(active_slot());
  if (pending_event_) {
    const auto entry = *pending_event_;
    pending_event_.reset();
    enter_event(entry);
    dispatch_skip_ = dispatch_skip_ == 0 ? 1 : 0;
    return;
  }
  if (after.can_reconsider &&
      (before.completion_code != after.completion_code ||
       slot.evaluation_skip == 0)) {
    const auto entry = current_entry();
    constexpr std::size_t kFirstSystemEntry = 18, kLastSystemEntry = 20;
    if (after.completed && depth_ <= 1 && entry != 1 &&
        !(entry >= kFirstSystemEntry && entry <= kLastSystemEntry) &&
        !definition_->nodes()[slot.node].first_child) {
      const auto* definition = definition_;
      auto* scoring = scoring_;
      start(*definition, *scoring, 1);
    } else if (after.completed && depth_ > 0 &&
               !definition_->nodes()[slot.node].first_child) {
      const bool event = slot.event;
      const auto free_slot = slot.host_slot;
      actions_.exit(free_slot);
      slot = {.host_slot = free_slot};
      --depth_;
      auto& prior = slots_.at(depth_);
      if (event) {
        // Source event return re-enters the retained action chain from zero;
        // it does not resume stale action elapsed or reuse live callbacks.
        if (prior.entered) actions_.exit(prior.host_slot);
        prior.elapsed = 0;
        prior.evaluation_skip = 0;
        prior.entered = true;
        prior.retained = false;
        actions_.enter(prior.host_slot, {definition_, prior.node});
      } else {
        // An authored call returns to selection at the interrupted entry,
        // discarding that old action frame as well as the completed callee.
        const auto entries = definition_->entries();
        std::size_t interrupted_entry = entries.size() - 1;
        for (std::size_t i = 0; i + 1 < entries.size(); ++i) {
          if (prior.node < entries[i]) {
            interrupted_entry = i;
            break;
          }
        }
        if (interrupted_entry == 0) {
          throw std::invalid_argument(
              "Interrupted behavior has no source entry");
        }
        if (depth_ == 0) {
          const auto* definition = definition_;
          auto* scoring = scoring_;
          start(*definition, *scoring, interrupted_entry);
        } else {
          const auto prior_host = prior.host_slot;
          if (prior.entered) actions_.exit(prior_host);
          prior = {.host_slot = prior_host};
          --depth_;
          enter_event(interrupted_entry);
        }
      }
    } else {
      select(restart_branch(definition_->nodes(), slot.node), false, after);
    }
  }
  dispatch_skip_ = dispatch_skip_ == 0 ? 1 : 0;
}
}  // namespace bw::game
