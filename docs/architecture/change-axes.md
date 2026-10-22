# Reasons the engine changes

**Design · working baseline · October 22, 2026.**
Derived from the [engine architecture](logical-overview.md) and
[execution contracts](execution-contracts.md). [File mapping](file-mapping.md)
assigns these responsibilities to code.

An axis is an independently changing responsibility, not automatically a class,
library or directory. Split a unit when a concrete change affects one part without
affecting another. Keep operations together when their contract requires them to
change together. A phase can cross many axes; a module can serve many phases.

## Runtime structure, scene and presentation

These axes follow the [runtime ownership model](runtime-model.md),
[world structure](world-and-simulation.md) and [renderer](rendering-and-presentation.md).
They are independent of which part is reconstructed first.

| ID / responsibility | What triggers a change | What it may change | What stays outside |
|---|---|---|---|
| E13 Runtime/world ownership | A different owner lifetime, world-instance composition or quiescence requirement | Engine/world aggregates, explicit service dependencies and partial construction | Concrete mission outcomes, actor equations and platform API calls |
| E14 Scene/spatial observations | A changed transform/bounds/membership or query-result contract | Shared scene observations and world bindings with declared phase validity | A universal spatial index, camera policy or physics-result policy |
| R1 Views and visibility | Recovered camera representation, culling, volume or group-selection behavior | View contracts and renderer scene/visibility preparation | Gameplay camera control, simulation eligibility and material equations |
| R2 Materials, shading and environment | Recovered variant, parameter, texture/light/environment or numerical shader meaning | Definition/instance material state, shader binding and lighting inputs | Native graphics API lifetime, game time and entity ownership |
| R3 Render passes | Recovered target dependencies, draw/pass ordering or postprocess behavior | Pass descriptions, attachment use and contribution sequencing | Generic task scheduling and actor simulation |
| E15 UI presentation | Recovered layout/font/sprite primitive or UI scene behavior | Shared layout/draw state and render contribution | Menu navigation, unlock policy and screen task completion |
| E16 Memory and residency | A changed storage-lifetime, retention, capacity or allocator requirement | Scoped allocation interfaces/accounting and owner-specific reuse policies | Invented global eviction, quality fallback or changed script outcomes |
| E17 Shared numerical contracts | A demonstrated common transform/vector/interpolation or conversion contract | Shared math with explicit coordinate/rounding/precision semantics | Combining superficially similar algorithms with different fidelity requirements |
| G6 Camera behavior | Recovered player/aim/cutscene camera policy | Game camera controller producing the renderer's view request | Projection conventions, visibility algorithms and native device calls |

E13 does not turn the world aggregate into a manager for every operation. Its
systems retain their own state authority. E14 does not force actor queries,
physics and rendering to use the same candidate-selection algorithm. R1/R2/R3
separate renderer responsibilities that the earlier command-only E8 boundary did
not describe. E8 below retains command encoding/submission and consumption.
E16 leaves allocation ownership with the actual subsystem; shared helpers appear
only where a concrete contract warrants them.

**Boundary tests:** correcting a camera follow equation changes G6, not the
renderer device adapter. Correcting a shader constant changes R2 without changing
actor ownership. A pass-order correction changes R3 and its expected image, not
the resource catalog. A world restart lifetime correction changes E13/G1 rather
than hiding cleanup policy inside the mesh parser. Changing a scene representation
must preserve every consumer's selection and phase-validity contract.

## Execution and game meaning

| ID / responsibility | What triggers a change | What it may change | What stays outside |
|---|---|---|---|
| E1 Task progression | Recovered task transition, pass or eligibility rules | Lifecycle scheduling and current/requested task state | Root mission policy, job transport and specific task bodies |
| E12 Background work | Recovered queue, submission, execution or completion rules | Job argument ownership, capacity/rejection, ordering and pending/running status | Task lifecycle and specific resource/game work bodies |
| E2 Script execution | Required Lua semantics, marshaling, yield/resume or context lifetime | Compatible runtime, bridge mechanism and generic continuation pumping | API-specific game operations and mission flow |
| G1 Session/mission and game orchestration | Recovered root/mission/startup/screen transition or mode schedule | Session/mission ownership, entry selection, game-specific gates, outcomes and task composition | Generic Lua pump, container layout and host APIs |
| G2 Script bindings | Recovered native API arguments/results or its operation phases | Root/mission/AI API registration and typed adapters into game/engine services | Generic VM scheduling and hidden duplicate game rules |
| E3 Entity lifecycle | Recovered hierarchy, handles, message/state dispatch or retirement mechanism | Parent/child traversal, state transitions, identity validation and delayed release | Particular enemy/player state logic |
| G3 Actor and action behavior | Another reconstructed player/enemy/vehicle/boss component or native AI action | Concrete state/action equations, factories, parameter interpretation and cadence policy | Shared entity storage and generic AI tree traversal |
| E4 AI evaluation | Recovered tree control, score selection or controller transition mechanism | Tree walking, score comparison, branch/controller state and action-interface lifecycle | Concrete movement/combat routines and global game scheduling |
| E5 Animation | Recovered channel codec/sampling, blending, events or attachment computation | Track evaluation, pose construction and shared model-motion behavior | Actor decision logic, graphics backend and effect emission |
| E6 Effects | Recovered pack binding, emission, RNG, child, simulation or retirement rule | Effect instances and particle behavior using animation/collision/presentation contracts | Independent physics engine or duplicate model renderer |
| E7 Collision and physics semantics | Recovered scene/query/filter/mutation or engine collision behavior | Observable collision/dynamics contract, engine overlap mechanisms and mutation sequencing | A particular SDK's ABI or platform packaging |
| E8 Render command submission | Recovered model/sprite/text/particle/video command or consumer contract | Command families, encoding, retained inputs and CPU/device completion boundaries | Camera, visibility, material and pass policy owned by R1/R2/R3; screen behavior |
| E9 Audio behavior | Recovered cue/bank/spatial/event semantics | Logical audio service, cue state and bank lifetime | Native mixer/device implementation and video playback |
| E11 Video behavior | Recovered movie open, sample, completion or stop semantics | Playback state, frame ownership and consumer handoff | OS codec graph, screen state machine and audio bank policy |
| E10 Input interpretation | Recovered deadzone, action-mask, edge/repeat or binding behavior | Transform sampled device observations into engine input state | OS device polling and screen/mission decisions |
| G4 Persistence | Recovered snapshot/restore fields or persistence operation behavior | Game-state capture/restore and interpretation of saved records | File transport, unlock decisions and generic task execution |
| G5 Progression | Recovered mission advancement, record or unlock policy | Availability/outcome decisions using game and saved state | File transport, asset discovery and serialization layout |

G1 includes distinct startup, mission and screen implementations; sharing an axis
does not require putting them in one file. G3 separates concrete behavior families
as evidence requires. G4 snapshot/restore and G5 progression are separate because an unlock-policy
correction need not change serialization. Field decoding remains in its A5 family
and storage transport in P1. Shared math is introduced only for identical operations
with the same numerical contract, not because formulas look similar.

**Boundary tests:** adding a recovered enemy action changes G3 and its registration,
not the E2 Lua pump. Correcting AI tie selection changes E4 without changing enemy
movement equations. Model particles consume E5/E8 directly; a new particle shape
does not require copying either subsystem. A menu unlock correction changes game
policy without changing archive discovery or save file I/O.

## Content and resource execution

| ID / responsibility | What triggers a change | What it may change | What stays outside |
|---|---|---|---|
| A1 Container formats | Another supported RAT, CPR, DPK or AMB layout/encoding | Checked fields, names, ranges and format-specific immutable views | File location, request timing, runtime objects and gameplay |
| A2 Content access | Different installation layout, virtual prefix or archive source | Logical-to-physical location, mounts, access policy and path containment | Format decoding, catalog priority and game progression |
| A3 Catalog resolution | Recovered namespace/precedence/dependency-selection behavior | Lookup, exact selected identity and ordered dependency-name interpretation | I/O execution, handler phases and resource availability |
| A4 Resource lifecycle | Recovered request, phase, publication, retry or unload behavior | Request/file/record state, group retention, publication identity and handler scheduling | Payload equations and host graphics/audio implementation |
| A5 Payload interpretation | Newly recovered model, motion, effect, sprite, audio-bank or save-field schema | Bounded semantic data views and interpretation of authored fields | Live simulation, device allocation and mission rules |
| A6 Resource construction adapters | Another typed record/CALL construction or release contract | Connect decoded views and dependencies to the owning subsystem; publish/release results | Catalog policy, generic scheduler behavior and backend internals |

A1 has concrete format readers, not one universal binary decoder. A5 has concrete
payload families; it does not become a giant asset-format switch. DPK membership
belongs to A1; executing a member belongs to E2. A mesh handler can use A5
model data and P1 graphics contracts without putting device calls into CPR parsing.

**Boundary tests:** reading CPR bytes from a packaged archive changes A2 while
leaving A1 unchanged. A new mesh layout changes A5 without changing catalog
precedence. Correcting tag unload changes A4 without changing texture decoding.

## Hosts, composition and evidence

| ID / responsibility | What triggers a change | What it may change | What stays outside |
|---|---|---|---|
| P1 Platform and middleware capabilities | Another file/window/input/clock/thread/device/media/physics implementation | Narrow capability contracts and their explicit concrete adapters | Game flow, content schemas and faithful gameplay equations |
| C1 Application composition | Another supported host configuration or executable purpose | Select and connect implementations, own application lifetime and report capabilities | Reimplementation of engine algorithms in the launcher |
| V1 Verification and inspection | A new observable contract, independent reference or diagnostic view | Test inputs/oracles, tools and trace/cost reporting | Production behavior adjusted to satisfy a self-generated oracle |

P1 is a family of independent host capabilities, not a single platform object
with every service method. Graphics, input, file access and physics backends can
change independently. Each concrete adapter belongs under its capability; only
application composition knows the selected combination. Abstract interfaces are
introduced where a boundary needs them, not mechanically for every function.

The engine preserves the original input action/repeat/deadzone behavior above
raw platform device sampling. A host input adapter supplies observations; game
bindings decide their meaning. A physics backend supplies the declared E7 result
contract; changing an SDK version does not by itself prove compatible behavior.

**Boundary tests:** selecting an Xbox file adapter changes P1/C1 and packaging,
not CPR decoding. Replacing a graphics backend changes its adapter, shader
implementation and capability validation, not actor health rules. An independently
verified numerical correction may legitimately affect a shared contract; the
boundary is not a promise that every backend change is invisible.

## Independent dimensions, not speculative implementations

The PC content format, game rules, script semantics, host operating system,
graphics backend and middleware choice are separate dimensions. Only the PC
RAT/CPR reader is implemented. Mac is the current development host; Series X is
the destination. This design does not commit to Metal, D3D12, a newer PhysX,
a particular codec, or a new Lua runtime.

Declare a supported combination at composition time. Unsupported capabilities
fail explicitly. No empty alternative implementations, plugin loader, universal
serializer, ECS conversion or generalized job graph is needed to reserve a
future implementation location.

## Applying the axes

For each implementation identify its runtime owner, callers, produced values,
phase/lifetime and failure semantics, then the axis that explains why it changes.
The [source mapping](file-mapping.md) assigns engine and game responsibilities to
code. The [existing reader migration](../../IMPLEMENTATION.md#migrating-src-01)
is delivery work; it does not define the engine's structure.

Write implementation design in file-opening contracts, with the corresponding
architecture and evidence references. Apply the [engineering workflow](../process/workflow.md),
including both MCP corpus consultations for C++ header design and implementation.
Do not invent guideline citations or treat a directory as evidence that its
subsystem exists.
