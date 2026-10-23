// Check the stage activation policy independently of raw resource decoding.
// Both phases: I.1 (#ri-explicit) distinguishes effective settings; MEM.9
// (guidelines/memory/MEM.9-embedded-allocate-at-init.md) uses fixed values.
#include "game/stage_shadow.hpp"

#include <cmath>
#include <numbers>
#include <stdexcept>

namespace {
void require(bool value, const char* message) {
  if (!value) throw std::runtime_error(message);
}
}  // namespace
void stage_shadow_tests() {
  bw::formats::StageShadow authored;
  authored.far_distance = 750;
  authored.minimum_height = -5;
  authored.maximum_height = 80;
  authored.alignment_threshold = .8F;
  authored.parallel_near_extension = 90;
  authored.receiver_radius = .058F;
  const auto effective = bw::game::initial_stage_shadow_settings(authored);
  require(effective.far_distance == 750 && effective.minimum_height == -5 &&
              effective.maximum_height == 80 &&
              effective.alignment_threshold == .8F &&
              effective.parallel_near_extension == 90 &&
              std::abs(effective.perspective_angle_radians -
                       std::numbers::pi_v<float> / 3) < .00001F &&
              effective.depth_scale == 2 &&
              effective.filter_radius_uv == .058F / 1024 &&
              !authored.setup_enabled,
          "Activation must compose source defaults without mutating raw data");
}
