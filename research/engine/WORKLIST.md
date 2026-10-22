# Engine mapping worklist

Updated October 22, 2026. [Architecture](ARCHITECTURE.md).

Every major domain now has a connected static map. This closes the broad
subsystem-discovery queue; it does not mean every native function, data field or
runtime behavior is reconstructed. The items below distinguish established
architectural paths from remaining contract work. Unknowns stay local and do not
reopen the confirmed project direction. Source reconstruction is now selected.
The [implementation plan](../../IMPLEMENTATION.md) consumes this contract backlog;
its resource/data foundation (SRC-01) is now implemented and corpus-checked.
The [reconstruction design set](../../docs/architecture/README.md) is a review
draft defining how these contracts enter one engine; review precedes the next
source slice. See [module documentation](../../docs/resources.md). Runtime lifecycle/handlers
remain implementation work for SRC-02.

## Execution and resources

| ID | Established path | Remaining closure |
|---|---|---|
| EXEC-01 | CRT initializer → concrete application → service setup → startup task → root; render stop/join and post-loop cleanup | Secondary destructors and exceptional startup/quit exits |
| EXEC-02 | Mission load/start/events, status/outcome routing and restart anchors | All event producers, modal gates and reload-side ownership |
| EXEC-03 | Lifecycle states, active/late passes; concrete input/VSync/audio/screen methods | Exact mode-specific task list order and cadence |
| EXEC-04 | Lua registrations, immediate/yielding calls, pending/resume convention | Precise native argument/result types for every callback |
| RES-01 | RAT ingestion, recursive relocation, dotted lookup, precedence, dependency requests and CPR/CALL consumers | Remaining record fields and uncommon consumers |
| RES-02 | Worker callback, copied-argument ownership, publication and normal cleanup | Cancellation, partial allocation and producer/consumer races |
| RES-03 | All 128 queue slots counted, including running callbacks; lock helpers resolved | Closed for this queue |
| RES-04 | Requested/opened/phase/failure masks and publication readiness separated | Script-visible outcome of each asset failure; unbounded phase retry behavior |
| RES-05 | Tag unload → archive unlink → identity-checked CALL/type release → section free | Pending-load cancellation and partially initialized resources |

Maps: [bootstrap/tasks](subsystems/bootstrap-and-tasks.md),
[execution/scripts](subsystems/execution-and-scripts.md),
[mission lifecycle](subsystems/mission-lifecycle.md),
[worker](subsystems/background-resource-worker.md),
[resources](subsystems/resource-dispatch.md),
[stage loading](subsystems/mission-and-stage-loading.md).

## Graphics, actors and simulation

| ID | Established path | Remaining closure |
|---|---|---|
| MODEL-01 | PW0 declarations, 125 skeleton records, groups/draw metadata | Coordinate/palette conventions and visual validation |
| MODEL-02 | Mesh handler → GPU resources → queued model command → record interpreter → indexed draw | All material/deformation branches and draw-state equivalence |
| MODEL-03 | Motion relocation → pose consumers → vector/rotation sampling; finite half conversion audited | Additional pose/morph/material passes, exact SIMD approximations and out-of-range policy |
| RENDER-01 | Command arena/list publication, worker execution, PresentEx, completion wait | Device reset/loss, all error returns and postprocess order |
| RENDER-02 | All 766 names → S/T pairs → 490 shader bytecodes; all IDs/stages/hashes checked | Shader constant meaning and 49 additional unique scan hashes outside this registry |
| ACTOR-01 | Eight factories/nine game tables; selected actor/model ownership, deferred spawn and retirement | Other actor variants, spawn gates and exceptional paths |
| ACTOR-02 | Selected hierarchy/frame/state/message/AI/pose/attachment order | Other modes, actors and secondary pose/update passes |
| AI-01 | All 155 wrapper schemas / 27,079 nodes; call targets, evaluation and 109 registrations / 48 constructor families / 112 method targets | Original names for evaluation modes, full action parameter/gameplay schemas and stack edges |
| PHY-01 | Owned scene/SDK, collision queue, scene timing/simulate/fetch and release | Exact legacy ABI, shape/filter/receiver detail and separate hide/trace worker guarantees |

Maps: [models](subsystems/model-data.md), [rendering](subsystems/rendering.md),
[animation](subsystems/animation-data.md), [actor construction](subsystems/actors-and-animation.md),
[actor lifecycle](subsystems/actor-lifecycle.md), [AI](subsystems/ai-runtime.md),
[physics](subsystems/physics-and-collision.md).

The renderer is reconstructed incrementally within the implementation plan.
Its first target is faithful output; graphics upgrades follow the baseline.
Mapping the original GPU and thread boundaries is complete at the stated scope.

## Particle contracts carried forward

| ID | Established | Local questions |
|---|---|---|
| PFX-01 | Four packs; 585 effects / 1,640 parts / 833 definitions; curves and duplicate-ID precedence | Authored field names/units and scalar terminal index |
| PFX-02 | Common emission, RNG, motion, collision interface, children, model animation and class families | Specialized math and exact numeric behavior |
| PFX-03 | Geometry/command/GPU path, six blend modes and seven shader pairs | Translation and visual matching |
| PFX-04 | All 25 stage names/IDs, G/S split and nested EVMT consumers | Channel encodings/axes and final S-service classification |
| PFX-05 | Caps, eviction, graceful/forced stop and teardown | Arena fragmentation/failure edges |

Maps: [particles](subsystems/particles-and-effects.md),
[particle runtime](subsystems/particle-runtime-contracts.md).

## Remaining service contracts

| ID | Established path | Remaining closure |
|---|---|---|
| AUDIO-01 | XACT 3.7 / XAudio2 2.7 initialization, memory/stream banks, cue updates, spatial calls and release | Cue flags/generation/voice policy and full bank codec decode |
| VIDEO-01 | DirectShow graph, two texture buffers, pixel conversion, draw/event polling and release | Codec availability, unusual events and graph failures |
| INPUT-01 | Active task polling, controller fallback/deadzones, action masks, repeats, keyboard/mouse read | Full binding/action consumers and mouse conventions |
| SAVE-01 | Three local files, exact-count I/O, completion versus success, checkpoint/section serialization | Nested schemas, short/malformed load behavior and unlock/menu policy |
| PLATFORM-01 | Thread/event/timing/heap/file boundaries; Steam context and concrete stats/callback callers; config | Exact loaded DLLs, remote-storage policy, exceptional cleanup and runtime behavior |
| UI-01 | Six startup lifecycles, title/menu transitions, font/sprite ring and movie GPU path | Other dialogs, layout, exact timing, overflow/reuse and progression policy |

Maps: [audio/movies](subsystems/audio-and-movies.md), [input](subsystems/input.md),
[saves](subsystems/saves-and-progression.md), [UI](subsystems/ui-and-screen-tasks.md),
[platform](subsystems/platform-and-services.md).

## Runtime and provenance

- **OBS-01:** original-game runtime observations remain absent. Needed witnesses
  are now specific: startup/task cadence; resource failure/cancellation; queue
  reuse; physics timing; animation/graphics match; audio/video and save round trip.
  Static reconstruction continues to be useful without treating these as passed.
- **PROV-01:** in-house engine origin is established. Historical engine name,
  extent of reuse and exact PC changes remain historical questions.
- **PROV-02:** exact Steam app build ID remains to record if available; existing
  depot/manifest IDs and executable hashes identify the acquired research inputs.

## Completion and handoff rule

A domain's architecture is covered when its inputs, owner, execution/dispatch,
outputs/platform boundary and normal release are connected, with local gaps
named. Automatic decompilation alone does not satisfy that rule. Behavioral
reconstruction requires the remaining contract work and appropriate runtime
witnesses; no percentage of the entire engine is inferred from export counts.

Source reconstruction is the selected implementation route. Renderer backend,
middleware adaptation and final packaging remain engineering decisions within
that route. Continue local contract closure alongside implementation; ask only
for a real direction change or required external input.
