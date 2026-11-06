// game_host's command line (bw/hosts/game_host_options.hpp): the six rows
// and the functions that take their values, with no environment, file system
// or global read.
#include "bw/hosts/game_host_options.hpp"

#include "bw/content/asset_id.hpp"
#include "bw/hosts/option_table.hpp"

#include <array>
#include <charconv>
#include <cmath>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <system_error>

namespace bw::hosts {
namespace {

using Row = OptionRow<GameHostOptions>;

std::string_view take_seconds(GameHostOptions& options, std::string_view text) {
  double value = 0.0;
  const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
  if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size()) {
    return "is not a number";
  }
  if (!std::isfinite(value) || value < 0.0) {
    return "is not finite and non-negative";
  }
  options.seconds = value;
  return {};
}

std::string_view take_fullscreen(GameHostOptions& options, std::string_view /*value*/) {
  options.fullscreen = true;
  return {};
}

template <std::filesystem::path GameHostOptions::* Member>
std::string_view take_directory(GameHostOptions& options, std::string_view value) {
  if (value.empty()) {
    return "is an empty directory";
  }
  options.*Member = std::filesystem::path(value);
  return {};
}

// The empty value is an id that does not validate (asset_id.hpp, Empty).
std::string_view take_asset(GameHostOptions& options, std::string_view value) {
  if (bw::content::validate_asset_id(value) != bw::content::AssetIdError::None) {
    return "is not an asset id";
  }
  options.asset = std::string(value);
  return {};
}

constexpr std::array kRows{
    Row{"--seconds", "N", &take_seconds, {}},
    Row{"--fullscreen", {}, &take_fullscreen, {}},
    Row{"--shaders", "DIR", &take_directory<&GameHostOptions::shaders>, {}},
    Row{"--cache", "DIR", &take_directory<&GameHostOptions::cache>, {}},
    Row{"--overrides", "DIR", &take_directory<&GameHostOptions::overrides>, {}},
    Row{"--asset", "ID", &take_asset, "--cache"},
};

} // namespace

std::span<const OptionRow<GameHostOptions>> game_host_option_rows() noexcept { return kRows; }

GameHostParse parse_game_host_options(std::span<const std::string_view> arguments) {
  return parse_options(game_host_option_rows(), arguments);
}

std::string game_host_usage() { return usage_line("game_host", game_host_option_rows()); }

} // namespace bw::hosts
