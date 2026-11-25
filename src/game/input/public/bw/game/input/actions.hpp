#ifndef BW_GAME_INPUT_ACTIONS_HPP
#define BW_GAME_INPUT_ACTIONS_HPP

#include "bw/input/player_input.hpp"

#include <cstdint>
#include <span>
#include <string_view>

// Responsibility: the game's actions and their default mappings for the
// pad and for keyboard and mouse (docs/architecture/input.md, "The game's
// actions and default mappings | src/game"). They are tables handed to
// the player's input (bw/input/player_input.hpp); every number in them is
// one the shipped game runs on, named below.
//
// Contract.
//   1. actions() is the game's actions, each with the name constant below
//      and this type: Move and Look are Axis2D, every other a Switch.
//      Move's own modifier is a radial dead zone from kMoveDeadZone to 1.
//      Look's value is the turn input a camera rig's slot multiplies by
//      its speed and the step (bw/camera/rig.hpp, contract 2): x the yaw,
//      to the right positive; y the pitch, up positive.
//   2. The contexts, and the mappings each holds, pad first, then
//      keyboard and mouse:
//      default_context()        priority kDefaultPriority
//        Move          PadLeft2D; KeyW, KeyS, KeyA, KeyD
//        Look          PadRight2D; Mouse2D
//        Shoot         PadRightTrigger; MouseLeft            Down
//        Dodge         PadLeftTrigger; KeySpace              Pressed
//        Crouch        PadLeftStick; KeyLeftShift            Pressed
//        View          PadRightStick; KeyE                   Pressed
//        Reload        PadX; KeyR                            Pressed
//        Melee         PadA; MouseRight                      Pressed
//        FormNext      PadB; MouseScrollDown                 Pressed
//        FormPrevious  MouseScrollUp                         Pressed
//        RingOpen      PadLeftShoulder, PadRightShoulder; MouseMiddle
//                                                            Pressed
//      ring_context()           priority kRingPriority
//        RingLevelDown PadLeftShoulder; MouseScrollDown      Pressed
//        RingLevelUp   PadRightShoulder; MouseScrollUp       Pressed
//        RingCast1     PadA; KeyX                            Pressed
//        RingCast2     PadB; KeyC                            Pressed
//        RingCast3     PadX; KeyZ                            Pressed
//        RingClose     PadY; MouseMiddle                     Pressed
//      Every trigger's threshold is kTriggerPressed.
//   3. A stick mapping runs an axial dead zone from kStickDeadZone to 1 on
//      each axis. The pad's Look then scales by kLookSensitivity, so a
//      stick held fully to one side gives 1.7. The mouse's Look divides
//      its counts by kMouseCountsDivisor and flips y. The keys' Move
//      mappings give (0, 1), (0, -1), (-1, 0) and (1, 0).
//   4. load_game_input(input) loads actions() and the two contexts and
//      applies default_context() at kDefaultPriority. It returns the ids,
//      or the first failure of a load, which names what failed. Nothing
//      here applies ring_context().
//
// The values, each read from the shipped game: the stick dead zone is 7864 of 32767 per axis; the
// move vector's is 0.15; look sensitivity is 1.7 at the shipped setting; the mouse's counts are
// divided by 41 at the shipped setting; a trigger is pressed above half its travel. Look
// sensitivity is a scale modifier on the mapping.
namespace bw::game {

inline constexpr std::string_view kActionMove = "Move";
inline constexpr std::string_view kActionLook = "Look";
inline constexpr std::string_view kActionShoot = "Shoot";
inline constexpr std::string_view kActionDodge = "Dodge";
inline constexpr std::string_view kActionCrouch = "Crouch";
inline constexpr std::string_view kActionView = "View";
inline constexpr std::string_view kActionReload = "Reload";
inline constexpr std::string_view kActionMelee = "Melee";
inline constexpr std::string_view kActionFormNext = "FormNext";
inline constexpr std::string_view kActionFormPrevious = "FormPrevious";
inline constexpr std::string_view kActionRingOpen = "RingOpen";
inline constexpr std::string_view kActionRingLevelDown = "RingLevelDown";
inline constexpr std::string_view kActionRingLevelUp = "RingLevelUp";
inline constexpr std::string_view kActionRingCast1 = "RingCast1";
inline constexpr std::string_view kActionRingCast2 = "RingCast2";
inline constexpr std::string_view kActionRingCast3 = "RingCast3";
inline constexpr std::string_view kActionRingClose = "RingClose";

inline constexpr float kStickDeadZone = 7864.0F / 32767.0F;
inline constexpr float kMoveDeadZone = 0.15F;
inline constexpr float kLookSensitivity = 1.7F;
inline constexpr float kMouseCountsDivisor = 41.0F;
inline constexpr float kTriggerPressed = 0.5F;

inline constexpr std::int32_t kDefaultPriority = 0;
inline constexpr std::int32_t kRingPriority = 1;

[[nodiscard]] std::span<const input::ActionDescription> actions() noexcept;
[[nodiscard]] const input::ContextDescription& default_context() noexcept;
[[nodiscard]] const input::ContextDescription& ring_context() noexcept;

struct GameInput {
  input::ContextId default_context{};
  input::ContextId ring_context{};
  input::InputFailure failure{};
  [[nodiscard]] bool ok() const noexcept { return failure.ok(); }
};

[[nodiscard]] GameInput load_game_input(input::PlayerInput& input);

} // namespace bw::game
#endif
