// Compose effective source shadow fit settings when a stage is activated.
// Raw authored fields remain immutable; the runtime initializer supplies the
// recovered warp angle and depth multiplier before observer publication.
// Inputs are borrowed for the call, output is a fixed owned value. No dynamic
// storage, shared state or geometry traversal. Both phases: I.1 (#ri-explicit)
// exposes authored versus runtime policy; MEM.9 (guidelines/memory/
// MEM.9-embedded-allocate-at-init.md) keeps initialization scalar-only.
#pragma once
#include "bw/render/shadows/shadow.hpp"
#include "game/content/original/stage_environment_settings.hpp"

namespace bw::game {
render::ShadowWarpSettings initial_stage_shadow_settings(
    const formats::StageShadow& authored);
}  // namespace bw::game
