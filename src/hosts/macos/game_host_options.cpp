// game_host's command line (bw/hosts/game_host_options.hpp): the grammar and
// the precedence the header states, with no environment, file system or
// global read.
#include "bw/hosts/game_host_options.hpp"

#include <charconv>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <span>
#include <string_view>
#include <system_error>

namespace bw::hosts {
namespace {

// The value of the option at `option` is missing: the next argument is
// absent or starts with "--".
bool value_missing(std::span<const std::string_view> arguments, std::size_t option) {
  return option + 1 == arguments.size() || arguments[option + 1].starts_with("--");
}

// MalformedSeconds unless the whole of `text` spells a number; then
// SecondsOutOfRange unless it is finite and non-negative.
UsageError parse_seconds(std::string_view text, double& seconds) {
  double value = 0.0;
  const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
  if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size()) {
    return UsageError::MalformedSeconds;
  }
  if (!std::isfinite(value) || value < 0.0) {
    return UsageError::SecondsOutOfRange;
  }
  seconds = value;
  return UsageError::None;
}

GameHostParse failure(UsageError error, std::string_view argument) {
  GameHostParse result{};
  result.error = error;
  result.argument = argument;
  return result;
}

} // namespace

GameHostParse parse_game_host_options(std::span<const std::string_view> arguments) {
  GameHostParse result{};
  bool seen_seconds = false;
  bool seen_fullscreen = false;
  bool seen_shaders = false;
  for (std::size_t i = 0; i < arguments.size(); ++i) {
    const std::string_view argument = arguments[i];
    if (argument == "--seconds") {
      if (seen_seconds) {
        return failure(UsageError::DuplicateOption, argument);
      }
      seen_seconds = true;
      if (value_missing(arguments, i)) {
        return failure(UsageError::MissingValue, argument);
      }
      ++i;
      const UsageError error = parse_seconds(arguments[i], result.options.seconds);
      if (error != UsageError::None) {
        return failure(error, arguments[i]);
      }
    } else if (argument == "--fullscreen") {
      if (seen_fullscreen) {
        return failure(UsageError::DuplicateOption, argument);
      }
      seen_fullscreen = true;
      result.options.fullscreen = true;
    } else if (argument == "--shaders") {
      if (seen_shaders) {
        return failure(UsageError::DuplicateOption, argument);
      }
      seen_shaders = true;
      if (value_missing(arguments, i)) {
        return failure(UsageError::MissingValue, argument);
      }
      ++i;
      if (arguments[i].empty()) {
        return failure(UsageError::EmptyDirectory, arguments[i]);
      }
      result.options.shaders = std::filesystem::path(arguments[i]);
    } else {
      return failure(UsageError::UnknownOption, argument);
    }
  }
  return result;
}

} // namespace bw::hosts
