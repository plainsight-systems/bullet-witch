// character_viewer's input script (bw/character_viewer/script.hpp): the
// grammar's parse, a pure function of the text. Each line is split into
// words that view the text, so an error's word is a view of the caller's
// text; each command is built with window.hpp's input constructors.
#include "bw/character_viewer/script.hpp"

#include "bw/platform/window/window.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace bw::character_viewer {
namespace {

using platform::Key;
using platform::MouseButton;
using platform::PixelPoint;

constexpr std::string_view kBlanks = " \t";

// The most words a command takes: move X Y left right middle.
constexpr std::size_t kMaxWords = 6;

// The words of one line, at most kMaxWords kept; `count` is every word on
// the line, so a line with more is still seen as too long.
struct Words {
  std::array<std::string_view, kMaxWords> word{};
  std::size_t count{};
};

Words split_words(std::string_view line) {
  Words words;
  std::size_t at = line.find_first_not_of(kBlanks);
  while (at != std::string_view::npos) {
    const std::size_t end = std::min(line.find_first_of(kBlanks, at), line.size());
    if (words.count < kMaxWords) {
      words.word.at(words.count) = line.substr(at, end - at);
    }
    ++words.count;
    at = line.find_first_not_of(kBlanks, end);
  }
  return words;
}

// A decimal int32 spelled by the whole word.
std::optional<std::int32_t> integer_of(std::string_view word) {
  std::int32_t value = 0;
  const auto parsed = std::from_chars(word.data(), word.data() + word.size(), value);
  if (parsed.ec != std::errc{} || parsed.ptr != word.data() + word.size()) {
    return std::nullopt;
  }
  return value;
}

// A finite decimal float spelled by the whole word, with no exponent.
std::optional<float> lines_of(std::string_view word) {
  float value = 0.0F;
  const auto parsed =
      std::from_chars(word.data(), word.data() + word.size(), value, std::chars_format::fixed);
  if (parsed.ec != std::errc{} || parsed.ptr != word.data() + word.size() ||
      !std::isfinite(value)) {
    return std::nullopt;
  }
  return value;
}

std::optional<MouseButton> button_of(std::string_view word) {
  if (word == "left") {
    return MouseButton::Left;
  }
  if (word == "right") {
    return MouseButton::Right;
  }
  if (word == "middle") {
    return MouseButton::Middle;
  }
  return std::nullopt;
}

std::optional<Key> key_of(std::string_view word) {
  constexpr std::array<std::pair<std::string_view, Key>, 16> kKeys{{
      {"Escape", Key::Escape},
      {"R", Key::R},
      {"P", Key::P},
      {"W", Key::W},
      {"A", Key::A},
      {"S", Key::S},
      {"D", Key::D},
      {"Left", Key::Left},
      {"Right", Key::Right},
      {"Up", Key::Up},
      {"Down", Key::Down},
      {"Equal", Key::Equal},
      {"Minus", Key::Minus},
      {"LeftBracket", Key::LeftBracket},
      {"RightBracket", Key::RightBracket},
      {"Other", Key::Other},
  }};
  for (const auto& [name, key] : kKeys) {
    if (word == name) {
      return key;
    }
  }
  return std::nullopt;
}

// down is pressed, up released; nullopt for any other word.
std::optional<bool> pressed_of(std::string_view word) {
  if (word == "down") {
    return true;
  }
  if (word == "up") {
    return false;
  }
  return std::nullopt;
}

// One line's command, or its error and the offending word.
struct LineResult {
  ScriptCommand command{};
  ScriptError error{ScriptError::None};
  std::string_view word{};
};

LineResult failed(ScriptError error, std::string_view word) {
  LineResult result;
  result.error = error;
  result.word = word;
  return result;
}

LineResult event_command(platform::WindowEvent event) {
  LineResult result;
  result.command.kind = ScriptCommandKind::Event;
  result.command.event = event;
  return result;
}

// X and Y at words[first] and words[first + 1].
std::optional<PixelPoint> point_of(const Words& words, std::size_t first, LineResult& error) {
  const std::optional<std::int32_t> x = integer_of(words.word.at(first));
  if (!x.has_value()) {
    error = failed(ScriptError::InvalidNumber, words.word.at(first));
    return std::nullopt;
  }
  const std::optional<std::int32_t> y = integer_of(words.word.at(first + 1U));
  if (!y.has_value()) {
    error = failed(ScriptError::InvalidNumber, words.word.at(first + 1U));
    return std::nullopt;
  }
  return PixelPoint{*x, *y};
}

// move X Y [left] [right] [middle]
LineResult parse_move(const Words& words) {
  if (words.count < 3U || words.count > 6U) {
    return failed(ScriptError::ArgumentCount, words.word[0]);
  }
  LineResult error;
  const std::optional<PixelPoint> point = point_of(words, 1U, error);
  if (!point.has_value()) {
    return error;
  }
  std::uint8_t buttons = 0;
  for (std::size_t i = 3; i < words.count; ++i) {
    const std::optional<MouseButton> button = button_of(words.word.at(i));
    if (!button.has_value()) {
      return failed(ScriptError::UnknownButton, words.word.at(i));
    }
    buttons = static_cast<std::uint8_t>(buttons | platform::mouse_button_bit(*button));
  }
  return event_command(platform::mouse_moved_event(*point, buttons));
}

// button left|right|middle down|up X Y
LineResult parse_button(const Words& words) {
  if (words.count != 5U) {
    return failed(ScriptError::ArgumentCount, words.word[0]);
  }
  const std::optional<MouseButton> button = button_of(words.word[1]);
  if (!button.has_value()) {
    return failed(ScriptError::UnknownButton, words.word[1]);
  }
  const std::optional<bool> pressed = pressed_of(words.word[2]);
  if (!pressed.has_value()) {
    return failed(ScriptError::UnknownButton, words.word[2]);
  }
  LineResult error;
  const std::optional<PixelPoint> point = point_of(words, 3U, error);
  if (!point.has_value()) {
    return error;
  }
  return event_command(platform::mouse_button_event(*button, *pressed, *point));
}

// scroll LINES
LineResult parse_scroll(const Words& words) {
  if (words.count != 2U) {
    return failed(ScriptError::ArgumentCount, words.word[0]);
  }
  const std::optional<float> lines = lines_of(words.word[1]);
  if (!lines.has_value()) {
    return failed(ScriptError::InvalidNumber, words.word[1]);
  }
  return event_command(platform::scrolled_event(*lines));
}

// key NAME [down|up]
LineResult parse_key(const Words& words) {
  if (words.count != 2U && words.count != 3U) {
    return failed(ScriptError::ArgumentCount, words.word[0]);
  }
  const std::optional<Key> key = key_of(words.word[1]);
  if (!key.has_value()) {
    return failed(ScriptError::UnknownKey, words.word[1]);
  }
  bool pressed = true;
  if (words.count == 3U) {
    const std::optional<bool> word = pressed_of(words.word[2]);
    if (!word.has_value()) {
      return failed(ScriptError::UnknownKey, words.word[2]);
    }
    pressed = *word;
  }
  return event_command(platform::key_event(*key, pressed));
}

// dump FILE: the line after the command word, trimmed of blanks. `command`
// views `line`.
LineResult parse_dump(std::string_view line, std::string_view command) {
  const std::size_t after = static_cast<std::size_t>(command.data() - line.data()) + command.size();
  std::string_view rest = line.substr(after);
  const std::size_t first = rest.find_first_not_of(kBlanks);
  if (first == std::string_view::npos) {
    return failed(ScriptError::EmptyDumpFile, command);
  }
  rest = rest.substr(first, rest.find_last_not_of(kBlanks) - first + 1U);
  LineResult result;
  result.command.kind = ScriptCommandKind::Dump;
  result.command.file = std::string(rest);
  return result;
}

LineResult parse_line(std::string_view line, const Words& words) {
  const std::string_view command = words.word[0];
  if (command == "move") {
    return parse_move(words);
  }
  if (command == "button") {
    return parse_button(words);
  }
  if (command == "scroll") {
    return parse_scroll(words);
  }
  if (command == "key") {
    return parse_key(words);
  }
  if (command == "dump") {
    return parse_dump(line, command);
  }
  return failed(ScriptError::UnknownCommand, command);
}

} // namespace

ScriptParse parse_script(std::string_view text) {
  ScriptParse parse;
  std::size_t number = 0;
  for (std::size_t at = 0; at < text.size();) {
    const std::size_t end = std::min(text.find('\n', at), text.size());
    std::string_view line = text.substr(at, end - at);
    at = end + 1U;
    ++number;
    if (line.ends_with('\r')) {
      line.remove_suffix(1U);
    }
    const Words words = split_words(line);
    if (words.count == 0U || words.word[0].starts_with('#')) {
      continue;
    }
    LineResult result = parse_line(line, words);
    if (result.error != ScriptError::None) {
      parse.error = result.error;
      parse.line = number;
      parse.word = result.word;
      return parse;
    }
    parse.commands.push_back(std::move(result.command));
  }
  return parse;
}

} // namespace bw::character_viewer
