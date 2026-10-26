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
#include "bw/content/formats/background_placement.hpp"
#include "bw/content/formats/stage_definitions.hpp"
#include "bw/lighting/local_lights.hpp"
#include "bw/math/vector.hpp"
#include "game/content/original/resident_model.hpp"

namespace bw::game {
std::optional<scene::LocalLight> prepare_placed_light(
    const formats::PhysicsCollisionDefinition& definition,
    std::span<const formats::PlacementMatrix> body_world,
    const formats::PlacementMatrix* object_world, std::size_t joint_count);
// Resolve the original placed-model lighting observation from control-point
// slot zero. Read only that slot: unrelated frames need not have valid poses.
// Missing resource/slot or source signed -1 influence returns the owner's first
// placement translation. Otherwise evaluate its first authored point through
// PHIT-joint-mapped direct world matrices, preserving weights without inverse
// bind, normalization or an extra placement transform. Zero influences retain
// the authored point because the original direct-world core matrix is identity.
// Matrices and body association must be nonempty/equal-sized; consumed values
// must be finite. Malformed selected records throw resources::Error; unresolved
// nonnegative joint poses reject explicitly rather than fabricate a fallback.
// Borrowed inputs last only for the call; result owns XYZ. No allocation or
// shared state, O(selected influences * bodies), intended at scene preparation.
// See placed-lighting-control-point.md. Both MCP phases: I.1 (#ri-explicit)
// separates control identity from skeleton identity, SL.con.3 (#rsl-bounds)
// bounds selected data, MEM.9
// (guidelines/memory/MEM.9-embedded-allocate-at-init.md) keeps query scratch
// fixed.
math::Vector3<float> placed_receiver_position(
    const ResidentModelView& model,
    std::span<const formats::PlacementMatrix> matrices);
}  // namespace bw::game
