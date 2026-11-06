#ifndef BW_HOSTS_GAME_HOST_OPTIONS_HPP
#define BW_HOSTS_GAME_HOST_OPTIONS_HPP

#include "bw/hosts/option_table.hpp"

#include <filesystem>
#include <span>
#include <string>
#include <string_view>

// Responsibility: game_host's command line: its options and the table that
// parses them (bw/hosts/option_table.hpp, whose grammar, errors and usage
// line apply as stated there). A new option is a member of GameHostOptions
// and a row of the table; the parse loop and the usage text do not change.
//
// The rows, in table order:
//   --seconds N       N a finite, non-negative decimal number spelled by the
//                     whole argument; 0, the default, means the host runs
//                     until the window is asked to close
//                     (seconds_limit_reached). Refused when it is not such
//                     a number.
//   --fullscreen      a flag: the window starts in borderless fullscreen.
//   --shaders DIR     the Metal shader artifact directory. Refused when
//                     empty. Absent, shaders stays empty and the host
//                     resolves its default from the executable's location.
//   --cache DIR       the converted-originals cache directory
//                     (content/layers.hpp, the converted layer). Refused
//                     when empty.
//   --overrides DIR   the local overrides directory. Refused when empty.
//                     Absent, the overrides layer is not searched.
//   --asset ID        the mesh the host draws in place of the fixture cube.
//                     Refused when the id does not validate
//                     (content/asset_id.hpp). Needs --cache. Without
//                     --asset the host draws the cube and ignores --cache
//                     and --overrides.
//
// On any error the host prints game_host_usage() and exits 2.
namespace bw::hosts {

struct GameHostOptions {
  double seconds{};
  bool fullscreen{};
  std::filesystem::path shaders{};
  std::filesystem::path cache{};
  std::filesystem::path overrides{};
  // Empty when --asset is absent.
  std::string asset{};
};

using GameHostParse = OptionParse<GameHostOptions>;

// The six rows above. The span views static storage.
[[nodiscard]] std::span<const OptionRow<GameHostOptions>> game_host_option_rows() noexcept;

// parse_options over game_host_option_rows(): the arguments after the
// program name.
[[nodiscard]] GameHostParse parse_game_host_options(std::span<const std::string_view> arguments);

// usage_line("game_host", game_host_option_rows()).
[[nodiscard]] std::string game_host_usage();

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
