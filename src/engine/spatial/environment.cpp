// Execute the scene environment contract in environment.hpp. Region lookup,
// table-key sampling and channel transitions share one scene lifetime. Stage
// adapters define channel meanings and initial values; no global state or
// rendering dependency is used. Prepare all storage before updates, validate
// a complete sampled candidate before publishing it, and retain values on
// failure. Both MCP phases: I.1 (#ri-explicit), MEM.9
// (guidelines/memory/MEM.9-embedded-allocate-at-init.md).
#include "bw/spatial/environment.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace bw::scene {
Environment::Environment(AreaRegions regions,
                         animation::ScalarAnimation animation,
                         std::span<const EnvironmentRegionBinding> bindings,
                         EnvironmentChannels initial,
                         EnvironmentTransition transition,
                         const std::shared_ptr<memory::Domain>& domain)
    : regions_(std::move(regions)),
      animation_(std::move(animation)),
      bindings_(bindings.begin(), bindings.end(),
                memory::Allocator<EnvironmentRegionBinding>(domain)),
      channels_(initial),
      transition_(transition) {
  if (bindings_.empty() || !std::isfinite(transition.target_weight) ||
      !std::isfinite(transition.current_weight) ||
      !std::isfinite(transition.snap_epsilon) || transition.target_weight < 0 ||
      transition.current_weight < 0 ||
      transition.target_weight + transition.current_weight != 1 ||
      transition.snap_epsilon < 0 ||
      !std::ranges::all_of(
          bindings_,
          [](const auto& binding) { return std::isfinite(binding.key); }) ||
      !std::ranges::all_of(
          channels_.values, [](float value) { return std::isfinite(value); })) {
    throw std::invalid_argument("Invalid scene environment definition");
  }
}
void Environment::update(const math::Vector3<float>& point, bool snap) {
  // Negative region identifiers are source sentinels for the default region.
  const auto region =
      std::max<std::int32_t>(0, regions_.containing_id(point).value_or(0));
  const auto found =
      std::ranges::find(bindings_, region, &EnvironmentRegionBinding::region);
  const auto key = (found == bindings_.end() ? bindings_.front() : *found).key;
  const auto target = animation_.sample(key);
  auto candidate = channels_.values;
  for (std::size_t index = 0; index < animation_.channel_count(); ++index) {
    auto value = target.at(index);
    if (!snap && channels_.smooth.at(index)) {
      value = target.at(index) * transition_.target_weight +
              channels_.values.at(index) * transition_.current_weight;
      if (std::abs(value - target.at(index)) < transition_.snap_epsilon) {
        value = target.at(index);
      }
    }
    if (!std::isfinite(value)) {
      throw std::invalid_argument("Scene environment transition overflow");
    }
    candidate.at(index) = value;
  }
  channels_.values = candidate;
}
}  // namespace bw::scene
