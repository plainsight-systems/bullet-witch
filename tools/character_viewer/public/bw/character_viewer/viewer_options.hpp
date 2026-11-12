#ifndef BW_CHARACTER_VIEWER_VIEWER_OPTIONS_HPP
#define BW_CHARACTER_VIEWER_VIEWER_OPTIONS_HPP

#include "bw/hosts/option_table.hpp"

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// Responsibility: character_viewer's command line: its options and the
// table that parses them (bw/hosts/option_table.hpp, whose grammar, errors
// and usage line apply as stated there), in the form of
// bw/hosts/game_host_options.hpp. A new option is a member of ViewerOptions
// and a row of the table.
//
// The rows, in table order:
//   --seconds N       as game_host's: a finite, non-negative decimal; 0,
//                     the default, runs until Escape or the window's close.
//   --fullscreen      a flag: the window starts in borderless fullscreen.
//   --shaders DIR     the Metal shader artifact directory; refused when
//                     empty; absent, resolved from the executable's
//                     location as game_host does.
//   --cache DIR       the converted-originals cache; refused when empty.
//   --overrides DIR   the local overrides directory; refused when empty.
//   --size WxH        the window's content area in points: two decimal
//                     integers joined by one 'x', both positive and at most
//                     kMaxWindowPoints; refused otherwise. Default 1280x720.
//   --asset ID        a mesh id to draw (content/asset_id.hpp); refused
//                     when it does not validate. Repeatable: each
//                     occurrence appends to `assets` in argument order.
//                     Needs --cache.
//   --dump FILE       the PNG the first frame and each press of P are
//                     written to; refused when empty. Needs --asset.
//   --script FILE     the input script (bw/character_viewer/script.hpp)
//                     applied one command per frame; refused when empty.
//                     Needs --asset.
//   --grid N          every asset is drawn as N by N copies
//                     (bw/rendering/model_scene.hpp, contract 1e): a
//                     decimal integer from 1 to kMaxViewerGrid; refused
//                     otherwise. Default 1. Needs --asset.
// The executable requires at least one --asset; that is its check, after
// the parse, with the usage line and exit 2 like a table error, since a
// table row cannot state "at least once".
//
// Core guidelines read: I.1, Con.5 (a parse is a pure function of rows and
// arguments), E.27, F.20, ES.3 (the rows share game_host's loop).
namespace bw::character_viewer {

inline constexpr std::uint32_t kMaxWindowPoints = 16384;
// rendering::kMaxModelGrid.
inline constexpr std::uint32_t kMaxViewerGrid = 64;

struct ViewerOptions {
  double seconds{};
  bool fullscreen{};
  std::filesystem::path shaders{};
  std::filesystem::path cache{};
  std::filesystem::path overrides{};
  std::uint32_t width{1280};
  std::uint32_t height{720};
  // In argument order; empty when --asset is absent.
  std::vector<std::string> assets{};
  // Empty when --dump is absent.
  std::filesystem::path dump{};
  // Empty when --script is absent.
  std::filesystem::path script{};
  std::uint32_t grid{1};
};

using ViewerParse = hosts::OptionParse<ViewerOptions>;

// The ten rows above. The span views static storage.
[[nodiscard]] std::span<const hosts::OptionRow<ViewerOptions>> viewer_option_rows() noexcept;

// parse_options over viewer_option_rows(): the arguments after the program
// name.
[[nodiscard]] ViewerParse parse_viewer_options(std::span<const std::string_view> arguments);

// usage_line("character_viewer", viewer_option_rows()).
[[nodiscard]] std::string viewer_usage();

} // namespace bw::character_viewer

#endif
