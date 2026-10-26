// Scene-owned spatial environment channels. Construction takes prepared region
// and scalar-animation owners, copies region-to-key bindings and retains an
// explicit initial channel snapshot. No game channel meanings live here.
// Update selects the first containing region, resolves its authored binding
// (negative/missing identifiers select zero; absent bindings use the first),
// samples once, then applies per-channel immediate or
// weighted transitions. The key is a table coordinate, not a running clock.
// One call is one owner update; callers serialize updates and publication.
// Explicit snap bypasses blending; normal updates snap only within epsilon.
// Invalid bindings/policies/initial values reject before publication. Updates
// allocate nothing; cost O(regions + bindings + log keys + channels).
// Source algorithm: lighting-scene-producer-contract.md. Both MCP phases:
// Core I.1 (#ri-explicit) exposes transition policy and initial state;
// performance MEM.9 (guidelines/memory/MEM.9-embedded-allocate-at-init.md)
// prepares storage.
#pragma once
#include "area_regions.hpp"
#include "bw/animation/scalar_animation.hpp"

namespace bw::scene {
struct EnvironmentRegionBinding {
  std::int32_t region{};
  float key{};
};
struct EnvironmentTransition {
  float target_weight{}, current_weight{}, snap_epsilon{};
};
struct EnvironmentChannels {
  animation::ScalarAnimationSample values{};
  std::array<bool, animation::kMaximumScalarChannels> smooth{};
};
class Environment {
 public:
  Environment(AreaRegions regions, animation::ScalarAnimation animation,
              std::span<const EnvironmentRegionBinding> bindings,
              EnvironmentChannels initial, EnvironmentTransition transition,
              const std::shared_ptr<memory::Domain>& domain);
  void update(const math::Vector3<float>& point, bool snap = false);
  const auto& values() const { return channels_.values; }
  std::uint32_t version() const { return animation_.version(); }

 private:
  AreaRegions regions_;
  animation::ScalarAnimation animation_;
  memory::Vector<EnvironmentRegionBinding> bindings_;
  EnvironmentChannels channels_;
  EnvironmentTransition transition_;
};
}  // namespace bw::scene
