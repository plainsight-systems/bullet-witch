// Texture mip construction is a launch-only value: parsing cannot mutate any
// resident owner. Both-phase E.6 (#re-raii), I.11 (#ri-raw) and COPY.5
// (guidelines/copy-move/COPY.5-resource-owning-types-are-move-only.md) keep
// configuration ownership explicit and device resource candidates separate.
// Concept progression selection is an explicit development policy. Saved
// mode inherits the owned handoff; development mode supplies a nonpersistent
// completed-game capability profile at entry. Neither edits permanent saves.
// Design C.9/I.30, implementation E.4 (#re-design-invariants); both-phase MEM.9
// (guidelines/memory/MEM.9-embedded-allocate-at-init.md) bounds entry-only
// work. Capture rejects incompatible developer/view overrides before native
// startup; positive explicit deadlines remain available. Default verification
// is bounded, interactive runs are not. Core I.1 and performance MEM.9
// (references below), read in both phases, keep parsing explicit and out of
// frame work. Parse owned launch configuration before any native window or file
// access. Arguments are borrowed only for this call; invalid/missing numeric
// fields throw runtime_error without publishing a partial result. The optional
// static inspection camera accepts finite XYZ and yaw/pitch degrees, preserves
// the original projection, and never changes the scripted player. Algorithm:
// walk arguments once, validate full numeric fields, normalize yaw and compose
// an orthonormal positive-forward-Z pose. Cost O(argument bytes), with
// path/string storage only during setup. No global configuration or repeated
// frame parsing. Both MCP phases: I.1 (#ri-explicit) exposes the optional
// override; MEM.9 (guidelines/memory/MEM.9-embedded-allocate-at-init.md)
// confines work to setup.
#include <array>
#include <charconv>
#include <cmath>
#include <numbers>
#include <stdexcept>
#include <string_view>

#include "startup_application.hpp"

namespace bw::framework {
StartupOptions parse_startup_options(std::span<char*> arguments) {
  StartupOptions result;
  for (std::size_t index = 1; index < arguments.size(); ++index) {
    const std::string_view argument(arguments[index]);
    if (argument == "--inspect-camera") {
      constexpr std::size_t kCameraFields = 5;
      if (result.inspection_camera ||
          arguments.size() - index <= kCameraFields) {
        throw std::runtime_error(
            "--inspect-camera requires X Y Z YAW PITCH once");
      }
      std::array<float, kCameraFields> values{};
      for (auto& field : values) {
        const std::string_view value(arguments[++index]);
        const auto parsed = std::from_chars(value.begin(), value.end(), field);
        if (parsed.ec != std::errc{} || parsed.ptr != value.end() ||
            !std::isfinite(field)) {
          throw std::runtime_error("--inspect-camera requires finite numbers");
        }
      }
      constexpr float kRightAngle = 90;
      constexpr double kFullTurn = 360, kHalfTurn = 180;
      if (std::abs(values[4]) > kRightAngle) {
        throw std::runtime_error(
            "Inspection pitch must be between -90 and 90 degrees");
      }
      const auto yaw = static_cast<float>(std::remainder(values[3], kFullTurn) *
                                          std::numbers::pi / kHalfTurn);
      const auto pitch =
          static_cast<float>(values[4] * std::numbers::pi / kHalfTurn);
      const float sy = std::sin(yaw), cy = std::cos(yaw);
      const float sp = std::sin(pitch), cp = std::cos(pitch);
      // Positive yaw turns +Z toward +X; positive pitch looks upward.
      result.inspection_camera =
          render::ViewPose{{values[0], values[1], values[2]},
                           {cy, 0, -sy},
                           {-sy * sp, cp, -cy * sp},
                           {sy * cp, sp, cy * cp}};
    } else if (argument == "--concept-profile" &&
               index + 1 < arguments.size()) {
      const std::string_view value(arguments[++index]);
      if (value == "saved") {
        result.concept_profile = game::ConceptProfile::saved;
      } else if (value == "development-completed") {
        result.concept_profile = game::ConceptProfile::development_completed;
      } else {
        throw std::runtime_error(
            "--concept-profile requires saved or development-completed");
      }
    } else if (argument == "--texture-mips" && index + 1 < arguments.size()) {
      const std::string_view value(arguments[++index]);
      if (value != "on" && value != "off") {
        throw std::runtime_error("--texture-mips requires on or off");
      }
      result.texture_mips = value == "on";
    } else if (argument == "--capture") {
      result.capture = true;
    } else if (argument == "--windowed") {
      result.window.fullscreen = false;
    } else if (argument == "--verify-startup") {
      result.verify = true;
    } else if ((argument == "--game" || argument == "--profile" ||
                argument == "--max-seconds" || argument == "--window-width" ||
                argument == "--window-height") &&
               index + 1 < arguments.size()) {
      const std::string_view value(arguments[++index]);
      if (argument == "--game") {
        result.game = value;
      } else if (argument == "--profile") {
        result.profile = value;
      } else {
        int parsed_value{};
        const auto parsed =
            std::from_chars(value.begin(), value.end(), parsed_value);
        if (parsed.ec != std::errc{} || parsed.ptr != value.end() ||
            parsed_value <= 0) {
          throw std::runtime_error(std::string(argument) +
                                   " requires a positive integer");
        }
        if (argument == "--max-seconds") {
          result.maximum_seconds = parsed_value;
        } else if (argument == "--window-width") {
          result.window.width = static_cast<std::uint32_t>(parsed_value);
        } else {
          result.window.height = static_cast<std::uint32_t>(parsed_value);
        }
      }
    } else {
      throw std::runtime_error(
          "Usage: bw-startup --game GAME_DIRECTORY [--profile SAVE_DIRECTORY] "
          "[--verify-startup] [--capture] [--texture-mips on|off] "
          "[--concept-profile saved|development-completed] "
          "[--max-seconds N] [--windowed] [--window-width N] [--window-height "
          "N] [--inspect-camera X Y Z YAW PITCH]");
    }
  }
  if (result.game.empty()) {
    throw std::runtime_error("--game GAME_DIRECTORY is required");
  }
  if (result.capture && (!result.window.fullscreen || result.verify ||
                         result.inspection_camera)) {
    throw std::runtime_error(
        "--capture cannot combine with --windowed, --verify-startup or "
        "--inspect-camera");
  }
  if (result.verify && !result.maximum_seconds) {
    result.maximum_seconds = StartupOptions::kVerificationMaximumSeconds;
  }
  return result;
}

}  // namespace bw::framework
