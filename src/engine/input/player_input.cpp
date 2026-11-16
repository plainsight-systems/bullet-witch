// One player's actions and contexts (bw/input/player_input.hpp).
#include "bw/input/player_input.hpp"

#include "bw/input/key_state.hpp"
#include "bw/input/keys.hpp"
#include "bw/input/modifiers.hpp"
#include "bw/input/triggers.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace bw::input {

struct PlayerInput::State {
  struct Action {
    std::string name{};
    ValueType type{ValueType::Switch};
    Combine combine{Combine::HighestMagnitude};
    std::vector<Modifier> modifiers{};
    bool consumes{true};
    ActionState state{};
    // This evaluation's joined value, whether a mapping reached it and
    // whether one fired; and whether it fired in the evaluation before.
    ActionValue joined{};
    bool reached{};
    bool fired_now{};
    bool fired_before{};
    // The kept record of the mappings that ran this evaluation (contract
    // 4d): whether one ran, and the kept one's firing and whether it has a
    // trigger.
    bool kept{};
    bool kept_fired{};
    bool kept_has_trigger{};
  };
  struct Mapping {
    std::uint32_t action{};
    Key key{Key::KeyW};
    std::vector<Modifier> modifiers{};
    std::vector<Trigger> triggers{};
    // One per trigger, kept while the context is loaded (contract 4).
    std::vector<TriggerMemory> memory{};
  };
  struct Context {
    std::string name{};
    std::uint32_t first_mapping{};
    std::uint32_t mapping_count{};
  };
  struct Applied {
    std::uint32_t context{};
    std::int32_t priority{};
    // When it was first added: equal priorities keep this order.
    std::uint32_t order{};
  };
  struct Binding {
    std::uint32_t action{};
    ActionEvent event{ActionEvent::Fired};
    ActionFunction function{};
    void* user{};
  };

  PlayerInputDescription description{};
  std::vector<Action> actions{};
  std::vector<Mapping> mappings{};
  std::vector<Context> contexts{};
  // Highest priority first; equal priorities in the order first added.
  std::vector<Applied> applied{};
  // Requests made since the last evaluate; applied at its start.
  std::vector<Applied> pending{};
  std::vector<Binding> bindings{};
  std::uint32_t next_order{};
  // Whether each key was active at the last evaluate (down, pressed or
  // reading a value other than zero): its mappings run once more (contract
  // 4b).
  std::array<bool, kKeyCount> key_active_before{};

  [[nodiscard]] std::optional<std::uint32_t> action_named(std::string_view name) const noexcept {
    for (std::size_t index = 0; index < actions.size(); ++index) {
      if (actions[index].name == name) {
        return static_cast<std::uint32_t>(index);
      }
    }
    return std::nullopt;
  }

  // The pending requests into the applied list, which stays sorted with no
  // allocation (its storage is reserved at creation).
  void apply_pending() {
    for (const Applied& request : pending) {
      std::size_t at = applied.size();
      for (std::size_t index = 0; index < applied.size(); ++index) {
        if (applied[index].context == request.context) {
          at = index;
          break;
        }
      }
      if (at == applied.size()) {
        applied.push_back(request);
      } else {
        applied[at].priority = request.priority;
      }
    }
    pending.clear();
    // Insertion sort: priority descending, then order ascending.
    for (std::size_t index = 1; index < applied.size(); ++index) {
      const Applied moving = applied[index];
      std::size_t slot = index;
      while (slot > 0 && (applied[slot - 1].priority < moving.priority ||
                          (applied[slot - 1].priority == moving.priority &&
                           applied[slot - 1].order > moving.order))) {
        applied[slot] = applied[slot - 1];
        --slot;
      }
      applied[slot] = moving;
    }
  }
};

namespace {

// The joined value of an action: per axis the value of largest magnitude,
// the later on a tie, or the sum.
float join_axis(Combine combine, float current, float value) noexcept {
  if (combine == Combine::Sum) {
    return current + value;
  }
  return std::abs(value) >= std::abs(current) ? value : current;
}

// Contract 4b: a key is active when it is down, was pressed this frame or
// reads a value other than zero.
bool key_active(const KeyState& keys, Key key) noexcept {
  const KeyValue value = keys.value(key);
  return keys.down(key) || keys.presses(key) > 0U || value.x != 0.0F || value.y != 0.0F;
}

ActionValue key_value(const KeyState& keys, Key key) noexcept {
  const KeyValue value = keys.value(key);
  return key_kind(key) == KeyKind::Axis2D ? ActionValue{value.x, value.y, 0.0F}
                                          : ActionValue{value.x, 0.0F, 0.0F};
}

} // namespace

PlayerInput::PlayerInput() : state_(std::make_unique<State>()) {}
PlayerInput::~PlayerInput() = default;

std::unique_ptr<PlayerInput> PlayerInput::create(const PlayerInputDescription& description) {
  std::unique_ptr<PlayerInput> input(new PlayerInput());
  State& state = *input->state_;
  state.description = description;
  state.actions.reserve(description.max_actions);
  state.mappings.reserve(description.max_mappings);
  state.contexts.reserve(description.max_contexts);
  state.applied.reserve(description.max_contexts);
  state.pending.reserve(description.max_contexts);
  state.bindings.reserve(description.max_bindings);
  return input;
}

InputFailure PlayerInput::load_actions(std::span<const ActionDescription> actions) {
  State& state = *state_;
  for (std::size_t index = 0; index < actions.size(); ++index) {
    const ActionDescription& action = actions[index];
    if (state.action_named(action.name).has_value()) {
      return InputFailure{InputError::DuplicateAction, std::string(action.name)};
    }
    for (std::size_t earlier = 0; earlier < index; ++earlier) {
      if (actions[earlier].name == action.name) {
        return InputFailure{InputError::DuplicateAction, std::string(action.name)};
      }
    }
    if (state.actions.size() + index >= state.description.max_actions) {
      return InputFailure{InputError::ActionsFull, std::string(action.name)};
    }
  }
  for (const ActionDescription& description : actions) {
    State::Action action{};
    action.name = std::string(description.name);
    action.type = description.type;
    action.combine = description.combine;
    action.modifiers.assign(description.modifiers.begin(), description.modifiers.end());
    action.consumes = description.consumes;
    action.state.action = ActionId{static_cast<std::uint32_t>(state.actions.size())};
    state.actions.push_back(std::move(action));
  }
  return InputFailure{};
}

ContextResult PlayerInput::load_context(const ContextDescription& context) {
  State& state = *state_;
  ContextResult result{};
  for (const State::Context& loaded : state.contexts) {
    if (loaded.name == context.name) {
      result.failure = InputFailure{InputError::DuplicateContext, std::string(context.name)};
      return result;
    }
  }
  if (state.contexts.size() >= state.description.max_contexts) {
    result.failure = InputFailure{InputError::ContextsFull, std::string(context.name)};
    return result;
  }
  if (state.mappings.size() + context.mappings.size() > state.description.max_mappings) {
    result.failure = InputFailure{InputError::MappingsFull, std::string(context.name)};
    return result;
  }
  // Resolved before anything is kept, so an error loads nothing.
  std::vector<State::Mapping> resolved;
  resolved.reserve(context.mappings.size());
  for (const MappingDescription& description : context.mappings) {
    const std::optional<std::uint32_t> action = state.action_named(description.action);
    if (!action.has_value()) {
      result.failure = InputFailure{InputError::UnknownAction, std::string(description.action)};
      return result;
    }
    const std::optional<Key> key = key_from_name(description.key);
    if (!key.has_value()) {
      result.failure = InputFailure{InputError::UnknownKey, std::string(description.key)};
      return result;
    }
    State::Mapping mapping{};
    mapping.action = *action;
    mapping.key = *key;
    mapping.modifiers.assign(description.modifiers.begin(), description.modifiers.end());
    mapping.triggers.assign(description.triggers.begin(), description.triggers.end());
    mapping.memory.resize(mapping.triggers.size());
    resolved.push_back(std::move(mapping));
  }
  State::Context loaded{};
  loaded.name = std::string(context.name);
  loaded.first_mapping = static_cast<std::uint32_t>(state.mappings.size());
  loaded.mapping_count = static_cast<std::uint32_t>(context.mappings.size());
  for (State::Mapping& mapping : resolved) {
    state.mappings.push_back(std::move(mapping));
  }
  result.id = ContextId{static_cast<std::uint32_t>(state.contexts.size())};
  state.contexts.push_back(std::move(loaded));
  return result;
}

InputError PlayerInput::add_context(ContextId context, std::int32_t priority) {
  State& state = *state_;
  if (context.value >= state.contexts.size()) {
    return InputError::UnknownContext;
  }
  for (State::Applied& request : state.pending) {
    if (request.context == context.value) {
      request.priority = priority;
      return InputError::None;
    }
  }
  // Each context is pending at most once, so the reserved storage holds
  // every request.
  state.pending.push_back(State::Applied{context.value, priority, state.next_order});
  ++state.next_order;
  return InputError::None;
}

// Contract 6: UnknownAction for an id that names no action.
InputError PlayerInput::bind(ActionId action, ActionEvent event, ActionFunction function,
                             void* user) {
  State& state = *state_;
  if (action.value >= state.actions.size()) {
    return InputError::UnknownAction;
  }
  if (state.bindings.size() >= state.description.max_bindings) {
    return InputError::BindingsFull;
  }
  state.bindings.push_back(State::Binding{action.value, event, function, user});
  return InputError::None;
}

void PlayerInput::evaluate(const KeyState& keys) {
  State& state = *state_;
  state.apply_pending();
  for (State::Action& action : state.actions) {
    action.joined = ActionValue{};
    action.reached = false;
    action.fired_now = false;
    action.kept = false;
    action.kept_fired = false;
    action.kept_has_trigger = false;
  }
  std::array<bool, kKeyCount> hidden{};
  for (const State::Applied& applied : state.applied) {
    const State::Context& context = state.contexts[applied.context];
    const std::size_t first = context.first_mapping;
    const std::size_t end = first + context.mapping_count;
    for (std::size_t index = first; index < end; ++index) {
      State::Mapping& mapping = state.mappings[index];
      const auto key = static_cast<std::size_t>(mapping.key);
      if (hidden[key]) {
        continue;
      }
      // A mapping runs when its key is down, was pressed this frame or
      // reads a value other than zero, or was so at the evaluate before.
      if (!key_active(keys, mapping.key) && !state.key_active_before[key]) {
        continue;
      }
      State::Action& action = state.actions[mapping.action];
      ActionValue value = as_type(key_value(keys, mapping.key), action.type);
      for (const Modifier& modifier : mapping.modifiers) {
        value = apply_modifier(modifier, value, action.type);
      }
      bool fires = false;
      if (mapping.triggers.empty()) {
        fires = fires_without_trigger(value);
      } else {
        // Every trigger runs, so each remembers this frame (contract 4).
        for (std::size_t trigger = 0; trigger < mapping.triggers.size(); ++trigger) {
          fires =
              update_trigger(mapping.triggers[trigger], mapping.memory[trigger], value) || fires;
        }
      }
      // The larger record is kept, by firing and then by having a trigger.
      const bool has_trigger = !mapping.triggers.empty();
      if (!action.kept || (fires && !action.kept_fired) ||
          (fires == action.kept_fired && has_trigger && !action.kept_has_trigger)) {
        action.kept = true;
        action.kept_fired = fires;
        action.kept_has_trigger = has_trigger;
      }
      if (value.x != 0.0F || value.y != 0.0F || value.z != 0.0F) {
        action.joined = ActionValue{join_axis(action.combine, action.joined.x, value.x),
                                    join_axis(action.combine, action.joined.y, value.y),
                                    join_axis(action.combine, action.joined.z, value.z)};
        action.reached = true;
      }
    }
    for (std::size_t index = first; index < end; ++index) {
      const State::Mapping& mapping = state.mappings[index];
      if (state.actions[mapping.action].consumes) {
        hidden[static_cast<std::size_t>(mapping.key)] = true;
      }
    }
  }
  for (State::Action& action : state.actions) {
    ActionValue value{};
    if (action.reached) {
      value = action.joined;
      for (const Modifier& modifier : action.modifiers) {
        value = apply_modifier(modifier, value, action.type);
      }
    }
    // Contract 4d: the kept record decides; with no trigger, a value the
    // action's own modifiers changed decides instead.
    if (action.kept) {
      if (action.kept_has_trigger || value == action.joined) {
        action.fired_now = action.kept_fired;
      } else {
        action.fired_now = fires_without_trigger(value);
      }
    }
    std::uint8_t events = 0U;
    if (action.fired_now && !action.fired_before) {
      events = static_cast<std::uint8_t>(static_cast<std::uint8_t>(ActionEvent::Started) |
                                         static_cast<std::uint8_t>(ActionEvent::Fired));
    } else if (action.fired_now) {
      events = static_cast<std::uint8_t>(ActionEvent::Fired);
    } else if (action.fired_before) {
      events = static_cast<std::uint8_t>(ActionEvent::Completed);
    }
    action.state.events = events;
    action.state.value = value;
    action.fired_before = action.fired_now;
  }
  for (std::size_t key = 0; key < kKeyCount; ++key) {
    state.key_active_before[key] = key_active(keys, static_cast<Key>(key));
  }
  // One queue in bound order, a Started binding whose action started this
  // frame put at its front: those first, the last bound first, then every
  // other binding whose event the frame holds, in bound order. Bound
  // functions may add contexts, which go to the pending list and take
  // effect at the next evaluate (contract 3); they bind nothing here.
  const std::size_t bound = state.bindings.size();
  for (std::size_t index = bound; index > 0; --index) {
    const State::Binding& binding = state.bindings[index - 1];
    const ActionState& action_state = state.actions[binding.action].state;
    if (binding.event == ActionEvent::Started && action_state.has(ActionEvent::Started)) {
      binding.function(binding.user, action_state);
    }
  }
  for (std::size_t index = 0; index < bound; ++index) {
    const State::Binding& binding = state.bindings[index];
    const ActionState& action_state = state.actions[binding.action].state;
    if (binding.event != ActionEvent::Started && action_state.has(binding.event)) {
      binding.function(binding.user, action_state);
    }
  }
}

ActionId PlayerInput::find_action(std::string_view name) const noexcept {
  const std::optional<std::uint32_t> index = state_->action_named(name);
  return index.has_value() ? ActionId{*index} : ActionId{};
}

std::size_t PlayerInput::action_count() const noexcept { return state_->actions.size(); }

std::string_view PlayerInput::action_name(ActionId action) const noexcept {
  return state_->actions[action.value].name;
}

ActionId PlayerInput::action_at(std::size_t index) const noexcept {
  return ActionId{static_cast<std::uint32_t>(index)};
}

const ActionState& PlayerInput::state(ActionId action) const noexcept {
  return state_->actions[action.value].state;
}

} // namespace bw::input
