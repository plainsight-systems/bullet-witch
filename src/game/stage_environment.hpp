// Original stage environment compatibility schema. Decode the retained AREA,
// STBL and FLAN publications into a scene Environment owner; translate source
// channel indices into typed authored lighting/fog/sky/output values. The
// engine owns region queries, sampling and transition lifetime. This adapter
// owns no clock, controller or renderer and applies no visual tuning.
// Definitions are borrowed only during construction; the result owns all
// runtime data. Missing partial inputs or malformed table bindings reject. No
// update-time allocation. Source channel map:
// lighting-scene-producer-contract.md. Both MCP phases: Core I.1 (#ri-explicit)
// exposes compatibility conversion; performance MEM.9
// (guidelines/memory/MEM.9-embedded-allocate-at-init.md) retains prepared
// channels rather than decoding assets during rendering.
#pragma once
#include "bw/content/stage.hpp"
#include "bw/scene/environment.hpp"
namespace bw::game {
std::optional<scene::Environment> prepare_stage_environment(
    const content::StageDefinition& stage,
    const std::shared_ptr<memory::Domain>& domain);
formats::StageEnvironment stage_environment_snapshot(
    const formats::StageEnvironment& authored,
    const scene::Environment& environment);
}  // namespace bw::game
