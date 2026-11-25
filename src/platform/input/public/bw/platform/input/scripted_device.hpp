#ifndef BW_PLATFORM_INPUT_SCRIPTED_DEVICE_HPP
#define BW_PLATFORM_INPUT_SCRIPTED_DEVICE_HPP

#include "bw/platform/input/device_input.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// Responsibility: a scripted device: a text whose lines become the same
// InputSamples the window and a pad produce (device_input.hpp), tick by
// tick, plus named marks a host acts on, so a test or an agent drives the
// game with no hand on a device. It is a development device: a host reads
// it only when its command line names a script, and says so in its output.
// The parse is a pure function of the text; nothing here reads a file, a
// clock or a real device.
//
// Grammar. One command per line; lines end in '\n', with a '\r' before it
// dropped; words separated by spaces or tabs; a blank line or one whose
// first word starts with '#' is skipped. TICK is a decimal integer, 0 the
// first tick of the run; VALUE a finite decimal float with no exponent;
// each spelled by the whole word.
//   TICK set ELEMENT VALUE   one sample of ELEMENT (a name of
//                            device_input.hpp) handed over before tick TICK
//   TICK mark NAME           names tick TICK; NAME is one word of letters,
//                            digits, '-' and '_'
//   TICK end                 the run's last tick is TICK
//
// Contract.
//   1. parse_script(text) returns the commands in line order. Ticks never
//      decrease from one line to the next, and the script's last command
//      is its one `end`.
//   2. The first line in error ends the parse, with `line` the 1-based
//      line and `word` the offending word, viewing `text`; the script is
//      then not meaningful. One error per rule: UnknownCommand (a second
//      word not in the grammar), ArgumentCount (a word count the command
//      does not take), InvalidNumber (TICK or VALUE not spelled as above,
//      or TICK past kScriptMaxTick), UnknownElement (a name
//      element_from_name does not know), InvalidMarkName, TickDecreases
//      (a TICK lower than the line before's), MissingEnd (the text has no
//      `end`; `line` is the last line), AfterEnd (a command after `end`).
//   3. commands_at(script, tick) is the script's commands of that tick, in
//      line order: contiguous, since ticks never decrease. A Set command's
//      `sample` is handed to key state before the tick; a Mark command's
//      `name` names the tick. A tick with no command gives an empty span.
//      last_tick(script) is the `end` command's tick.
//   4. A sample is handed over exactly as a real device's would be: a
//      button stays down until a `set` of 0, a pad axis keeps its value
//      until another `set`, and mouse motion and the wheel are that
//      tick's alone. The script states devices' elements, never actions.
//
// Core guidelines read: I.1 and Con.5 (the parse reads the text and
// nothing else), E.27 (named errors with the line and word), F.20
// (commands by value), Enum.3. Performance guidelines read: MEM.9 (the
// script is parsed once, before the first tick; a tick's lookup allocates
// nothing).
namespace bw::platform {

inline constexpr std::uint32_t kScriptMaxTick = 1'000'000;

enum class ScriptCommandKind : std::uint8_t { Set, Mark, End };

struct ScriptCommand {
  ScriptCommandKind kind{ScriptCommandKind::Set};
  std::uint32_t tick{};
  // Set.
  InputSample sample{};
  // Mark.
  std::string name{};
};

enum class ScriptError : std::uint8_t {
  None,
  UnknownCommand,
  ArgumentCount,
  InvalidNumber,
  UnknownElement,
  InvalidMarkName,
  TickDecreases,
  MissingEnd,
  AfterEnd,
};

struct InputScript {
  std::vector<ScriptCommand> commands{};
  ScriptError error{ScriptError::None};
  std::size_t line{};
  std::string_view word{};
  [[nodiscard]] bool ok() const noexcept { return error == ScriptError::None; }
};

[[nodiscard]] InputScript parse_script(std::string_view text);

// Contract 3. Precondition for both: script.ok(). The span views the
// script.
[[nodiscard]] std::span<const ScriptCommand> commands_at(const InputScript& script,
                                                         std::uint32_t tick) noexcept;
[[nodiscard]] std::uint32_t last_tick(const InputScript& script) noexcept;

} // namespace bw::platform
#endif
