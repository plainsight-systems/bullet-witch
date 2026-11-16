#ifndef BW_INPUT_PLAYER_INPUT_HPP
#define BW_INPUT_PLAYER_INPUT_HPP

#include "bw/input/key_state.hpp"
#include "bw/input/keys.hpp"
#include "bw/input/modifiers.hpp"
#include "bw/input/triggers.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>

// Responsibility: one player's actions and the contexts that map keys to
// them (docs/architecture/input.md, invariants 3 to 9 and 12). The game
// hands its actions and contexts to the load functions as tables; the
// player's controller calls evaluate once a tick; game code reads action
// states or binds functions to them, and never reads a key.
//
// Contract.
//   1. load_actions(actions) adds the actions, in order. Each has a name,
//      a value type, a rule for combining several mappings, its own
//      modifiers and whether it consumes its keys. DuplicateAction names
//      an action loaded twice; ActionsFull at capacity. On an error
//      nothing of the call is loaded.
//   2. load_context(context) loads a context: a name and its mappings,
//      each naming an action and a key by name, with modifiers and
//      triggers. UnknownAction and UnknownKey carry the name that does not
//      exist; DuplicateContext, ContextsFull and MappingsFull as named. On
//      an error the context is not loaded. The description's storage is
//      copied; nothing is kept of the caller's.
//   3. add_context(context, priority) applies a loaded context to the
//      player. It takes effect at the start of the next evaluate, also
//      when called from a bound function. Adding an applied context
//      changes its priority. UnknownContext for an id not loaded.
//   4. evaluate(keys), once a tick, in this order:
//      a. The applied contexts are taken by priority, highest first, equal
//         priorities in the order they were added.
//      b. Each mapping of a context whose key is not hidden is run when
//         its key is down, was pressed this frame or reads a value other
//         than zero, or did at the evaluate before: the key's value as
//         the action's type (a Button or Axis1D key gives x; an Axis2D
//         key x and y), through the mapping's modifiers in
//         order, then its triggers (triggers.hpp, contracts 2 to 4). An
//         idle key's mapping is not run and leaves the action unreached. A
//         mapping whose modified value is not exactly zero joins the
//         action's value by the action's rule: HighestMagnitude takes, per axis,
//         the value of largest magnitude, the later mapping on a tie; Sum
//         adds. The action's firing is made in 4d.
//      c. After a whole context has run, the keys of its mappings whose
//         action consumes are hidden from every context below. Several
//         mappings of one key inside one context all run.
//      d. Each action's own modifiers run on its joined value. Of the
//         mappings that ran, one is kept: a mapping that fired before one
//         that did not, and among equals one with a trigger before one
//         without. When the kept mapping has a trigger the action fires as
//         it did. When it has none, the action fires as it did, unless the
//         action's own modifiers changed the value: then the action fires
//         when the changed value is not zero. An action no mapping reached
//         reads zero and does not fire.
//      e. Each action's events are made from last frame's and this
//         frame's firing: Started and Fired on the first frame it fires,
//         Fired on each frame after, Completed on the first frame it does
//         not.
//      f. Bound functions are called in one queue: the bindings are
//         walked in the order bound, across all actions; a binding to
//         Started whose action has Started this frame goes to the front of
//         the queue, so the last-bound such binding is called first; every
//         other binding whose event the frame holds is called after them,
//         in the order bound.
//      A mapping's trigger memory is kept while its context is loaded,
//      whether or not the mapping ran.
//   5. state(action) is the action's events and value as of the last
//      evaluate; before the first, no events and zero.
//   6. bind(action, event, function, user) calls `function(user, state)`
//      as contract 4f says. BindingsFull at capacity. UnknownAction for an
//      id that names no action. `user` is observed.
//   7. Failures are InputError by name with the offending name; no call
//      falls back.
//
// Core guidelines read: I.11 and R.1 (created into a unique_ptr), E.27
// (named errors with the name), R.3 (`user` is observed), F.24 (tables in
// by span). Performance guidelines read: MEM.9 (tables sized at creation;
// evaluate allocates nothing), EMB.2 (fixed capacities).
namespace bw::input {

enum class Combine : std::uint8_t { HighestMagnitude, Sum };

struct ActionDescription {
  std::string_view name{};
  ValueType type{ValueType::Switch};
  Combine combine{Combine::HighestMagnitude};
  std::span<const Modifier> modifiers{};
  bool consumes{true};
};

struct MappingDescription {
  std::string_view action{};
  std::string_view key{};
  std::span<const Modifier> modifiers{};
  std::span<const Trigger> triggers{};
};

struct ContextDescription {
  std::string_view name{};
  std::span<const MappingDescription> mappings{};
};

struct ActionId {
  std::uint32_t value{0xFFFFFFFFU};
  [[nodiscard]] friend constexpr bool operator==(ActionId, ActionId) noexcept = default;
};
struct ContextId {
  std::uint32_t value{0xFFFFFFFFU};
  [[nodiscard]] friend constexpr bool operator==(ContextId, ContextId) noexcept = default;
};

// The events of one frame, as bits.
enum class ActionEvent : std::uint8_t { Started = 1, Fired = 2, Completed = 4 };

struct ActionState {
  ActionId action{};
  // ActionEvent bits.
  std::uint8_t events{};
  ActionValue value{};
  [[nodiscard]] constexpr bool has(ActionEvent event) const noexcept {
    return (events & static_cast<std::uint8_t>(event)) != 0U;
  }
};

using ActionFunction = void (*)(void* user, const ActionState& state);

enum class InputError : std::uint8_t {
  None,
  DuplicateAction,
  ActionsFull,
  UnknownAction,
  UnknownKey,
  DuplicateContext,
  ContextsFull,
  MappingsFull,
  UnknownContext,
  BindingsFull,
};

struct InputFailure {
  InputError error{InputError::None};
  // The action, key or context name the error is about; empty otherwise.
  std::string name{};
  [[nodiscard]] bool ok() const noexcept { return error == InputError::None; }
};

struct ContextResult {
  ContextId id{};
  InputFailure failure{};
  [[nodiscard]] bool ok() const noexcept { return failure.ok(); }
};

struct PlayerInputDescription {
  std::uint32_t max_actions{32};
  std::uint32_t max_contexts{8};
  std::uint32_t max_mappings{128};
  std::uint32_t max_bindings{64};
};

class PlayerInput {
public:
  [[nodiscard]] static std::unique_ptr<PlayerInput>
  create(const PlayerInputDescription& description);
  PlayerInput(const PlayerInput&) = delete;
  PlayerInput& operator=(const PlayerInput&) = delete;
  ~PlayerInput();

  [[nodiscard]] InputFailure load_actions(std::span<const ActionDescription> actions);
  [[nodiscard]] ContextResult load_context(const ContextDescription& context);
  [[nodiscard]] InputError add_context(ContextId context, std::int32_t priority);
  [[nodiscard]] InputError bind(ActionId action, ActionEvent event, ActionFunction function,
                                void* user);

  // Contract 4.
  void evaluate(const KeyState& keys);

  // The action of that name, or an id equal to ActionId{} when none.
  [[nodiscard]] ActionId find_action(std::string_view name) const noexcept;
  [[nodiscard]] std::size_t action_count() const noexcept;
  // Precondition for the three: the id is an action of this player; index
  // is below action_count() and names the action loaded at that place.
  [[nodiscard]] std::string_view action_name(ActionId action) const noexcept;
  [[nodiscard]] ActionId action_at(std::size_t index) const noexcept;
  [[nodiscard]] const ActionState& state(ActionId action) const noexcept;

private:
  PlayerInput();
  struct State;
  std::unique_ptr<State> state_;
};

} // namespace bw::input
#endif
