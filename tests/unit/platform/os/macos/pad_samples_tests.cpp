// Unit test: the difference of two extended gamepad readings as samples
// (bw/platform/os/macos/pad.hpp, contract 3), one table-driven scenario.
#include "bw/platform/os/macos/pad.hpp"

#include "bw/platform/input/device_input.hpp"
#include "bw/platform/input/pad.hpp"
#include "bw/testing/harness.hpp"

#include <array>
#include <cstddef>

namespace {

using bw::platform::InputElement;
using bw::platform::InputSample;
using bw::platform::kPadElementCount;
using bw::platform::macos::ExtendedGamepadValues;
using bw::testing::require;
using Values = ExtendedGamepadValues;

struct Row {
  float Values::* axis{};
  bool Values::* button{};
  InputSample expected;
};

// (macos/pad.hpp, contract 3), rows in the header's table order: each field
// changed alone gives its element and value, an axis as it is and a button
// as 1; an unchanged pair gives nothing; every field changed gives one
// sample each in the table's order, here all going to 0 as a disconnect's
// poll reads.
void pad_samples() {
  constexpr std::array kRows{
      Row{nullptr, &Values::a, {InputElement::PadA, 1.0F}},
      Row{nullptr, &Values::x, {InputElement::PadX, 1.0F}},
      Row{nullptr, &Values::b, {InputElement::PadB, 1.0F}},
      Row{nullptr, &Values::y, {InputElement::PadY, 1.0F}},
      Row{nullptr, &Values::left_shoulder, {InputElement::PadLeftShoulder, 1.0F}},
      Row{nullptr, &Values::right_shoulder, {InputElement::PadRightShoulder, 1.0F}},
      Row{&Values::left_x, nullptr, {InputElement::PadLeftX, -0.5F}},
      Row{&Values::left_y, nullptr, {InputElement::PadLeftY, 0.25F}},
      Row{&Values::right_x, nullptr, {InputElement::PadRightX, 1.0F}},
      Row{&Values::right_y, nullptr, {InputElement::PadRightY, -1.0F}},
      Row{&Values::right_trigger, nullptr, {InputElement::PadRightTrigger, 0.125F}},
      Row{&Values::left_trigger, nullptr, {InputElement::PadLeftTrigger, 0.75F}},
      Row{nullptr, &Values::left_stick, {InputElement::PadLeftStick, 1.0F}},
      Row{nullptr, &Values::right_stick, {InputElement::PadRightStick, 1.0F}},
  };
  static_assert(kRows.size() == kPadElementCount);
  std::array<InputSample, kPadElementCount> out{};
  Values all{};
  for (std::size_t row = 0; row < kRows.size(); ++row) {
    Values changed{};
    for (Values* values : {&changed, &all}) {
      if (kRows[row].axis != nullptr) {
        values->*kRows[row].axis = kRows[row].expected.value;
      } else {
        values->*kRows[row].button = true;
      }
    }
    require(bw::platform::macos::pad_samples(Values{}, changed, out) == 1U &&
                out[0] == kRows[row].expected,
            row, "one changed field gives its element and value");
    require(bw::platform::macos::pad_samples(changed, changed, out) == 0U, row,
            "an unchanged pair gives nothing");
  }
  require(bw::platform::macos::pad_samples(all, Values{}, out) == kPadElementCount,
          "every field changed gives one sample each");
  for (std::size_t row = 0; row < kRows.size(); ++row) {
    require(out[row] == InputSample{kRows[row].expected.element, 0.0F}, row,
            "the samples follow the table's order, each at 0");
  }
}

using Scenario = bw::testing::Scenario<void>;

constexpr std::array kScenarios{
    Scenario{"pad_samples", &pad_samples},
};

} // namespace

int main(int argc, char** argv) { return bw::testing::run_scenario(argc, argv, kScenarios); }
