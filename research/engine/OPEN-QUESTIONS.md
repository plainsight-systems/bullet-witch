# Open research questions

Updated October 22, 2026. These are local contracts within the confirmed
[approach](../../BRIEF.md). The [architecture](ARCHITECTURE.md) covers the major
subsystems; the [worklist](WORKLIST.md) distinguishes established paths from
remaining reconstruction. The engine's general feasibility is not an open item.

## Resources and graphics

- RAT namespace ingestion, dependency lookup, publication and tag unload are
  mapped. Recover uncommon record fields, partial initialization/cancellation
  and script-visible failure outcomes. The loader's phase retries can continue
  indefinitely; queue completion and asset success are different conditions.
- GPU creation handlers and model/sprite/particle/movie draw consumers are
  connected. Finish material, skinning/palette and coordinate conventions,
  device reset/loss and complete postprocess ordering.
- All 766 named shader variants resolve to 490 cached bytecodes. Identify the
  remaining 49 unique bytecode hashes from the broader scan outside that registry,
  and recover constant/register meaning for translation.
- Animation track fields and finite half conversion are decoded. Recover exact
  SIMD blend approximations, out-of-range/sentinel policy, secondary pose passes
  and morph/material animation behavior.

## Actors, AI, physics and particles

- Eight actor factories/nine game tables and a selected full actor lifecycle are
  connected. Resolve differences in other actors/modes, spawn gates, movement
  receivers and exceptional ownership paths.
- All 155 AI wrappers / 27,079 nodes / 6,817 call targets have structural checks.
  All 109 action registrations resolve to 48 constructor families and 112 method
  targets. Decode their full parameter schemas, gameplay effects, exact evaluation
  modes, controller-stack limits and message registration policy. Diagnostic
  format strings supply original parameter labels but are not complete schemas.
- The owned PhysX scene is connected to timing, simulate/fetch and release.
  Establish the exact 2.6.4 ABI, shape/filter/receiver details, and synchronization
  guarantees of the separate hide/trace workers. The available 2.8.1 headers
  establish related interfaces, not exact old vtable offsets.
- Particle common emission/curves/RNG, class families, children, collision,
  animation, GPU state, caps and teardown are mapped. Remaining: scalar terminal
  index, specialized math, authored names/units, allocation failure/fragmentation
  and exact numeric behavior.
- Preserve resource-order-dependent duplicate particle IDs and incoming load
  adjustment. Complete EVMT channel/axis semantics and S-service classification.
  Native cross/two-point classes do not prove gameplay usage: neither primitive
  code appears in the four inventoried packs.

## Scheduling, UI and services

- CRT installs the concrete application; startup, service registration, root
  driver and normal render stop/join are mapped. Resolve exceptional exits,
  secondary destructor order and mode-specific task ordering/cadence.
- Recovered Lua already round-trips. Finish exact argument/result and ownership
  contracts for native callbacks, mission event producers, modal/restart gates.
  The old 461-indirect-call inventory is a selected baseline, not a current count
  of unresolved calls across the engine.
- Six startup screen task interfaces and the common text/sprite/movie draw path
  are connected. Finish individual dialog/menu branches, localized layout,
  all-disabled selection and overflow handling, and ring reuse under unusual cadence.
- Input polling, controller fallback, deadzones, held/pressed/repeat masks and
  keyboard/mouse acquisition are mapped. Resolve every binding/action consumer
  and mouse-space convention.
- XACT 3.7 / XAudio2 2.7 bank/cue/update/release paths are connected. Recover codec
  entries, cue flags, voice generation/stealing and exact spatial parameters.
- DirectShow graph, completion polling, two-buffer conversion and GPU submission
  are connected. Validate codecs, unexpected events, graph failure and timing.
- Three save files, exact-count I/O, checkpoint serialization and section header
  are mapped. Decode nested schemas, malformed/short-data outcomes and menu
  unlock policy. Five costume assets and the ending `_ExecBonusOpen(0)` call do
  not by themselves establish individual unlock conditions.
- Steam context, callback pumping and concrete stats calls are mapped. Determine
  actual loaded runtime modules, cloud policy and failure behavior from evidence;
  the generic interface list does not prove multiplayer or every service's usage.

## Runtime witnesses and implementation decisions

No original-game execution has yet validated these static results. Targeted
witnesses needed for faithful reconstruction are startup/task cadence, resource
failure/cancellation, command-buffer reuse, physics timing, animation/graphics,
audio/video and save round trips. These are validation tasks for explicit
contracts, not a request to restart broad feasibility experiments.

The user selected source reconstruction, followed by a faithful direct port and
later upgrades. See the [implementation plan](../../IMPLEMENTATION.md). Series X
Developer Mode remains the destination and the Mac the research host. Renderer
backend, legacy middleware adaptation, packaging and performance targets remain
open engineering choices. Verify destination constraints when implementing those
boundaries. The translate-versus-reconstruct direction is settled.

## Historical provenance

Cavia's in-house origin and original release date are established by direct
sources in [engine history](../sources/cavia-engine-history.md). Historical engine
name, extent of reuse in other games and exact PC port changes remain open.
CEDEC access and the from-scratch statement are closed questions.

The acquired files are identified by executable hashes and recorded depot/manifest
IDs. Exact Steam app build ID remains unrecorded; the UI's `Version 1.0.5` string
is not that identifier. Preserve originals and add hashes when new assets enter
an experiment. Never place research in the game installation.
