#include "bw/platform/input/scripted_device.hpp"

#include "bw/platform/input/device_input.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

// The scripted device's parse (scripted_device.hpp): the text is split into
// lines and words by index, so every word and error view points into the
// caller's text, and each line is checked in one order: a command after
// `end`, the word count before the command is known, TICK, the command
// word, the command's word count, the tick order, then the command's own
// arguments. A line breaks one rule at most in every scripted case the
// header names; which error a line breaking two rules gets is not part of
// the contract.
//
// Empty text has no lines, so its MissingEnd has line 0; otherwise
// MissingEnd's line is the last line, and its word is the empty view at the
// end of the text. A final line without '\n' is still a line.
namespace bw::platform {
namespace {

constexpr std::size_t kMaxWords = 5;

struct Words {
  std::array<std::string_view, kMaxWords> word{};
  // Every word on the line, though only the first kMaxWords are kept.
  std::size_t count{};
};

[[nodiscard]] constexpr bool is_blank(char c) noexcept { return c == ' ' || c == '\t'; }
[[nodiscard]] constexpr bool is_digit(char c) noexcept { return c >= '0' && c <= '9'; }

[[nodiscard]] Words split_words(std::string_view line) noexcept {
  Words words{};
  std::size_t at = 0;
  while (at < line.size()) {
    while (at < line.size() && is_blank(line[at])) {
      ++at;
    }
    if (at == line.size()) {
      break;
    }
    const std::size_t start = at;
    while (at < line.size() && !is_blank(line[at])) {
      ++at;
    }
    if (words.count < kMaxWords) {
      words.word[words.count] = line.substr(start, at - start);
    }
    ++words.count;
  }
  return words;
}

// TICK: decimal digits only, the whole word, at most kScriptMaxTick.
[[nodiscard]] std::optional<std::uint32_t> parse_tick(std::string_view word) noexcept {
  if (word.empty() || !std::ranges::all_of(word, is_digit)) {
    return std::nullopt;
  }
  std::uint64_t value = 0;
  for (const char c : word) {
    value = value * 10U + static_cast<std::uint64_t>(c - '0');
    if (value > kScriptMaxTick) {
      return std::nullopt;
    }
  }
  return static_cast<std::uint32_t>(value);
}

// VALUE: an optional sign, digits with at most one '.', at least one digit,
// no exponent, finite as a float.
[[nodiscard]] std::optional<float> parse_value(std::string_view word) noexcept {
  std::string_view body = word;
  bool negative = false;
  if (!body.empty() && (body.front() == '-' || body.front() == '+')) {
    negative = body.front() == '-';
    body.remove_prefix(1);
  }
  std::size_t digits = 0;
  std::size_t points = 0;
  for (const char c : body) {
    if (is_digit(c)) {
      ++digits;
    } else if (c == '.') {
      ++points;
    } else {
      return std::nullopt;
    }
  }
  if (digits == 0U || points > 1U) {
    return std::nullopt;
  }
  float value = 0.0F;
  const auto [end, error] =
      std::from_chars(body.data(), body.data() + body.size(), value, std::chars_format::fixed);
  if (error != std::errc{} || end != body.data() + body.size() || !std::isfinite(value)) {
    return std::nullopt;
  }
  return negative ? -value : value;
}

[[nodiscard]] constexpr bool is_mark_name(std::string_view name) noexcept {
  return !name.empty() && std::ranges::all_of(name, [](char c) {
    return is_digit(c) || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '-' || c == '_';
  });
}

struct LineError {
  ScriptError error{ScriptError::None};
  std::string_view word{};
};

// One non-skipped line into `script`, or the rule it breaks.
[[nodiscard]] LineError parse_line(const Words& words, bool ended, InputScript& script) {
  const std::string_view first = words.word[0];
  if (ended) {
    return {ScriptError::AfterEnd, first};
  }
  if (words.count < 2U) {
    return {ScriptError::ArgumentCount, first};
  }
  const std::optional<std::uint32_t> tick = parse_tick(first);
  if (!tick) {
    return {ScriptError::InvalidNumber, first};
  }
  const std::string_view command = words.word[1];
  ScriptCommandKind kind = ScriptCommandKind::End;
  std::size_t expected = 0;
  if (command == "set") {
    kind = ScriptCommandKind::Set;
    expected = 4;
  } else if (command == "mark") {
    kind = ScriptCommandKind::Mark;
    expected = 3;
  } else if (command == "end") {
    kind = ScriptCommandKind::End;
    expected = 2;
  } else {
    return {ScriptError::UnknownCommand, command};
  }
  if (words.count != expected) {
    // The first word past the command's count, or the command when short.
    return {ScriptError::ArgumentCount, words.count > expected ? words.word[expected] : command};
  }
  if (!script.commands.empty() && *tick < script.commands.back().tick) {
    return {ScriptError::TickDecreases, first};
  }
  ScriptCommand parsed{};
  parsed.kind = kind;
  parsed.tick = *tick;
  if (kind == ScriptCommandKind::Set) {
    const std::optional<InputElement> element = element_from_name(words.word[2]);
    if (!element) {
      return {ScriptError::UnknownElement, words.word[2]};
    }
    const std::optional<float> value = parse_value(words.word[3]);
    if (!value) {
      return {ScriptError::InvalidNumber, words.word[3]};
    }
    parsed.sample = InputSample{*element, *value};
  } else if (kind == ScriptCommandKind::Mark) {
    if (!is_mark_name(words.word[2])) {
      return {ScriptError::InvalidMarkName, words.word[2]};
    }
    parsed.name = std::string(words.word[2]);
  }
  script.commands.push_back(std::move(parsed));
  return {};
}

} // namespace

InputScript parse_script(std::string_view text) {
  InputScript script{};
  bool ended = false;
  std::size_t line_number = 0;
  std::size_t at = 0;
  while (at < text.size()) {
    const std::size_t newline = text.find('\n', at);
    const std::size_t stop = newline == std::string_view::npos ? text.size() : newline;
    std::string_view line = text.substr(at, stop - at);
    if (!line.empty() && line.back() == '\r') {
      line.remove_suffix(1);
    }
    at = newline == std::string_view::npos ? text.size() : newline + 1U;
    ++line_number;

    const Words words = split_words(line);
    if (words.count == 0U || words.word[0].front() == '#') {
      continue;
    }
    const LineError error = parse_line(words, ended, script);
    if (error.error != ScriptError::None) {
      script.commands.clear();
      script.error = error.error;
      script.line = line_number;
      script.word = error.word;
      return script;
    }
    ended = script.commands.back().kind == ScriptCommandKind::End;
  }
  if (!ended) {
    script.commands.clear();
    script.error = ScriptError::MissingEnd;
    script.line = line_number;
    script.word = text.substr(text.size());
  }
  return script;
}

// Ticks never decrease (contract 1), so a tick's commands are one run found
// by binary search; nothing is allocated.
std::span<const ScriptCommand> commands_at(const InputScript& script, std::uint32_t tick) noexcept {
  const auto range = std::ranges::equal_range(script.commands, tick, {}, &ScriptCommand::tick);
  return {range.begin(), range.end()};
}

std::uint32_t last_tick(const InputScript& script) noexcept { return script.commands.back().tick; }

} // namespace bw::platform
