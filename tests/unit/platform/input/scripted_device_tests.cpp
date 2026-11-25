// Unit test: the scripted device's parse (bw/platform/input/scripted_device.hpp,
// contracts 1-3), one table-driven scenario.
#include "bw/platform/input/scripted_device.hpp"

#include "bw/platform/input/device_input.hpp"
#include "bw/testing/harness.hpp"

#include <array>
#include <cstddef>
#include <span>
#include <string_view>

namespace {

using bw::platform::InputElement;
using bw::platform::InputSample;
using bw::platform::ScriptCommandKind;
using bw::platform::ScriptError;

using bw::testing::require;

struct ErrorRow {
  std::string_view text;
  ScriptError error;
  std::size_t line;
  // The offending word, for the rules whose word the header names; empty
  // and not asserted for the others.
  std::string_view word;
  bool word_named;
};

// (scripted_device.hpp, contracts 1-3): the grammar's skipped lines, '\r'
// and tabs; commands in line order; a tick's commands as one span, an empty
// one for a tick with none; last_tick; and one row per ScriptError name,
// each breaking one rule, with its line, and the word where the rule names
// one.
void parse_table() {
  constexpr std::string_view kScript = "# turn right, then mark\n"
                                       "\n"
                                       "0 set PadRightX 1\r\n"
                                       "0\tmark   start\n"
                                       "  # indented comment\n"
                                       "30 set PadRightX -0.25\n"
                                       "30 set MouseMotionX 246\n"
                                       "31 end";
  const bw::platform::InputScript script = bw::platform::parse_script(kScript);
  require(script.ok() && script.commands.size() == 5U, "a valid script parses every command");
  const std::span<const bw::platform::ScriptCommand> first = commands_at(script, 0);
  require(first.size() == 2U && first[0].kind == ScriptCommandKind::Set &&
              first[0].sample == InputSample{InputElement::PadRightX, 1.0F},
          "tick 0 starts with its set, '\\r' dropped");
  require(first[1].kind == ScriptCommandKind::Mark && first[1].name == "start",
          "the mark follows in line order, split on tabs and runs of spaces");
  const std::span<const bw::platform::ScriptCommand> later = commands_at(script, 30);
  require(later.size() == 2U && later[0].sample == InputSample{InputElement::PadRightX, -0.25F} &&
              later[1].sample == InputSample{InputElement::MouseMotionX, 246.0F},
          "a tick's commands are contiguous, in line order, with signed values");
  require(commands_at(script, 12).empty(), "a tick with no command gives an empty span");
  require(commands_at(script, 31).size() == 1U &&
              commands_at(script, 31)[0].kind == ScriptCommandKind::End,
          "the end is the last command");
  require(last_tick(script) == 31U, "last_tick is the end's tick");

  constexpr std::array kErrors{
      ErrorRow{"0 press KeyW 1\n0 end\n", ScriptError::UnknownCommand, 1, "press", true},
      ErrorRow{"0 set KeyW\n0 end\n", ScriptError::ArgumentCount, 1, {}, false},
      ErrorRow{"0 set KeyW 1e3\n0 end\n", ScriptError::InvalidNumber, 1, "1e3", true},
      ErrorRow{"0 set KeyQ 1\n0 end\n", ScriptError::UnknownElement, 1, "KeyQ", true},
      ErrorRow{"0 mark a.b\n0 end\n", ScriptError::InvalidMarkName, 1, "a.b", true},
      ErrorRow{"5 set KeyW 1\n4 end\n", ScriptError::TickDecreases, 2, "4", true},
      ErrorRow{"0 set KeyW 1\n# no end\n", ScriptError::MissingEnd, 2, {}, false},
      ErrorRow{"0 end\n1 set KeyW 0\n", ScriptError::AfterEnd, 2, {}, false},
  };
  for (std::size_t row = 0; row < kErrors.size(); ++row) {
    const ErrorRow& expected = kErrors[row];
    const bw::platform::InputScript failed = bw::platform::parse_script(expected.text);
    require(failed.error == expected.error, row, "the rule's error is named");
    require(failed.line == expected.line, row, "the line in error is given");
    require(!expected.word_named || failed.word == expected.word, row,
            "the offending word is given");
    const bool views_text = failed.word.data() >= expected.text.data() &&
                            failed.word.data() <= expected.text.data() + expected.text.size();
    // MissingEnd names no word (contract 2), so nothing is asserted of it.
    require(expected.error == ScriptError::MissingEnd || views_text, row,
            "the word views the text");
  }
}

using Scenario = bw::testing::Scenario<void>;

constexpr std::array kScenarios{
    Scenario{"parse_table", &parse_table},
};

} // namespace

int main(int argc, char** argv) { return bw::testing::run_scenario(argc, argv, kScenarios); }
