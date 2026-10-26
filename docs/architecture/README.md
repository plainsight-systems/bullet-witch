# Bullet Witch engine architecture

**Finalized architecture design · October 26, 2026.** Start with the
[reorganization plan](reorganization-plan.md): settled boundaries, destination
map and four implementation slices. The [proposed system, lifetime and frame
boundaries](proposed-boundaries.md) provide the visual design. These are design
artifacts; production execution has not started. [Current systems and
ownership](current-ownership.md) distinguishes the accepted implementation.
The [runtime subsystem inventory](../../research/engine/subsystems/runtime-subsystem-inventory.md)
provides concrete files/build targets and implemented, partial or planned status.

The broader [target architecture](logical-overview.md) describes the finished
game/engine structure. It is not an implementation inventory. In particular,
`EngineInstance`, a complete mission `World`, entity registry, generic render-scene
extraction and a general job system are not all present as runtime owners today.
Proposed names describe responsibilities, not recovered Cavia C++ class names.

## Current review

The frontend, gameplay pause and HUD have user acceptance. Main-menu music/cues,
live audio gain, original shared Options/controller/calibration children, costume
persistence and reachable Power Up are implemented. Original Lua still orchestrates
native tasks. Concept status/resource routing is resolved; it is not an open
status-5-versus-6 question. See [frontend verification](../../research/engine/subsystems/frontend-verification.md)
and [mission launch](../../research/engine/subsystems/mission-launch.md).

The current One Shot Kill scene has stage/actor rendering, lighting, custom static
contact, Alicia motion/actions and camera. Authored full mission/entity startup,
dynamic physics, NPC AI, live effects, mission-result production and complete
world checkpoint restoration remain unfinished. Steam rankings use the original
unavailable-service route; no achievement import provider is implemented.

The agreed design separates game-world behavior from retained render-scene
membership, keeps scene lighting identifiable, and composes world/movie/UI as
sibling outputs. Engine families remain peers; game files group by responsibility.
Original-install admission and future enhanced asset/script selection are policy
above ordered resource resolution; mission activation remains distinct from
residency. The plan preserves current algorithms and native acceptance. It adds
no gameplay, physics, AI, asset upgrades or debug implementation.

## Read in this order

| View | Question |
| --- | --- |
| [Finalized reorganization plan](reorganization-plan.md) | What moves, which ownership contracts change, and in what order? |
| [Complete proposed architecture boundaries](proposed-boundaries.md) | What are the recommended engine/game/content/native boundaries, lifetimes and frame flow? |
| [Current systems and proposed ownership repairs](current-ownership.md) | What exists now, what is coupled, and what should change before moving files? |
| [Runtime subsystem inventory](../../research/engine/subsystems/runtime-subsystem-inventory.md) | Which source/build owners are implemented, partial or planned? |
| [Target engine architecture](logical-overview.md) | What should the completed engine/game structure support? |
| [Runtime ownership](runtime-model.md) | What survives each transition? Target lifetime model, clearly separate from current composition. |
| [World timing](world-timing.md) | Who owns pacing, presentation/world time, pause admission and current input? |
| [Rendering and presentation](rendering-and-presentation.md), [lighting](lighting.md) | How do scene observations, light evaluation, passes and native output relate? |
| [Actors and animation](actors-and-animation.md), [gameplay](player-gameplay.md) | Where are actor/action state, resources, pose publication and HUD boundaries? |
| [Progression](player-progression.md), [world and simulation](world-and-simulation.md) | What is permanent, mission-effective or future world/entity state? |
| [Memory and residency](memory-and-residency.md) | Who retains allocations/assets, and what is current versus future replacement-pack resolution? |
| [Execution contracts](execution-contracts.md), [shared math](shared-math.md) | Which mutation, completion, numerical and reuse rules must hold? |
| [Source mapping](file-mapping.md), [change axes](change-axes.md) | How should established owners constrain later directory and dependency changes? |
| [Runtime scenarios](runtime-scenarios.md), [renderer reconstruction](world-renderer-reconstruction.md) | What recovered behavior constrains implementation? |

**Implemented** means code exists at the stated scope; acceptance and test evidence
remain separate. **Recovered** means supported by original source/data evidence.
**Proposed** means a reviewable reconstruction design. **Unresolved/planned** means
a missing decision or runtime consumer, not an inert implementation to count as
done. Current and proposed diagrams are explicitly labelled; arrows describe
ownership, dependency or data/order as stated, never implicit worker threads.

The [original-engine map](../../research/engine/ARCHITECTURE.md) owns recovered
facts; the [brief](../../BRIEF.md) retains the faithful-port-then-improvements
sequence. Existing pinned Unreal references guide ownership where needed without
importing its framework, source code or directory layout. The accepted
[integrated game/engine organization](../../research/engine/subsystems/source-organization.md)
and singular portable contracts/native implementations remain the baseline.
