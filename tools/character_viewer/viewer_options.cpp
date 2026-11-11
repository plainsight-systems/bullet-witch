// character_viewer's command line
// (bw/character_viewer/viewer_options.hpp): the nine rows and the functions
// that take their values, with no environment, file system or global read.
#include "bw/character_viewer/viewer_options.hpp"

#include "bw/content/asset_id.hpp"
#include "bw/hosts/option_table.hpp"

#include <array>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <system_error>

namespace bw::character_viewer {
namespace {

using Row = hosts::OptionRow<ViewerOptions>;

std::string_view take_seconds(ViewerOptions& options, std::string_view text) {
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

std::string_view take_fullscreen(ViewerOptions& options, std::string_view /*value*/) {
  options.fullscreen = true;
  return {};
}

template <std::filesystem::path ViewerOptions::* Member>
std::string_view take_directory(ViewerOptions& options, std::string_view value) {
  if (value.empty()) {
    return "is an empty directory";
  }
  options.*Member = std::filesystem::path(value);
  return {};
}

// One side of WxH: a decimal integer that is the whole text, in [1,
// kMaxWindowPoints]; 0 when it is not.
std::uint32_t window_side(std::string_view text) {
  std::uint32_t value = 0;
  const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
  if (text.empty() || parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() ||
      value > kMaxWindowPoints) {
    return 0;
  }
  return value;
}

// The refusal names the ceiling in words, so it follows the constant.
static_assert(kMaxWindowPoints == 16384);

std::string_view take_size(ViewerOptions& options, std::string_view value) {
  const std::size_t x = value.find('x');
  if (x == std::string_view::npos) {
    return "is not WxH";
  }
  const std::uint32_t width = window_side(value.substr(0, x));
  const std::uint32_t height = window_side(value.substr(x + 1U));
  if (width == 0U || height == 0U) {
    return "is not WxH with both sides from 1 to 16384 points";
  }
  options.width = width;
  options.height = height;
  return {};
}

// The empty value is an id that does not validate (asset_id.hpp, Empty).
std::string_view take_asset(ViewerOptions& options, std::string_view value) {
  if (bw::content::validate_asset_id(value) != bw::content::AssetIdError::None) {
    return "is not an asset id";
  }
  options.assets.emplace_back(value);
  return {};
}

std::string_view take_dump(ViewerOptions& options, std::string_view value) {
  if (value.empty()) {
    return "is an empty file name";
  }
  options.dump = std::filesystem::path(value);
  return {};
}

std::string_view take_script(ViewerOptions& options, std::string_view value) {
  if (value.empty()) {
    return "is an empty file name";
  }
  options.script = std::filesystem::path(value);
  return {};
}

constexpr std::array kRows{
    Row{"--seconds", "N", &take_seconds, {}, false},
    Row{"--fullscreen", {}, &take_fullscreen, {}, false},
    Row{"--shaders", "DIR", &take_directory<&ViewerOptions::shaders>, {}, false},
    Row{"--cache", "DIR", &take_directory<&ViewerOptions::cache>, {}, false},
    Row{"--overrides", "DIR", &take_directory<&ViewerOptions::overrides>, {}, false},
    Row{"--size", "WxH", &take_size, {}, false},
    Row{"--asset", "ID", &take_asset, "--cache", true},
    Row{"--dump", "FILE", &take_dump, "--asset", false},
    Row{"--script", "FILE", &take_script, "--asset", false},
};

} // namespace

std::span<const hosts::OptionRow<ViewerOptions>> viewer_option_rows() noexcept { return kRows; }

ViewerParse parse_viewer_options(std::span<const std::string_view> arguments) {
  return hosts::parse_options(viewer_option_rows(), arguments);
}

std::string viewer_usage() { return hosts::usage_line("character_viewer", viewer_option_rows()); }

} // namespace bw::character_viewer
