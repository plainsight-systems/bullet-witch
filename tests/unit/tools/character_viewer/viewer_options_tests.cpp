// Unit test: character_viewer's command line
// (bw/character_viewer/viewer_options.hpp over bw/hosts/option_table.hpp),
// one table scenario over what this table adds: the grammar's rules
// game_host's parse test already asserts are not repeated here.
#include "bw/character_viewer/viewer_options.hpp"

#include "bw/hosts/option_table.hpp"
#include "bw/testing/harness.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>

namespace {

using bw::hosts::OptionError;
using bw::testing::require;

// One parse: up to eight arguments, the error and the argument it names,
// then on success the size and the assets.
struct Row {
  std::array<std::string_view, 8> args{};
  std::size_t count{};
  OptionError error{OptionError::None};
  std::string_view argument{};
  std::uint32_t width{1280};
  std::uint32_t height{720};
  std::array<std::string_view, 2> assets{};
  std::size_t asset_count{};
  std::string_view script{};
  std::uint32_t grid{1};
};

// (viewer_options.hpp, the rows --size, --asset, --dump, --script and --grid;
// option_table.hpp, contract 1, 2 and 4 as amended for a repeatable row): the
// default size; two --asset accumulate in argument order; --size's accepted
// and refused forms; --dump and --script without --asset and with an empty
// file; --script stored; --grid's default, accepted value and refusal
// above kMaxViewerGrid; the usage line's repeatable, --dump and --script
// entries.
void rows() {
  const std::string usage = bw::character_viewer::viewer_usage();
  require(usage.find(" [--asset ID]...") != std::string::npos,
          "the usage line marks --asset repeatable: " + usage);
  require(usage.find(" [--dump FILE]") != std::string::npos,
          "the usage line names --dump: " + usage);
  require(usage.find(" [--script FILE]") != std::string::npos,
          "the usage line names --script: " + usage);
  constexpr std::array table{
      Row{{}, 0, OptionError::None, ""},
      Row{{"--cache", "c", "--asset", "model.b", "--asset", "model.a", "--dump", "f.png"},
          8,
          OptionError::None,
          "",
          1280,
          720,
          {"model.b", "model.a"},
          2},
      Row{{"--size", "640x480"}, 2, OptionError::None, "", 640, 480},
      Row{{"--size", "640"}, 2, OptionError::InvalidValue, "640"},
      Row{{"--size", "0x480"}, 2, OptionError::InvalidValue, "0x480"},
      Row{{"--size", "640x"}, 2, OptionError::InvalidValue, "640x"},
      Row{{"--size", "20000x10"}, 2, OptionError::InvalidValue, "20000x10"},
      Row{{"--dump", "f.png"}, 2, OptionError::RequiredOptionMissing, "--dump"},
      Row{{"--cache", "c", "--asset", "model.a", "--dump", ""}, 6, OptionError::InvalidValue, ""},
      Row{{"--cache", "c", "--asset", "model.a", "--script", "s.txt"},
          6,
          OptionError::None,
          "",
          1280,
          720,
          {"model.a"},
          1,
          "s.txt"},
      Row{{"--script", "s.txt"}, 2, OptionError::RequiredOptionMissing, "--script"},
      Row{{"--cache", "c", "--asset", "model.a", "--script", ""}, 6, OptionError::InvalidValue, ""},
      Row{{"--cache", "c", "--asset", "model.a", "--grid", "3"},
          6,
          OptionError::None,
          "",
          1280,
          720,
          {"model.a"},
          1,
          "",
          3},
      Row{{"--cache", "c", "--asset", "model.a", "--grid", "65"},
          6,
          OptionError::InvalidValue,
          "65"},
  };
  for (std::size_t i = 0; i < table.size(); ++i) {
    const Row& row = table[i];
    const bw::character_viewer::ViewerParse parse =
        bw::character_viewer::parse_viewer_options(std::span(row.args.data(), row.count));
    require(parse.error == row.error, i,
            "error " + std::to_string(static_cast<unsigned>(parse.error)));
    require(parse.argument == row.argument, i, "named " + std::string(parse.argument));
    if (row.error != OptionError::None) {
      continue;
    }
    require(parse.options.width == row.width && parse.options.height == row.height, i,
            "the size is " + std::to_string(parse.options.width) + "x" +
                std::to_string(parse.options.height));
    require(parse.options.assets.size() == row.asset_count, i, "the asset count");
    for (std::size_t a = 0; a < row.asset_count; ++a) {
      require(parse.options.assets[a] == row.assets.at(a), i, "the assets in argument order");
    }
    require(parse.options.script == row.script, i, "the script " + parse.options.script.string());
    require(parse.options.grid == row.grid, i, "the grid " + std::to_string(parse.options.grid));
  }
}

using Scenario = bw::testing::Scenario<void>;

constexpr std::array kScenarios{
    Scenario{"rows", &rows},
};

} // namespace

int main(int argc, char** argv) { return bw::testing::run_scenario(argc, argv, kScenarios); }
