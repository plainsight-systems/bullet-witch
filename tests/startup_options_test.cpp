// Launch configuration is exercised in process without native windows or game
// files. Verify default/authored camera selection, explicit orientation and
// rejection before startup. Fixtures own argument strings through parsing;
// returned configuration retains no argument views. Cost is bounded fixture
// storage only. Both MCP phases: I.1 (#ri-explicit) tests the public interface;
// MEM.9 (guidelines/memory/MEM.9-embedded-allocate-at-init.md) keeps
// configuration work in setup. These are cases in the existing runtime
// executable.
#include <cmath>
#include <initializer_list>
#include <stdexcept>
#include <string>
#include <vector>

#include "framework/startup_application.hpp"

namespace {
bw::framework::StartupOptions parse(std::initializer_list<const char*> input) {
  std::vector<std::string> storage;
  storage.reserve(input.size());
  for (const auto* argument : input) storage.emplace_back(argument);
  std::vector<char*> arguments;
  arguments.reserve(storage.size());
  for (auto& argument : storage) arguments.push_back(argument.data());
  return bw::framework::parse_startup_options(arguments);
}
void require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}
void reject(std::initializer_list<const char*> input) {
  bool rejected = false;
  try {
    (void)parse(input);
  } catch (const std::runtime_error&) {
    rejected = true;
  }
  require(rejected, "Invalid inspection options accepted");
}
}  // namespace

void startup_options_tests() {
  const auto normal = parse({"host", "--game", "owned-game"});
  require(!normal.inspection_camera && normal.window.fullscreen,
          "Normal launch must retain the source camera and fullscreen");
  require(!normal.maximum_seconds && !normal.capture,
          "Interactive launches must not have an implicit timeout");
  require(
      normal.concept_profile == bw::game::ConceptProfile::development_completed,
      "Early concept access must declare its development profile");
  const auto saved =
      parse({"host", "--game", "owned-game", "--concept-profile", "saved"});
  require(saved.concept_profile == bw::game::ConceptProfile::saved,
          "Saved progression entry must be selectable");
  reject({"host", "--game", "owned-game", "--concept-profile", "all-max"});
  reject({"host", "--game", "owned-game", "--concept-profile"});
  const auto capture = parse({"host", "--game", "owned-game", "--capture"});
  require(capture.capture && capture.window.fullscreen &&
              !capture.maximum_seconds && !capture.inspection_camera &&
              !capture.verify,
          "Capture must preserve ordinary fullscreen startup");
  const auto bounded = parse(
      {"host", "--game", "owned-game", "--capture", "--max-seconds", "15"});
  require(bounded.maximum_seconds == 15,
          "Explicit capture deadline was not retained");
  const auto verification =
      parse({"host", "--game", "owned-game", "--verify-startup"});
  require(verification.maximum_seconds ==
              bw::framework::StartupOptions::kVerificationMaximumSeconds,
          "Verification must retain its bounded default");
  const auto explicit_verification =
      parse({"host", "--game", "owned-game", "--verify-startup",
             "--max-seconds", "20"});
  require(explicit_verification.maximum_seconds == 20,
          "Explicit verification deadline was not retained");
  reject({"host", "--game", "owned-game", "--capture", "--windowed"});
  reject({"host", "--game", "owned-game", "--windowed", "--capture"});
  reject({"host", "--game", "owned-game", "--capture", "--verify-startup"});
  reject({"host", "--game", "owned-game", "--capture", "--inspect-camera", "0",
          "0", "0", "0", "0"});
  reject({"host", "--game", "owned-game", "--max-seconds", "0"});
  reject({"host", "--game", "owned-game", "--max-seconds", "-1"});
  reject({"host", "--game", "owned-game", "--max-seconds"});
  const auto explicit_view =
      parse({"host", "--game", "owned-game", "--windowed", "--inspect-camera",
             "1", "2", "-3", "450", "30"});
  require(explicit_view.inspection_camera && !explicit_view.window.fullscreen,
          "Explicit inspection camera was not retained");
  if (!explicit_view.inspection_camera) {
    throw std::runtime_error("Missing explicit camera");
  }
  const auto& pose = *explicit_view.inspection_camera;
  require(pose.eye == std::array<float, 3>{1, 2, -3} &&
              std::abs(pose.forward[0] - std::sqrt(0.75F)) < 0.00001F &&
              std::abs(pose.forward[1] - 0.5F) < 0.00001F &&
              std::abs(pose.forward[2]) < 0.00001F,
          "Inspection yaw/pitch convention differs from the CLI contract");
  // The renderer independently validates orthonormality and projection.
  (void)bw::render::make_view_transforms(pose, {0.7F, 16.0F / 9, 0.01F, 1000});
  reject({"host", "--game", "owned-game", "--inspect-camera", "1", "2"});
  reject({"host", "--game", "owned-game", "--inspect-camera", "nan", "0", "0",
          "0", "0"});
  reject({"host", "--game", "owned-game", "--inspect-camera", "1x", "0", "0",
          "0", "0"});
  reject({"host", "--game", "owned-game", "--inspect-camera", "0", "0", "0",
          "0", "91"});
  reject({"host", "--game", "owned-game", "--inspect-camera", "0", "0", "0",
          "0", "0", "--inspect-camera", "0", "0", "0", "0", "0"});
}
