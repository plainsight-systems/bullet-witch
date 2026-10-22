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
| [World, scene and simulation](world-and-simulation.md) | What is authoritative state, and how do actors, AI, animation, physics and effects cooperate? |
| [Rendering and presentation](rendering-and-presentation.md) | How do scene/view/material/pass systems produce submitted graphics and media output? |
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

Only the resource/data foundation is implemented: RAT/CPR container inspection,
namespace/dependency decoding and explicit-priority catalog lookup. The inspector
uses the same production library that later engine systems will consume. See
[its contract](../resources.md). The game executable and systems described here
are not created by writing these documents.

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
performs no code migration, renderer construction or backend selection.

The implemented resource/task/Lua/root subset, its target dependencies and current
limits are recorded in [startup runtime](../startup-runtime.md). Architecture
views remain the full target; an original-root test with fixture services does
not establish an interactive title screen or completed gameplay systems.
