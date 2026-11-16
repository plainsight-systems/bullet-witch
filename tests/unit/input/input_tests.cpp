// Unit test: device samples through key state, modifiers, triggers and
// contexts to action states (bw/input/key_state.hpp, player_input.hpp), and
// the load errors the documents promise.
#include "bw/input/key_state.hpp"
#include "bw/input/modifiers.hpp"
#include "bw/input/player_input.hpp"
#include "bw/input/triggers.hpp"
#include "bw/platform/input/device_input.hpp"
#include "bw/testing/harness.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <string>
#include <string_view>

namespace {

using bw::input::ActionDescription;
using bw::input::ActionValue;
using bw::input::MappingDescription;
using bw::input::Trigger;
using bw::input::ValueType;
using bw::platform::InputElement;
using bw::platform::InputSample;
using bw::testing::require;

constexpr std::uint8_t kStartedFired = 3;
constexpr std::uint8_t kFired = 2;
constexpr std::uint8_t kCompleted = 4;

constexpr std::array kActions{
    ActionDescription{"Look", ValueType::Axis2D},
    ActionDescription{"Move", ValueType::Axis2D},
    ActionDescription{"Jump"},
    ActionDescription{"Use"},
    ActionDescription{"Next"},
    ActionDescription{"Aim", ValueType::Axis2D},
    ActionDescription{"High"},
    ActionDescription{"Low"},
    ActionDescription{"Fire"},
};

constexpr std::array kStick{
    bw::input::dead_zone(bw::input::DeadZoneKind::Axial, bw::input::DeadZoneRange{0.24F, 1.0F}),
    bw::input::scale(ActionValue{1.7F, 1.7F, 1.0F})};
constexpr std::array kSwap{bw::input::swap_xy()};
constexpr std::array kPressed{Trigger{bw::input::TriggerKind::Pressed, 0.5F}};

constexpr std::array kLowMappings{
    MappingDescription{"Look", "PadRight2D", kStick, {}},
    MappingDescription{"Move", "KeyW", kSwap, {}},
    MappingDescription{"Move", "KeyD", {}, {}},
    MappingDescription{"Jump", "KeySpace", {}, kPressed},
    MappingDescription{"Use", "KeyE", {}, kPressed},
    MappingDescription{"Next", "MouseScrollUp", {}, kPressed},
    MappingDescription{"Aim", "Mouse2D", {}, {}},
    MappingDescription{"Low", "KeyR", {}, kPressed},
    MappingDescription{"Fire", "PadRightTrigger", {}, kPressed},
};
constexpr std::array kHighMappings{MappingDescription{"High", "KeyR", {}, kPressed}};

// Appends the letter `user` names to the calls of row 8's frame.
std::string calls;
void call_a(void* /*user*/, const bw::input::ActionState& /*state*/) { calls += 'A'; }
void call_b(void* /*user*/, const bw::input::ActionState& /*state*/) { calls += 'B'; }
void call_c(void* /*user*/, const bw::input::ActionState& /*state*/) { calls += 'C'; }

struct Expected {
  std::string_view action;
  std::uint8_t events;
  ActionValue value;
};

// (key_state.hpp, invariants 1-5; player_input.hpp, contract 4): frame 0
// takes a stick past its dead zone, scaled; two keys into one 2D value; a
// press held; a press and release in one frame; one wheel line; mouse
// counts; a key both contexts map. Frame 1 has no samples: the stick keeps
// its value, the held keys stay, a pressed trigger fires once, the
// in-frame press reads 0, the wheel's key and the mouse read 0, and the
// lower context never sees the key the higher one consumes. Frame 2 presses
// the key of the in-frame press again: its mapping ran on 0 in frame 1, so
// the press fires. A trigger axis held at 0.3 under a threshold of 0.5 joins
// the value and never fires (triggers.hpp, contract 1). Frame 0's bound
// functions are one queue: the Started bindings first, the last bound first,
// then the Fired binding bound before them.
void samples_to_actions() {
  const auto input = bw::input::PlayerInput::create({});
  require(input->load_actions(kActions).ok(), "the actions load");
  const auto low = input->load_context({"low", kLowMappings});
  const auto high = input->load_context({"high", kHighMappings});
  require(low.ok() && high.ok(), "the contexts load");
  require(input->add_context(low.id, 0) == bw::input::InputError::None &&
              input->add_context(high.id, 1) == bw::input::InputError::None,
          "the contexts apply");
  using bw::input::ActionEvent;
  require(input->bind(input->find_action("Look"), ActionEvent::Fired, &call_c, nullptr) ==
                  bw::input::InputError::None &&
              input->bind(input->find_action("Jump"), ActionEvent::Started, &call_a, nullptr) ==
                  bw::input::InputError::None &&
              input->bind(input->find_action("Use"), ActionEvent::Started, &call_b, nullptr) ==
                  bw::input::InputError::None,
          "the bindings are made");
  bw::input::KeyState keys;
  const float look = (0.62F - 0.24F) / (1.0F - 0.24F) * 1.7F;
  struct Frame {
    std::initializer_list<InputSample> samples;
    std::array<Expected, 9> expected;
  };
  const std::array frames{
      Frame{{{InputElement::PadRightX, 0.62F},
             {InputElement::KeyW, 1.0F},
             {InputElement::KeyD, 1.0F},
             {InputElement::KeySpace, 1.0F},
             {InputElement::KeyE, 1.0F},
             {InputElement::KeyE, 0.0F},
             {InputElement::MouseWheel, 1.0F},
             {InputElement::MouseMotionX, 10.0F},
             {InputElement::KeyR, 1.0F},
             {InputElement::PadRightTrigger, 0.3F}},
            {Expected{"Look", kStartedFired, {look, 0.0F, 0.0F}},
             Expected{"Move", kStartedFired, {1.0F, 1.0F, 0.0F}},
             Expected{"Jump", kStartedFired, {1.0F, 0.0F, 0.0F}},
             Expected{"Use", kStartedFired, {1.0F, 0.0F, 0.0F}},
             Expected{"Next", kStartedFired, {1.0F, 0.0F, 0.0F}},
             Expected{"Aim", kStartedFired, {10.0F, 0.0F, 0.0F}},
             Expected{"High", kStartedFired, {1.0F, 0.0F, 0.0F}}, Expected{"Low", 0, {}},
             Expected{"Fire", 0, {0.3F, 0.0F, 0.0F}}}},
      Frame{{},
            {Expected{"Look", kFired, {look, 0.0F, 0.0F}},
             Expected{"Move", kFired, {1.0F, 1.0F, 0.0F}},
             Expected{"Jump", kCompleted, {1.0F, 0.0F, 0.0F}}, Expected{"Use", kCompleted, {}},
             Expected{"Next", kCompleted, {}}, Expected{"Aim", kCompleted, {}},
             Expected{"High", kCompleted, {1.0F, 0.0F, 0.0F}}, Expected{"Low", 0, {}},
             Expected{"Fire", 0, {0.3F, 0.0F, 0.0F}}}},
      Frame{{{InputElement::KeyE, 1.0F}},
            {Expected{"Look", kFired, {look, 0.0F, 0.0F}},
             Expected{"Move", kFired, {1.0F, 1.0F, 0.0F}}, Expected{"Jump", 0, {1.0F, 0.0F, 0.0F}},
             Expected{"Use", kStartedFired, {1.0F, 0.0F, 0.0F}}, Expected{"Next", 0, {}},
             Expected{"Aim", 0, {}}, Expected{"High", 0, {1.0F, 0.0F, 0.0F}},
             Expected{"Low", 0, {}}, Expected{"Fire", 0, {0.3F, 0.0F, 0.0F}}}},
  };
  for (std::size_t frame = 0; frame < frames.size(); ++frame) {
    for (const InputSample& sample : frames[frame].samples) {
      keys.accumulate(sample);
    }
    keys.begin_frame();
    input->evaluate(keys);
    for (const Expected& expected : frames[frame].expected) {
      const bw::input::ActionState& state = input->state(input->find_action(expected.action));
      const bool value_matches = std::abs(state.value.x - expected.value.x) < 1.0e-5F &&
                                 std::abs(state.value.y - expected.value.y) < 1.0e-5F &&
                                 state.value.z == expected.value.z;
      require(state.events == expected.events && value_matches, frame,
              std::string(expected.action) + ": events " + std::to_string(state.events) +
                  " value " + std::to_string(state.value.x) + "," + std::to_string(state.value.y));
    }
    if (frame == 0) {
      require(calls == "BAC", "the bound functions' queue: " + calls);
    }
  }
}

// (player_input.hpp, contract 2; input.md, invariant 12): a mapping naming
// an action or a key that does not exist fails by that name.
void load_errors() {
  constexpr std::array kUnknownAction{MappingDescription{"Missing", "KeyW", {}, {}}};
  constexpr std::array kUnknownKey{MappingDescription{"Jump", "KeyQ", {}, {}}};
  struct Row {
    bw::input::ContextDescription context;
    bw::input::InputError error;
    std::string_view name;
  };
  const std::array rows{
      Row{{"one", kUnknownAction}, bw::input::InputError::UnknownAction, "Missing"},
      Row{{"two", kUnknownKey}, bw::input::InputError::UnknownKey, "KeyQ"},
  };
  for (std::size_t row = 0; row < rows.size(); ++row) {
    const auto input = bw::input::PlayerInput::create({});
    require(input->load_actions(kActions).ok(), row, "the actions load");
    const bw::input::ContextResult result = input->load_context(rows[row].context);
    require(result.failure.error == rows[row].error && result.failure.name == rows[row].name, row,
            "fails by name: " + result.failure.name);
  }
}

using Scenario = bw::testing::Scenario<void>;
constexpr std::array kScenarios{
    Scenario{"samples_to_actions", &samples_to_actions},
    Scenario{"load_errors", &load_errors},
};

} // namespace

int main(int argc, char** argv) { return bw::testing::run_scenario(argc, argv, kScenarios); }
