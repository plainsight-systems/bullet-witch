// The game's actions and default mappings (bw/game/input/actions.hpp).
#include "bw/game/input/actions.hpp"

#include "bw/input/modifiers.hpp"
#include "bw/input/player_input.hpp"
#include "bw/input/triggers.hpp"

#include <array>
#include <span>
#include <string>

namespace bw::game {
namespace {

using input::ActionDescription;
using input::ContextDescription;
using input::MappingDescription;
using input::Modifier;
using input::Trigger;
using input::ValueType;

// Move's own radial dead zone (contract 1).
constexpr std::array kMoveModifiers{
    input::dead_zone(input::DeadZoneKind::Radial, input::DeadZoneRange{kMoveDeadZone, 1.0F})};

constexpr std::array kActions{
    ActionDescription{kActionMove, ValueType::Axis2D, input::Combine::HighestMagnitude,
                      kMoveModifiers, true},
    ActionDescription{kActionLook, ValueType::Axis2D},
    ActionDescription{kActionShoot},
    ActionDescription{kActionDodge},
    ActionDescription{kActionCrouch},
    ActionDescription{kActionView},
    ActionDescription{kActionReload},
    ActionDescription{kActionMelee},
    ActionDescription{kActionFormNext},
    ActionDescription{kActionFormPrevious},
    ActionDescription{kActionRingOpen},
    ActionDescription{kActionRingLevelDown},
    ActionDescription{kActionRingLevelUp},
    ActionDescription{kActionRingCast1},
    ActionDescription{kActionRingCast2},
    ActionDescription{kActionRingCast3},
    ActionDescription{kActionRingClose},
};

// Contract 3: a stick's axial dead zone; the pad's look scale; the mouse's
// counts divisor with y flipped; the keys' move directions.
constexpr Modifier kStickAxialDeadZone =
    input::dead_zone(input::DeadZoneKind::Axial, input::DeadZoneRange{kStickDeadZone, 1.0F});
constexpr std::array kStickMove{kStickAxialDeadZone};
constexpr std::array kStickLook{
    kStickAxialDeadZone,
    input::scale(input::ActionValue{kLookSensitivity, kLookSensitivity, 1.0F})};
constexpr std::array kMouseLook{
    input::scale(input::ActionValue{1.0F / kMouseCountsDivisor, 1.0F / kMouseCountsDivisor, 1.0F}),
    input::negate(false, true, false)};
constexpr std::array kKeyForward{input::swap_xy()};
constexpr std::array kKeyBack{input::swap_xy(), input::negate(false, true, false)};
constexpr std::array kKeyLeft{input::negate(true, false, false)};

constexpr std::array kDown{Trigger{input::TriggerKind::Down, kTriggerPressed}};
constexpr std::array kPressed{Trigger{input::TriggerKind::Pressed, kTriggerPressed}};

constexpr std::array kDefaultMappings{
    MappingDescription{kActionMove, "PadLeft2D", kStickMove, {}},
    MappingDescription{kActionMove, "KeyW", kKeyForward, {}},
    MappingDescription{kActionMove, "KeyS", kKeyBack, {}},
    MappingDescription{kActionMove, "KeyA", kKeyLeft, {}},
    MappingDescription{kActionMove, "KeyD", {}, {}},
    MappingDescription{kActionLook, "PadRight2D", kStickLook, {}},
    MappingDescription{kActionLook, "Mouse2D", kMouseLook, {}},
    MappingDescription{kActionShoot, "PadRightTrigger", {}, kDown},
    MappingDescription{kActionShoot, "MouseLeft", {}, kDown},
    MappingDescription{kActionDodge, "PadLeftTrigger", {}, kPressed},
    MappingDescription{kActionDodge, "KeySpace", {}, kPressed},
    MappingDescription{kActionCrouch, "PadLeftStick", {}, kPressed},
    MappingDescription{kActionCrouch, "KeyLeftShift", {}, kPressed},
    MappingDescription{kActionView, "PadRightStick", {}, kPressed},
    MappingDescription{kActionView, "KeyE", {}, kPressed},
    MappingDescription{kActionReload, "PadX", {}, kPressed},
    MappingDescription{kActionReload, "KeyR", {}, kPressed},
    MappingDescription{kActionMelee, "PadA", {}, kPressed},
    MappingDescription{kActionMelee, "MouseRight", {}, kPressed},
    MappingDescription{kActionFormNext, "PadB", {}, kPressed},
    MappingDescription{kActionFormNext, "MouseScrollDown", {}, kPressed},
    MappingDescription{kActionFormPrevious, "MouseScrollUp", {}, kPressed},
    MappingDescription{kActionRingOpen, "PadLeftShoulder", {}, kPressed},
    MappingDescription{kActionRingOpen, "PadRightShoulder", {}, kPressed},
    MappingDescription{kActionRingOpen, "MouseMiddle", {}, kPressed},
};

constexpr std::array kRingMappings{
    MappingDescription{kActionRingLevelDown, "PadLeftShoulder", {}, kPressed},
    MappingDescription{kActionRingLevelDown, "MouseScrollDown", {}, kPressed},
    MappingDescription{kActionRingLevelUp, "PadRightShoulder", {}, kPressed},
    MappingDescription{kActionRingLevelUp, "MouseScrollUp", {}, kPressed},
    MappingDescription{kActionRingCast1, "PadA", {}, kPressed},
    MappingDescription{kActionRingCast1, "KeyX", {}, kPressed},
    MappingDescription{kActionRingCast2, "PadB", {}, kPressed},
    MappingDescription{kActionRingCast2, "KeyC", {}, kPressed},
    MappingDescription{kActionRingCast3, "PadX", {}, kPressed},
    MappingDescription{kActionRingCast3, "KeyZ", {}, kPressed},
    MappingDescription{kActionRingClose, "PadY", {}, kPressed},
    MappingDescription{kActionRingClose, "MouseMiddle", {}, kPressed},
};

constexpr ContextDescription kDefaultContext{"default", kDefaultMappings};
constexpr ContextDescription kRingContext{"ring", kRingMappings};

} // namespace

std::span<const input::ActionDescription> actions() noexcept { return kActions; }

const input::ContextDescription& default_context() noexcept { return kDefaultContext; }

const input::ContextDescription& ring_context() noexcept { return kRingContext; }

GameInput load_game_input(input::PlayerInput& input) {
  GameInput loaded{};
  loaded.failure = input.load_actions(actions());
  if (!loaded.ok()) {
    return loaded;
  }
  const input::ContextResult default_result = input.load_context(default_context());
  if (!default_result.ok()) {
    loaded.failure = default_result.failure;
    return loaded;
  }
  const input::ContextResult ring_result = input.load_context(ring_context());
  if (!ring_result.ok()) {
    loaded.failure = ring_result.failure;
    return loaded;
  }
  loaded.default_context = default_result.id;
  loaded.ring_context = ring_result.id;
  const input::InputError applied = input.add_context(default_result.id, kDefaultPriority);
  if (applied != input::InputError::None) {
    loaded.failure = input::InputFailure{applied, std::string(default_context().name)};
  }
  return loaded;
}

} // namespace bw::game
