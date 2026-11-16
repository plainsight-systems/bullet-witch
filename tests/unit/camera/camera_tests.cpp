// Unit test: rigs of the real nodes on the evaluator's main stack
// (bw/camera/rig.hpp, evaluator.hpp), and the load errors the documents
// promise.
#include "bw/camera/evaluator.hpp"
#include "bw/camera/pose.hpp"
#include "bw/camera/rig.hpp"
#include "bw/input/key_state.hpp"
#include "bw/input/player_input.hpp"
#include "bw/math/vector/vector3.hpp"
#include "bw/platform/input/device_input.hpp"
#include "bw/testing/harness.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <memory>
#include <span>
#include <string>
#include <string_view>

namespace {

using bw::camera::Blend;
using bw::camera::BlendKind;
using bw::camera::EasingCurve;
using bw::camera::Transition;
using bw::testing::require;

// Requests one rig on the main layer, optionally with its own transition.
class OneRequest final : public bw::camera::Director {
public:
  bw::camera::RigRequest request{};
  std::size_t run(double /*step*/, std::span<bw::camera::RigRequest> out) override {
    out[0] = request;
    return 1;
  }
};

constexpr Blend easing(float seconds) noexcept {
  return Blend{BlendKind::Easing, EasingCurve::CubicOut, seconds};
}

bw::camera::InputSlot slot(float speed) noexcept {
  bw::camera::InputSlot result{};
  result.action = "Look";
  result.speed_yaw = speed;
  result.speed_pitch = speed;
  return result;
}

constexpr bw::camera::LensValues kLensA{60.0F, 2.0F, 1.0F, 0.1F, 100.0F};
constexpr bw::camera::LensValues kLensB{30.0F, 2.0F, 1.0F, 0.1F, 100.0F};
constexpr std::array kAEnter{Transition{{}, {}, easing(2.0F)}};
constexpr std::array kAExit{Transition{{}, "a", easing(5.0F)}};
constexpr std::array kBEnter{Transition{"b", {}, easing(9.0F)}, Transition{{}, {}, easing(1.0F)}};
constexpr std::array kBExit{Transition{{}, "a", easing(0.5F)}};

void near(float observed, float expected, const std::string& what) {
  require(std::abs(observed - expected) <= 1.0e-4F,
          what + ": " + std::to_string(observed) + ", want " + std::to_string(expected));
}

// (rig.hpp, contracts 1-3; evaluator.hpp, contracts 3-7): the first rig
// pops in with the cut on its first evaluation only, its nodes giving the
// pose; a second rig takes the first matching of the top rig's exits and
// its own enters, its slot carried, and its weight by the step, the slots
// turning at the speeds mixed by that weight; pushing the rig a blend is
// leaving adds a third entry with the leaving rig's exit; a request's own
// transition wins, and an entry at weight 1 removes those below it.
void stack_table() {
  const std::array a_nodes{bw::camera::attach(true),
                           bw::camera::offset({0.0F, 1.0F, 0.0F}, bw::camera::OffsetSpace::World),
                           bw::camera::boom({1.0F, 0.0F, -2.0F}, slot(10.0F)),
                           bw::camera::lens(kLensA)};
  const std::array b_nodes{bw::camera::attach(true),
                           bw::camera::offset({0.0F, 0.0F, 1.0F}, bw::camera::OffsetSpace::Pose),
                           bw::camera::boom({0.0F, 0.0F, -4.0F}, slot(20.0F)),
                           bw::camera::lens(kLensB)};
  const std::array rigs{bw::camera::RigDescription{"a", a_nodes, kAEnter, kAExit},
                        bw::camera::RigDescription{"b", b_nodes, kBEnter, kBExit}};
  constexpr std::array kLook{bw::input::ActionDescription{"Look", bw::input::ValueType::Axis2D}};
  constexpr std::array kMappings{bw::input::MappingDescription{"Look", "PadRight2D", {}, {}}};
  const auto input = bw::input::PlayerInput::create({});
  require(input->load_actions(kLook).ok(), "the action loads");
  const auto context = input->load_context({"look", kMappings});
  require(context.ok() && input->add_context(context.id, 0) == bw::input::InputError::None,
          "the context applies");
  const auto evaluator = bw::camera::Evaluator::create({});
  require(evaluator->load_rigs(rigs, *input).ok(), "the rigs load");
  OneRequest director;
  evaluator->set_director(&director);
  const bw::camera::RigId a = evaluator->find_rig("a");
  const bw::camera::RigId b = evaluator->find_rig("b");
  constexpr Transition kOwn{{}, {}, easing(0.25F)};

  // weight of an easing at elapsed / time, CubicOut.
  const auto cubic = [](float fraction) { return 1.0F - std::pow(1.0F - fraction, 3.0F); };
  const float carried = (10.0F + cubic(0.25F) * 10.0F) * 0.25F;
  struct Row {
    bw::camera::RigId rig{};
    const Transition* own{};
    float stick{};
    bool cut{};
    std::array<bw::camera::StackEntry, 3> stack{};
    std::size_t count{};
    float yaw{};
  };
  const std::array rows{
      Row{a, nullptr, 0.0F, true, {{{a, 1.0F}}}, 1, 0.0F},
      Row{a, nullptr, 0.0F, false, {{{a, 1.0F}}}, 1, 0.0F},
      Row{b, nullptr, 1.0F, false, {{{a, 1.0F}, {b, cubic(0.25F)}}}, 2, carried},
      Row{a, nullptr, 0.0F, false, {{{a, 1.0F}, {b, cubic(0.5F)}, {a, cubic(0.5F)}}}, 3, carried},
      Row{b, &kOwn, 0.0F, false, {{{b, 1.0F}}}, 1, carried},
      Row{b, nullptr, 0.0F, false, {{{b, 1.0F}}}, 1, carried},
  };
  bw::input::KeyState keys;
  for (std::size_t row = 0; row < rows.size(); ++row) {
    keys.accumulate({bw::platform::InputElement::PadRightX, rows[row].stick});
    keys.begin_frame();
    input->evaluate(keys);
    director.request =
        bw::camera::RigRequest{rows[row].rig, bw::camera::Layer::Main, rows[row].own};
    const bw::camera::Evaluation& evaluation = evaluator->evaluate(0.25);
    require(evaluation.valid && evaluation.cut == rows[row].cut, row, "valid, and the cut flag");
    const std::span<const bw::camera::StackEntry> stack = evaluator->stack();
    require(stack.size() == rows[row].count, row, "the stack's size");
    for (std::size_t entry = 0; entry < stack.size(); ++entry) {
      require(stack[entry].rig == rows[row].stack[entry].rig, row, "the entry's rig");
      near(stack[entry].weight, rows[row].stack[entry].weight,
           "row " + std::to_string(row) + " weight");
    }
    near(evaluation.pose.yaw_degrees, rows[row].yaw, "row " + std::to_string(row) + " yaw");
  }
  // The last pose is rig b's alone: attach, the pose-space offset forward,
  // the boom at the carried yaw, the lens.
  const bw::camera::Pose& pose = evaluator->evaluation().pose;
  const bw::math::Vector3f b_location =
      bw::camera::forward(0.0F, 0.0F) * 1.0F + bw::camera::forward(carried, 0.0F) * -4.0F;
  near(pose.location.x, b_location.x, "b x");
  near(pose.location.z, b_location.z, "b z");
  near(pose.field_of_view_degrees, kLensB.field_of_view_degrees, "b's lens");
  // The first rig's pose: attach, the world offset up, the boom right and
  // behind at yaw 0, its lens.
  const auto first = bw::camera::Evaluator::create({});
  require(first->load_rigs(rigs, *input).ok(), "the rigs load again");
  director.request = bw::camera::RigRequest{first->find_rig("a"), bw::camera::Layer::Main, nullptr};
  first->set_director(&director);
  keys.accumulate({bw::platform::InputElement::PadRightX, 0.0F});
  keys.begin_frame();
  input->evaluate(keys);
  const bw::camera::Pose& a_pose = first->evaluate(0.25).pose;
  const bw::math::Vector3f a_location = bw::math::Vector3f{0.0F, 1.0F, 0.0F} +
                                        bw::camera::right(0.0F) * 1.0F +
                                        bw::camera::forward(0.0F, 0.0F) * -2.0F;
  near(a_pose.location.x, a_location.x, "a x");
  near(a_pose.location.y, a_location.y, "a y");
  near(a_pose.location.z, a_location.z, "a z");
  near(a_pose.field_of_view_degrees, kLensA.field_of_view_degrees, "a's lens");
  near(a_pose.near_plane, kLensA.near_plane, "a's near");
}

// (evaluator.hpp, contract 1; camera.md, invariant 16): a transition
// naming a rig, or a boom naming an action, that does not exist fails by
// that name.
void load_errors() {
  const std::array nodes{bw::camera::boom({}, slot(1.0F))};
  bw::camera::InputSlot missing = slot(1.0F);
  missing.action = "Missing";
  const std::array missing_action{bw::camera::boom({}, missing)};
  constexpr std::array kToNowhere{Transition{{}, "nowhere", easing(1.0F)}};
  constexpr std::array kLook{bw::input::ActionDescription{"Look", bw::input::ValueType::Axis2D}};
  const auto input = bw::input::PlayerInput::create({});
  require(input->load_actions(kLook).ok(), "the action loads");
  struct Row {
    bw::camera::RigDescription rig;
    bw::camera::CameraError error;
    std::string_view name;
  };
  const std::array rows{
      Row{{"one", nodes, kToNowhere, {}}, bw::camera::CameraError::UnknownRig, "nowhere"},
      Row{{"two", missing_action, {}, {}}, bw::camera::CameraError::UnknownAction, "Missing"},
  };
  for (std::size_t row = 0; row < rows.size(); ++row) {
    const auto evaluator = bw::camera::Evaluator::create({});
    const bw::camera::CameraFailure failure =
        evaluator->load_rigs(std::span(&rows[row].rig, 1), *input);
    require(failure.error == rows[row].error && failure.name == rows[row].name, row,
            "fails by name: " + failure.name);
  }
}

using Scenario = bw::testing::Scenario<void>;
constexpr std::array kScenarios{
    Scenario{"stack_table", &stack_table},
    Scenario{"load_errors", &load_errors},
};

} // namespace

int main(int argc, char** argv) { return bw::testing::run_scenario(argc, argv, kScenarios); }
