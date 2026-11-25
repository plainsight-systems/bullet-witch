#ifndef BW_GAME_CHARACTERS_ALICIA_VIEW_HPP
#define BW_GAME_CHARACTERS_ALICIA_VIEW_HPP

#include "bw/camera/evaluator.hpp"
#include "bw/camera/rig.hpp"
#include "bw/characters/player_controller.hpp"
#include "bw/game/input/actions.hpp"
#include "bw/input/player_input.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

// Responsibility: Alicia's views (docs/architecture/camera.md, "Rigs,
// directors and their values | src/game"; characters.md, "A kind of
// character's values and controller | src/game/characters"): her standard
// and shooting-view rigs, the transition between them, the director that
// picks one, and what the view action does. Every number is one the
// shipped game runs on, named below.
//
// Contract.
//   1. rigs() is her two rigs, kRigStandard and kRigShooting. Each is, in
//      order: Attach to her place; Offset by (0, kPivotHeight, 0) in World
//      space; Boom with the offset (kShoulder, 0, -kBehind) and an input
//      slot on the Look action, its speed the rig's turn rate on both
//      axes, its yaw wrapped at 360 and its pitch held between minus and
//      plus kPitchLimit; Lens with the rig's field of view, a sensor of
//      kViewAspect to 1, kNear and kFar. The standard rig's turn rate is
//      kTurnRateStandard and its vertical field of view kStandardFov; the
//      shooting rig's are kTurnRateShooting and kShootingFov. A rig's
//      field of view is the horizontal one that gives that vertical field
//      across the sensor (bw/camera/pose.hpp, contract 2). Each rig's
//      enter transitions hold one that matches any change, with an Easing
//      blend, CubicOut, over kViewChangeSeconds.
//      Her place, the object the rigs attach to, is kBodyBelowPlace above
//      her feet: whoever places her puts her object that far above the
//      ground and draws her mesh that far below her object.
//   2. PlayerView is her director and her view's state. It starts in the
//      Standard view and each tick requests the rig of its view on the
//      main layer.
//   3. setup(input, controller, evaluator) loads rigs() into the evaluator
//      against `input`, names this director to it, names the controller
//      to the evaluator as its control rotation sink, and binds one
//      function: View, on Started, changes the view to the other one. It
//      returns the first failure of a load or the bind, by name. Called once; the three
//      arguments are observed and outlive this object, which is not moved
//      after setup.
//   4. So in the Standard view the right stick held fully to one side
//      turns the view kTurnRateStandard times kLookSensitivity degrees a
//      second, in the Shooting view kTurnRateShooting times
//      kLookSensitivity; a mouse count in a tick turns it by the view's turn rate times the
//      step over kMouseCountsDivisor degrees; and during a view
//      change the rate goes from the one view's to the other's with the
//      blend.
//
// The values, each read from the shipped game: her body is drawn 0.4 below her position; the view's
// set-up table gives the pivot's height 0.7, the shoulder's shift 0.5 and the distance behind 3 for
// both views, and the vertical fields of view 44 and 20, over a frame of 16 to 9; the turn rates
// are 100 degrees a second in the standard view and 45 in the shooting view; the pitch is held to
// 70 degrees either way; a view change takes 0.25 s; near and far are 0.01 and 1000. The game eases
// a view change with the curve 4t / (1 + 3t); CubicOut follows it to within 0.08 of the weight.
namespace bw::game::alicia {

inline constexpr std::string_view kRigStandard = "alicia.standard";
inline constexpr std::string_view kRigShooting = "alicia.shooting";

inline constexpr float kBodyBelowPlace = 0.4F;
inline constexpr float kPivotHeight = 0.7F;
inline constexpr float kShoulder = 0.5F;
inline constexpr float kBehind = 3.0F;
// Vertical, degrees.
inline constexpr float kStandardFov = 44.0F;
inline constexpr float kShootingFov = 20.0F;
inline constexpr float kViewAspect = 16.0F / 9.0F;
inline constexpr float kNear = 0.01F;
inline constexpr float kFar = 1000.0F;
// Degrees a second.
inline constexpr float kTurnRateStandard = 100.0F;
inline constexpr float kTurnRateShooting = 45.0F;
inline constexpr float kPitchLimit = 70.0F;
inline constexpr float kViewChangeSeconds = 0.25F;

[[nodiscard]] std::span<const camera::RigDescription> rigs() noexcept;

enum class View : std::uint8_t { Standard, Shooting };

enum class ViewSetupError : std::uint8_t { None, RigsFailed, BindFailed };

struct ViewSetupFailure {
  ViewSetupError error{ViewSetupError::None};
  // RigsFailed.
  camera::CameraFailure camera{};
  // BindFailed.
  input::InputError input{input::InputError::None};
  [[nodiscard]] bool ok() const noexcept { return error == ViewSetupError::None; }
};

class PlayerView final : public camera::Director {
public:
  PlayerView() = default;
  PlayerView(const PlayerView&) = delete;
  PlayerView& operator=(const PlayerView&) = delete;

  [[nodiscard]] ViewSetupFailure setup(input::PlayerInput& input,
                                       characters::PlayerController& controller,
                                       camera::Evaluator& evaluator);

  [[nodiscard]] View view() const noexcept { return view_; }

  [[nodiscard]] std::size_t run(double step, std::span<camera::RigRequest> out) override;

private:
  camera::RigId standard_{};
  camera::RigId shooting_{};
  View view_{View::Standard};
};

} // namespace bw::game::alicia
#endif
