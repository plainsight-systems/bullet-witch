# Bullet Witch engine architecture

**Target architecture · working structural baseline · October 22, 2026.**

Start with the [engine architecture](logical-overview.md). It describes the
finished runtime: application and engine lifetimes, session/mission/world owners,
entity and scene structure, simulation, rendering, resources and host boundaries.
The design is organized around that engine, independently of reconstruction order.

This structure is the working starting point for implementation. Detailed
behavioral and backend contracts remain open, and the baseline does not claim
that the engine has been implemented. It uses the recovered object/component/message mechanisms
and preserves their behavioral constraints without asserting that the proposed
owner names are Cavia's original source classes.

## Read in this order

| View | Architectural question |
|---|---|
| [Engine architecture](logical-overview.md) | What are the engine's structural units and their boundaries? |
| [Runtime objects and ownership](runtime-model.md) | What exists at runtime, who owns it, and what survives a transition? |
| [Player progression and mission state](player-progression.md) | Who owns permanent levels, effective mission abilities and save adaptation? |
| [World, scene and simulation](world-and-simulation.md) | What is authoritative state, and how do actors, AI, animation, physics and effects cooperate? |
| [World timing and tick admission](world-timing.md) | Who publishes simulation time, selects paused work and admits current gameplay input? |
| [Actors, poses and animation](actors-and-animation.md) | How do game movement, clip evaluation, skinning and weapon attachment share one actor transform? |
| [Rendering and presentation](rendering-and-presentation.md) | How do scene/view/material/pass systems produce submitted graphics and media output? |
| [Scene lighting](lighting.md) | Who owns authored fields and emitters, prepares view/receiver lighting, and publishes values to the renderer? |
| [World renderer reconstruction](world-renderer-reconstruction.md) | How does the native world renderer preserve the recovered design before measured optimization and graphics upgrades? |
| [Movie playback redesign](../video-player-redesign.md) | How do independent media streams, consumption clocks and presentation replace the failed prototype? |
| [Shared math](shared-math.md) | Where do reusable CPU calculations live, and how are precision and target implementations separated? |
| [Memory and residency](memory-and-residency.md) | How are allocations routed, budgeted, retained and reclaimed across CPU/GPU owners? |
| [Execution, lifetime and memory contracts](execution-contracts.md) | Who can mutate/read state, how does work cross domains, and when can memory be reused? |
| [Runtime scenarios](runtime-scenarios.md) | How do startup, mission execution, saves and shutdown traverse those systems? |
| [Change axes](change-axes.md) | Which responsibilities change independently, and why? |
| [Source ownership and dependencies](file-mapping.md) | Where do those contracts belong, and which dependencies are allowed? |

The logical architecture precedes source placement. Runtime ownership, source
dependency and execution order are different views; every diagram states which
relationship its arrows represent. A planned source directory is not an engine
system by itself.

## Evidence and design status

| Label | Meaning |
|---|---|
| **Recovered** | Supported at the stated scope by the original-engine map, instructions or corpus |
| **Proposed** | A reconstruction structure, ownership boundary or interface choice; not an original-class or implementation claim |
| **Unresolved** | A specific behavioral, numerical or implementation contract still lacks sufficient evidence/decision |
| **Implemented** | Present in source, with assurance stated separately from design or recovery |

All new runtime aggregate names, scene projections and subsystem partitions are
proposed. Recovered local orders constrain them. Camera equations, complete
visibility/lighting/pass behavior, all-mode cadence and some worker completion
edges remain unresolved; their owners and interfaces are now explicit. There is
no claim that naming them closes their implementation contracts.

The [original-engine map](../../research/engine/ARCHITECTURE.md) owns recovered
facts and research evidence. The [brief](../../BRIEF.md) owns project direction:
source reconstruction, faithful direct port, then improvements. The
[implementation plan](../../IMPLEMENTATION.md) owns delivery order and
[SRC-01 migration](../../IMPLEMENTATION.md#migrating-src-01). No migration sequence
or milestone growth diagram determines the target runtime architecture.

The implemented subset includes resource/data access, task lifecycle, Lua and
root execution, concrete logo/check/advertise/title session tasks, profile loading,
interactive menus, a validated gameplay handoff, shared movie timing/decoding,
and a Mac host using native Metal presentation and AudioQueue output. The
[startup contract](../startup-runtime.md), [movie player](../video-player.md),
[movie decoder](../video-decoder.md) and [native host](../native-startup-host.md) state their verification boundaries.
Original title/menu sprites, font text, authored costume previews and input
outcomes are implemented. `FrontEnd` owns profile-load and menu policies while
borrowing profile, writable storage, media/fades and presentation. The original
Lua root produces an owned `GameplayRequest`. One Shot Kill consumes that request
into mission-owned resources, stage rendering and bounded Alicia traversal with
root-driven animation, static contact and a live camera. Full mission start,
combat and dynamic world behavior remain unimplemented; see
[actors and animation](actors-and-animation.md).

The current menu options are volumes and costume selection. Volumes persist but
do not yet control audio gain; menu background music and cues are absent.
Campaign Stage Select and Chaos/Hell difficulty remain achievement-gated on a
fresh profile because no Steam achievement provider is connected. Concept Mission
now bypasses its game-completion gate by explicit user decision; this is an
intentional menu-policy departure, not fabricated completion or achievement state.
Extra-mission native status 5 versus Lua status 6 and selected-concept resource
routing remain unresolved. These limits are distinct from the verified new-game
handoff and costume save/reload path. See the
[front-end contract](../startup-runtime.md#profile-menu-and-gameplay-handoff).

The first presentation slice implements sample ownership, fade progression,
screen retirement and conservative Mac GPU reuse. The independent media pipeline
now completes both company movies on the Mac with exact PCM consumption and no
underruns in the recorded run. It does not settle the
world renderer's pass graph or asynchronous submission policy. Native shaders
are separate compiled source assets; backend-specific tuning stays behind
portable contracts.

## Review focus

- Are application, engine, session, mission, world and subsystem ownership clear,
  including shared definitions and submitted work that outlives an instance?
- Does the world model preserve recovered entity/component/message behavior while
  exposing explicit simulation, scene, query and presentation interfaces?
- Is rendering defined through scene/view/visibility/material/pass/submission
  responsibilities, with a distinct device boundary and honest evidence gaps?
- Does each mutable state have an authority, a valid observation phase and a
  release/completion rule across threads and devices?
- Do game policy, portable engine mechanisms and selected host implementations
  have enforceable dependency boundaries?
- Are unresolved contracts assigned to their owning systems rather than hidden
  behind generic update methods or replaced with plausible modern behavior?

Engineering practice follows the adapted [workflow](../process/workflow.md).
Implementation refines this baseline through explicit file contracts and evidence.
The next source slice is recorded in the implementation plan; this baseline
describes implemented contracts alongside the remaining target structure.

The implemented resource/task/Lua/root subset, its target dependencies and current
limits are recorded in [startup runtime](../startup-runtime.md). Architecture
views remain the full target. The original-root corpus now exercises real profile
I/O and costume save/reload with a synthetic renderer; native manual verification
separately observed costume selection and the gameplay request. Neither proves
original-game parity or completed gameplay systems.
