// Unit test: game_host's command line (bw/hosts/game_host_options.hpp,
// parse_game_host_options, UsageError, seconds_limit_reached), one
// table-driven scenario.
#include "bw/hosts/game_host_options.hpp"

#include "bw/testing/harness.hpp"

#include <array>
#include <cstddef>
#include <filesystem>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

using bw::hosts::UsageError;

// One parse: up to five arguments (count of them used, since "" is a real
// argument), then the error and the argument it names, then on success the
// options.
struct Row {
  std::array<std::string_view, 5> args{};
  std::size_t count{};
  UsageError error{UsageError::None};
  std::string_view argument{};
  double seconds{};
  bool fullscreen{};
  std::string_view shaders{};
};

constexpr UsageError kNone = UsageError::None;
constexpr UsageError kUnknown = UsageError::UnknownOption;
constexpr UsageError kMissing = UsageError::MissingValue;
constexpr UsageError kMalformed = UsageError::MalformedSeconds;
constexpr UsageError kRange = UsageError::SecondsOutOfRange;
constexpr UsageError kDuplicate = UsageError::DuplicateOption;
constexpr UsageError kEmpty = UsageError::EmptyDirectory;

// (game_host_options.hpp, GameHostOptions defaults, Grammar, Precedence,
// UsageError, GameHostParse::argument, seconds_limit_reached): the defaults;
// each option alone and all three in any order; a value starting with "--"
// is missing, one "-" is a value; every UsageError naming its argument;
// DuplicateOption before the value is looked at; left to right, the first
// error wins; 0 means until close.
void parse_table() {
  static_assert(!bw::hosts::seconds_limit_reached(0.0, 1.0e9));
  static_assert(bw::hosts::seconds_limit_reached(1.0, 1.0));
  static_assert(!bw::hosts::seconds_limit_reached(1.0, 0.999));
  constexpr std::array rows{
      Row{{}, 0, kNone, "", 0.0, false, ""},
      Row{{"--seconds", "2.5"}, 2, kNone, "", 2.5, false, ""},
      Row{{"--seconds", "0"}, 2, kNone, "", 0.0, false, ""},
      Row{{"--fullscreen"}, 1, kNone, "", 0.0, true, ""},
      Row{{"--shaders", "artifacts/metal"}, 2, kNone, "", 0.0, false, "artifacts/metal"},
      Row{{"--shaders", "s", "--fullscreen", "--seconds", "1"}, 5, kNone, "", 1.0, true, "s"},
      Row{{"--shaders", "-dir"}, 2, kNone, "", 0.0, false, "-dir"},
      Row{{"--shaders", "--fullscreen"}, 2, kMissing, "--shaders"},
      Row{{"--seconds", "--fullscreen"}, 2, kMissing, "--seconds"},
      Row{{"--seconds"}, 1, kMissing, "--seconds"},
      Row{{"--fullscreen", "--shaders"}, 2, kMissing, "--shaders"},
      Row{{"--frames"}, 1, kUnknown, "--frames"},
      Row{{"seconds"}, 1, kUnknown, "seconds"},
      Row{{""}, 1, kUnknown, ""},
      Row{{"--fullscreen", "--Fullscreen"}, 2, kUnknown, "--Fullscreen"},
      Row{{"--seconds", "abc"}, 2, kMalformed, "abc"},
      Row{{"--seconds", ""}, 2, kMalformed, ""},
      Row{{"--seconds", "1x"}, 2, kMalformed, "1x"},
      Row{{"--seconds", " 1"}, 2, kMalformed, " 1"},
      Row{{"--seconds", "-1"}, 2, kRange, "-1"},
      Row{{"--seconds", "-0.5"}, 2, kRange, "-0.5"},
      Row{{"--seconds", "inf"}, 2, kRange, "inf"},
      Row{{"--seconds", "nan"}, 2, kRange, "nan"},
      Row{{"--fullscreen", "--fullscreen"}, 2, kDuplicate, "--fullscreen"},
      Row{{"--seconds", "1", "--seconds", "2"}, 4, kDuplicate, "--seconds"},
      Row{{"--shaders", "a", "--shaders", "b"}, 4, kDuplicate, "--shaders"},
      Row{{"--seconds", "1", "--seconds"}, 3, kDuplicate, "--seconds"},
      Row{{"--seconds", "1", "--seconds", "abc"}, 4, kDuplicate, "--seconds"},
      Row{{"--shaders", "a", "--shaders", ""}, 4, kDuplicate, "--shaders"},
      Row{{"--shaders", ""}, 2, kEmpty, ""},
      Row{{"--bogus", "--seconds"}, 2, kUnknown, "--bogus"},
      Row{{"--seconds", "-1", "--bogus"}, 3, kRange, "-1"},
      Row{{"--shaders", "", "--fullscreen", "--fullscreen"}, 4, kEmpty, ""},
  };
  for (std::size_t i = 0; i < rows.size(); ++i) {
    const Row& row = rows[i];
    const bw::hosts::GameHostParse result =
        bw::hosts::parse_game_host_options(std::span(row.args).first(row.count));
    const bool options_match =
        row.error != kNone ||
        (result.options.seconds == row.seconds && result.options.fullscreen == row.fullscreen &&
         result.options.shaders == std::filesystem::path(row.shaders));
    if (result.error != row.error || result.argument != row.argument || !options_match) {
      throw std::runtime_error("row " + std::to_string(i) + ": error " +
                               std::to_string(static_cast<int>(result.error)) + " naming \"" +
                               std::string(result.argument) + "\"");
    }
  }
}

using Scenario = bw::testing::Scenario<void>;

constexpr std::array kScenarios{
    Scenario{"parse_table", &parse_table},
};

} // namespace

int main(int argc, char** argv) { return bw::testing::run_scenario(argc, argv, kScenarios); }
