// Resolve an authored background emitter against the current body-world
// palette. Definition and matrices are borrowed only for the call; the result
// owns all scalar data. The object transform is the original fallback for an
// invalid or unavailable joint. A partial palette, ambiguous mapping or
// nonfinite pose fails before publication. Algorithm: inspect optional decoded
// emitter, locate its body through the retained joint map, copy source
// colors/attenuation and world translation. No inverse bind or extra placement
// multiplication applies. O(body count), no allocation. The scene owner retains
// the result and updates it when the source body moves. Both MCP phases I.1
// (ri-explicit) make the fallback/lifetime explicit; MEM.9 (guidelines/memory/
// MEM.9-embedded-allocate-at-init.md) keeps preparation allocation-free.
// Source contract: research/engine/subsystems/placed-local-lights.md.
#pragma once
#include "bw/formats/background_placement.hpp"
#include "bw/formats/stage_definitions.hpp"
#include "bw/scene/lighting/local_lights.hpp"

namespace bw::game {
std::optional<scene::LocalLight> prepare_placed_light(
    const formats::PhysicsCollisionDefinition& definition,
    std::span<const formats::PlacementMatrix> body_world,
    const formats::PlacementMatrix* object_world, std::size_t joint_count);
}  // namespace bw::game
