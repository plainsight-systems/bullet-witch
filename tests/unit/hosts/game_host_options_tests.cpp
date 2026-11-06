// Unit test: game_host's command line (bw/hosts/game_host_options.hpp over
// bw/hosts/option_table.hpp), one table-driven scenario.
#include "bw/hosts/game_host_options.hpp"

#include "bw/hosts/option_table.hpp"
#include "bw/testing/harness.hpp"

#include <array>
#include <cstddef>
#include <filesystem>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

using bw::hosts::OptionError;
using bw::testing::require;

// One parse: up to eleven arguments, then the error and the argument it
// names, then on success the options.
struct Row {
  std::array<std::string_view, 11> args{};
  std::size_t count{};
  OptionError error{OptionError::None};
  std::string_view argument{};
  double seconds{};
  bool fullscreen{};
  std::string_view shaders{};
  std::string_view cache{};
  std::string_view overrides{};
  std::string_view asset{};
};

// (game_host_options.hpp, the rows; option_table.hpp, Contract 1, 2 and 4;
// seconds_limit_reached): the defaults; all six options, in an order other
// than the table's, each stored in its own member; one row per error name,
// each breaking one rule and naming its argument; the usage line names
// every option; 0 means until close.
void parse_table() {
  const std::string usage = bw::hosts::game_host_usage();
  for (const std::string_view option : {"--seconds N", "--fullscreen]", "--shaders DIR",
                                        "--cache DIR", "--overrides DIR", "--asset ID"}) {
    require(usage.find(option) != std::string::npos, "the usage line names " + std::string(option));
  }
  static_assert(!bw::hosts::seconds_limit_reached(0.0, 1.0e9));
  static_assert(bw::hosts::seconds_limit_reached(1.0, 1.0));
  static_assert(!bw::hosts::seconds_limit_reached(1.0, 0.999));
  constexpr std::array rows{
      Row{{}, 0, OptionError::None, "", 0.0, false, ""},
      Row{{"--overrides", "o", "--asset", "model.pw0.mesh", "--fullscreen", "--cache", "c",
           "--seconds", "2.5", "--shaders", "-dir"},
          11,
          OptionError::None,
          "",
          2.5,
          true,
          "-dir",
          "c",
          "o",
          "model.pw0.mesh"},
      Row{{"--frames"}, 1, OptionError::UnknownOption, "--frames"},
      Row{{"--seconds", "1", "--seconds", "2"}, 4, OptionError::DuplicateOption, "--seconds"},
      Row{{"--shaders", "--fullscreen"}, 2, OptionError::MissingValue, "--shaders"},
      Row{{"--seconds", "-1"}, 2, OptionError::InvalidValue, "-1"},
      Row{{"--asset", "model.pw0.mesh"}, 2, OptionError::RequiredOptionMissing, "--asset"},
  };
  for (std::size_t i = 0; i < rows.size(); ++i) {
    const Row& row = rows[i];
    const bw::hosts::GameHostParse result =
        bw::hosts::parse_game_host_options(std::span(row.args).first(row.count));
    const bool options_match =
        row.error != OptionError::None ||
        (result.options.seconds == row.seconds && result.options.fullscreen == row.fullscreen &&
         result.options.shaders == std::filesystem::path(row.shaders) &&
         result.options.cache == std::filesystem::path(row.cache) &&
         result.options.overrides == std::filesystem::path(row.overrides) &&
         result.options.asset == row.asset);
    const bool reason_matches = (row.error == OptionError::InvalidValue) != result.reason.empty();
    if (result.error != row.error || result.argument != row.argument || !options_match ||
        !reason_matches) {
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
