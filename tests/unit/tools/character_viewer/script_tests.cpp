// Unit test: character_viewer's input script
// (bw/character_viewer/script.hpp, parse_script), one table scenario over
// every command form and one error row per error name.
#include "bw/character_viewer/script.hpp"

#include "bw/platform/window/window.hpp"
#include "bw/testing/harness.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace {

using bw::character_viewer::ScriptCommand;
using bw::character_viewer::ScriptCommandKind;
using bw::character_viewer::ScriptError;
using bw::platform::Key;
using bw::platform::MouseButton;
using bw::platform::PixelPoint;
using bw::testing::require;

ScriptCommand event_of(bw::platform::WindowEvent event) {
  return ScriptCommand{ScriptCommandKind::Event, event, {}};
}

// (script.hpp, contracts 1 and 2): each form parses to its constructor's
// event, or to the dump's trimmed remainder, in line order, past a comment,
// a blank line, a tab and a CRLF ending, a button named twice counting once; then each error name,
// with its 1-based line and the word it views in the text.
void parse_table() {
  const std::uint8_t left = bw::platform::mouse_button_bit(MouseButton::Left);
  const std::uint8_t all =
      static_cast<std::uint8_t>(left | bw::platform::mouse_button_bit(MouseButton::Right) |
                                bw::platform::mouse_button_bit(MouseButton::Middle));
  constexpr std::string_view kText = "move 10 20\n"
                                     "move -5 7 left\n"
                                     "move 1 2 left right middle\n"
                                     "move 1 2 left left\n"
                                     "# a comment\n"
                                     "\n"
                                     "button right down 3 4\n"
                                     "button middle up 5 6\n"
                                     "scroll\t-2.5\n"
                                     "key Escape\n"
                                     "key R down\r\n"
                                     "key Left up\n"
                                     "key Up down\n"
                                     "key Down up\n"
                                     "key Equal\n"
                                     "key Minus down\n"
                                     "key LeftBracket up\n"
                                     "key RightBracket\n"
                                     "key W\n"
                                     "key A up\n"
                                     "key S down\n"
                                     "key D\n"
                                     "dump  out dir/f.png \n";
  const std::vector<ScriptCommand> expected{
      event_of(bw::platform::mouse_moved_event(PixelPoint{10, 20}, 0)),
      event_of(bw::platform::mouse_moved_event(PixelPoint{-5, 7}, left)),
      event_of(bw::platform::mouse_moved_event(PixelPoint{1, 2}, all)),
      event_of(bw::platform::mouse_moved_event(PixelPoint{1, 2}, left)),
      event_of(bw::platform::mouse_button_event(MouseButton::Right, true, PixelPoint{3, 4})),
      event_of(bw::platform::mouse_button_event(MouseButton::Middle, false, PixelPoint{5, 6})),
      event_of(bw::platform::scrolled_event(-2.5F)),
      event_of(bw::platform::key_event(Key::Escape, true)),
      event_of(bw::platform::key_event(Key::R, true)),
      event_of(bw::platform::key_event(Key::Left, false)),
      event_of(bw::platform::key_event(Key::Up, true)),
      event_of(bw::platform::key_event(Key::Down, false)),
      event_of(bw::platform::key_event(Key::Equal, true)),
      event_of(bw::platform::key_event(Key::Minus, true)),
      event_of(bw::platform::key_event(Key::LeftBracket, false)),
      event_of(bw::platform::key_event(Key::RightBracket, true)),
      event_of(bw::platform::key_event(Key::W, true)),
      event_of(bw::platform::key_event(Key::A, false)),
      event_of(bw::platform::key_event(Key::S, true)),
      event_of(bw::platform::key_event(Key::D, true)),
      ScriptCommand{ScriptCommandKind::Dump, {}, "out dir/f.png"},
  };
  const bw::character_viewer::ScriptParse parsed = bw::character_viewer::parse_script(kText);
  require(parsed.ok(), "the forms parse: error " +
                           std::to_string(static_cast<unsigned>(parsed.error)) + " at line " +
                           std::to_string(parsed.line) + " '" + std::string(parsed.word) + "'");
  require(parsed.commands.size() == expected.size(),
          "commands: " + std::to_string(parsed.commands.size()));
  for (std::size_t i = 0; i < expected.size(); ++i) {
    const ScriptCommand& got = parsed.commands[i];
    require(got.kind == expected[i].kind && got.event == expected[i].event &&
                got.file == expected[i].file,
            i, "the command is the line's");
  }

  struct Row {
    std::string_view text;
    ScriptError error;
    std::size_t line;
    std::string_view word;
  };
  constexpr std::array rows{
      Row{"# c\n\nkey R\njump 1", ScriptError::UnknownCommand, 4, "jump"},
      Row{"scroll", ScriptError::ArgumentCount, 1, "scroll"},
      Row{"move 10 2x", ScriptError::InvalidNumber, 1, "2x"},
      Row{"key Q", ScriptError::UnknownKey, 1, "Q"},
      Row{"button left press 1 2", ScriptError::UnknownButton, 1, "press"},
      Row{"dump \t ", ScriptError::EmptyDumpFile, 1, "dump"},
  };
  for (std::size_t i = 0; i < rows.size(); ++i) {
    const Row& row = rows[i];
    const bw::character_viewer::ScriptParse parse = bw::character_viewer::parse_script(row.text);
    require(parse.error == row.error, i,
            "error " + std::to_string(static_cast<unsigned>(parse.error)));
    require(parse.line == row.line, i, "line " + std::to_string(parse.line));
    const std::less_equal<const char*> at_or_before;
    require(
        parse.word == row.word && at_or_before(row.text.data(), parse.word.data()) &&
            at_or_before(parse.word.data() + parse.word.size(), row.text.data() + row.text.size()),
        i, "the word viewing the text: '" + std::string(parse.word) + "'");
  }
}

using Scenario = bw::testing::Scenario<void>;

constexpr std::array kScenarios{
    Scenario{"parse_table", &parse_table},
};

} // namespace

int main(int argc, char** argv) { return bw::testing::run_scenario(argc, argv, kScenarios); }
