// Alicia's views (bw/game/characters/alicia/view.hpp).
#include "bw/game/characters/alicia/view.hpp"

#include "bw/camera/evaluator.hpp"
#include "bw/camera/pose.hpp"
#include "bw/camera/rig.hpp"
#include "bw/characters/player_controller.hpp"
#include "bw/game/input/actions.hpp"
#include "bw/input/player_input.hpp"

#include <array>
#include <cstddef>
#include <span>

namespace bw::game::alicia {
namespace {

// The rig's input slot: the look action at the rig's turn rate on both
// axes, the yaw wrapped at a turn and the pitch held to kPitchLimit.
camera::InputSlot look_slot(float turn_rate) noexcept {
  camera::InputSlot slot{};
  slot.action = kActionLook;
  slot.speed_yaw = turn_rate;
  slot.speed_pitch = turn_rate;
  slot.wrap_yaw = 360.0F;
  slot.clamp_pitch = camera::AxisClamp{-kPitchLimit, kPitchLimit, true, true};
  return slot;
}

// Contract 1: attach, the pivot's height, the boom behind her shoulder and
// the lens, its horizontal field the one that gives the vertical field across a
// sensor of kViewAspect to 1.
struct ViewValues {
  // Degrees a second.
  float turn_rate{};
  // Vertical, degrees.
  float vertical_fov{};
};

std::array<camera::Node, 4> view_nodes(ViewValues values) noexcept {
  camera::LensValues lens{};
  lens.field_of_view_degrees = camera::horizontal_fov_degrees(values.vertical_fov, kViewAspect);
  lens.sensor_width = kViewAspect;
  lens.sensor_height = 1.0F;
  lens.near_plane = kNear;
  lens.far_plane = kFar;
  return {camera::attach(false),
          camera::offset(camera::Offset3{0.0F, kPivotHeight, 0.0F}, camera::OffsetSpace::World),
          camera::boom(camera::Offset3{kShoulder, 0.0F, -kBehind}, look_slot(values.turn_rate)),
          camera::lens(lens)};
}

// Either rig is entered from any rig over the view change.
constexpr std::array kEnter{camera::Transition{
    {},
    {},
    camera::Blend{camera::BlendKind::Easing, camera::EasingCurve::CubicOut, kViewChangeSeconds}}};

// View, on Started: the other view. `user` is the PlayerView's own view
// member, which setup names.
void change_view(void* user, const input::ActionState& /*state*/) {
  View& view = *static_cast<View*>(user);
  view = view == View::Standard ? View::Shooting : View::Standard;
}

} // namespace

// The horizontal fields are computed once, at the first call, from the
// named vertical fields.
std::span<const camera::RigDescription> rigs() noexcept {
  static const std::array<camera::Node, 4> standard =
      view_nodes(ViewValues{kTurnRateStandard, kStandardFov});
  static const std::array<camera::Node, 4> shooting =
      view_nodes(ViewValues{kTurnRateShooting, kShootingFov});
  static const std::array<camera::RigDescription, 2> table{
      camera::RigDescription{kRigStandard, standard, kEnter, {}},
      camera::RigDescription{kRigShooting, shooting, kEnter, {}}};
  return table;
}

ViewSetupFailure PlayerView::setup(input::PlayerInput& input,
                                   characters::PlayerController& controller,
                                   camera::Evaluator& evaluator) {
  ViewSetupFailure failure{};
  failure.camera = evaluator.load_rigs(rigs(), input);
  if (!failure.camera.ok()) {
    failure.error = ViewSetupError::RigsFailed;
    return failure;
  }
  standard_ = evaluator.find_rig(kRigStandard);
  shooting_ = evaluator.find_rig(kRigShooting);
  evaluator.set_director(this);
  evaluator.set_control_rotation_sink(&controller);
  failure.input =
      input.bind(input.find_action(kActionView), input::ActionEvent::Started, &change_view, &view_);
  if (failure.input != input::InputError::None) {
    failure.error = ViewSetupError::BindFailed;
  }
  return failure;
}

std::size_t PlayerView::run(double /*step*/, std::span<camera::RigRequest> out) {
  if (out.empty()) {
    return 0;
  }
  out[0] = camera::RigRequest{view_ == View::Standard ? standard_ : shooting_, camera::Layer::Main,
                              nullptr};
  return 1;
}

} // namespace bw::game::alicia
