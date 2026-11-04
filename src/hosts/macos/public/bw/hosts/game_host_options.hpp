#ifndef BW_HOSTS_GAME_HOST_OPTIONS_HPP
#define BW_HOSTS_GAME_HOST_OPTIONS_HPP

#include <cstdint>
#include <filesystem>
#include <span>
#include <string_view>

// Responsibility: game_host's command line as a pure parse. The host passes
// its arguments after the program name; nothing here reads the environment,
// the file system or a global, so the same arguments always give the same
// result.
//
// Grammar, each option at most once, in any order:
//   --seconds N       N a finite, non-negative decimal number spelled by the
//                     whole argument; 0, the default, means the host runs
//                     until the window is asked to close (seconds_limit_reached).
//   --fullscreen      the window starts in borderless fullscreen.
//   --shaders DIR     the Metal shader artifact directory, a non-empty path.
//                     Absent, shaders stays empty and the host resolves its
//                     default from the executable's location.
// The argument after --seconds or --shaders is that option's value; the
// value is missing when that argument is absent or starts with "--".
//
// Precedence: arguments are read left to right and the first error stops the
// parse; that error and the argument it names are returned. Within one
// option: an option given before is DuplicateOption, before its value is
// looked at; then a missing value is MissingValue; then the value is checked
// (MalformedSeconds before SecondsOutOfRange; EmptyDirectory).
//
// On any error the host prints kGameHostUsage and exits 2.
namespace bw::hosts {

inline constexpr std::string_view kGameHostUsage =
    "Usage: game_host [--seconds N] [--fullscreen] [--shaders DIRECTORY]";

struct GameHostOptions {
  double seconds{};
  bool fullscreen{};
  std::filesystem::path shaders{};
};

enum class UsageError : std::uint8_t {
  None,
  // An argument that is not one of the three options.
  UnknownOption,
  // --seconds or --shaders as the last argument, or followed by an argument
  // that starts with "--".
  MissingValue,
  // The --seconds value is not a decimal number spelled by the whole argument.
  MalformedSeconds,
  // The --seconds value is a number that is negative, infinite or not a number.
  SecondsOutOfRange,
  // An option given a second time.
  DuplicateOption,
  // The --shaders value is empty.
  EmptyDirectory,
};

// options is meaningful when error is None. argument names the argument the
// error is about (the option for DuplicateOption and MissingValue, the value
// for MalformedSeconds, SecondsOutOfRange and EmptyDirectory, the argument
// itself for UnknownOption); it views the caller's argument and is empty
// when error is None.
struct GameHostParse {
  GameHostOptions options{};
  UsageError error{UsageError::None};
  std::string_view argument{};
};

// The arguments after the program name, per the grammar and precedence above.
[[nodiscard]] GameHostParse parse_game_host_options(std::span<const std::string_view> arguments);

// Whether a run that has lasted `elapsed_seconds` has reached the --seconds
// limit `limit_seconds` (GameHostOptions::seconds): never when the limit is
// 0 (the run lasts until the window is asked to close), otherwise once
// elapsed_seconds is at least the limit.
[[nodiscard]] constexpr bool seconds_limit_reached(double limit_seconds,
                                                   double elapsed_seconds) noexcept {
  return limit_seconds > 0.0 && elapsed_seconds >= limit_seconds;
}

} // namespace bw::hosts
#endif
