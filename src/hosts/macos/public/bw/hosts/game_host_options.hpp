#ifndef BW_HOSTS_GAME_HOST_OPTIONS_HPP
#define BW_HOSTS_GAME_HOST_OPTIONS_HPP

#include "bw/hosts/option_table.hpp"

#include <cstdint>
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
//   --fixed-step S    S a finite decimal number above 0, spelled by the
//                     whole argument: every tick's step is S seconds,
//                     whatever the clock reads (bw/runtime/step.hpp).
//                     Absent, the step is measured and held to
//                     kLongestStep. Needs --asset.
//   --script FILE     a scripted device
//                     (bw/platform/input/scripted_device.hpp). Refused
//                     when empty. Needs --fixed-step, and through it
//                     --asset.
//   --dump DIR        the directory the marks of a script are written
//                     to. Refused when empty. Needs --script.
//   --action-log      a flag: one stdout line per action event. Needs
//                     --asset.
//
// A scripted run (--script). The host parses FILE before the first tick; a
// parse error, or a file it cannot read, is a named failure with the
// line. It prints `scripted_device script=<FILE>` first. The window's and
// the pad's own samples are not read and the cursor is not captured, so
// the run is a function of the script and the step alone. Before tick N
// the script's samples of tick N are handed to key state; after the
// script's last tick is drawn the run ends, exit 0. A scripted run that
// the window's close request or --seconds ends before the script's last
// tick is a named failure, exit 1. For each mark of a
// tick the host prints, after that tick's frame is drawn, one line:
//   mark=<name> tick=<N> time=<world seconds> pose=<x>,<y>,<z> yaw=<deg>
//   pitch=<deg> fov=<vertical deg> cut=<0|1> rigs=<rig>:<weight>[,<rig>:<weight>]
//   actions=<action>[,<action>]
// on one line: the published camera that tick's frame was drawn with, its
// `cut` read before the host clears the flag, the main stack bottom
// first, and the actions that fired in that tick, in load order (empty
// after `actions=` when none). Each number is the shortest decimal of up
// to nine significant digits that reads back as the float. With --dump
// the frame of that tick is also written to <DIR>/<name>.png, 8-bit RGBA
// of the back buffer, and the line ends with ` dump=<file>`; a file that
// cannot be written is a named failure.
//
// The action log (--action-log), scripted or not: after each tick, for
// each action with an event in that tick, in load order,
//   action tick=<N> name=<action> events=<Started|Fired|Completed joined
//   by '+'> value=<x>,<y>,<z>
// A Fired with no Started or Completed beside it is printed only when
// the action's value differs from the tick before's, so a held button is
// two lines and a moving stick one a tick.
//
// With --asset the host runs the world's tick: her object stands
// alicia::kBodyBelowPlace above the world's origin and the model is drawn
// that far below it, feet on the origin, as a grid of kModelGrid copies a
// side, the player's
// controller possesses the model's object, and the view is the player's
// camera (bw/game/characters/alicia/view.hpp). Without --script the host
// reads the window's device samples and the pad, and captures the cursor
// while the window is key.
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
  // 0 when --fixed-step is absent.
  double fixed_step{};
  // Empty when absent.
  std::filesystem::path script{};
  std::filesystem::path dump{};
  bool action_log{};
};

// The longest step of a measured run, seconds: the shipped game holds its
// frame time to this.
inline constexpr double kLongestStep = 0.1;
// Copies of the model a side, so the view has fixed things in it besides
// her.
inline constexpr std::uint32_t kModelGrid = 3;

using GameHostParse = OptionParse<GameHostOptions>;

// The ten rows above. The span views static storage.
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
