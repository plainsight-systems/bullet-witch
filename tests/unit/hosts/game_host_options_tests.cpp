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
  double fixed_step{};
  std::string_view script{};
  std::string_view dump{};
  bool action_log{};
};

// (game_host_options.hpp, the rows; option_table.hpp, Contract 1, 2 and 4;
// seconds_limit_reached): the defaults; all six options, in an order other
// than the table's, each stored in its own member; one row per error name,
// each breaking one rule and naming its argument; the usage line names
// every option; 0 means until close. The four rows of the scripted run:
// all four given with --cache and --asset, each stored in its own member; a
// fixed step of 0, an empty script and an empty dump refused; --fixed-step
// needing --asset, --script needing --fixed-step, --dump needing --script
// and --action-log needing --asset.
void parse_table() {
  const std::string usage = bw::hosts::game_host_usage();
  for (const std::string_view option :
       {"--seconds N", "--fullscreen]", "--shaders DIR", "--cache DIR", "--overrides DIR",
        "--asset ID", "--fixed-step S", "--script FILE", "--dump DIR", "--action-log]"}) {
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
      Row{{"--fixed-step", "0.5", "--script", "s", "--dump", "d"},
          6,
          OptionError::RequiredOptionMissing,
          "--fixed-step"},
      Row{{"--fixed-step", "0"}, 2, OptionError::InvalidValue, "0"},
      Row{{"--script", ""}, 2, OptionError::InvalidValue, ""},
      Row{{"--dump", ""}, 2, OptionError::InvalidValue, ""},
      Row{{"--script", "s"}, 2, OptionError::RequiredOptionMissing, "--script"},
      Row{{"--dump", "d"}, 2, OptionError::RequiredOptionMissing, "--dump"},
      Row{{"--action-log"}, 1, OptionError::RequiredOptionMissing, "--action-log"},
      Row{{"--cache", "c", "--asset", "model.pw0.mesh", "--fixed-step", "0.5", "--script", "s",
           "--dump", "d", "--action-log"},
          11,
          OptionError::None,
          "",
          0.0,
          false,
          "",
          "c",
          "",
          "model.pw0.mesh",
          0.5,
          "s",
          "d",
          true},
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
         result.options.asset == row.asset && result.options.fixed_step == row.fixed_step &&
         result.options.script == std::filesystem::path(row.script) &&
         result.options.dump == std::filesystem::path(row.dump) &&
         result.options.action_log == row.action_log);
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
