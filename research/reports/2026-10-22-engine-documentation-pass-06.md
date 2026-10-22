# Engine documentation pass 06 — architectural coverage

October 22, 2026. Current entry point: [engine architecture](../engine/ARCHITECTURE.md),
revision 7. Incremental record: [pass 06 log](../logs/2026-10-22-engine-pass-06.md).

## Outcome and scope

The major Steam PC engine domains now have connected static maps: startup and
shutdown, task/script scheduling, resources, actors, AI, animation, physics,
particles, rendering, UI, input, audio/video, saves and platform services. The
brief now records completion of architectural coverage. The worklist tracks
specific remaining contracts rather than unidentified subsystem entry points.

This is an architectural milestone, not a claim that every native function,
asset field or gameplay behavior is reconstructed. No original-game runtime
session or playable port was produced in this pass. The established Lua source
round-trip results remain the executable script baseline.

All work stayed under `research/`. The downloaded game was read-only. Final
[hash checks](../evidence/engine/pass-06/final-input-hashes.json) confirm both the
original executable and unpacked analysis image still match their baselines.

## Connected results

| Area | Result | Living map |
|---|---|---|
| Bootstrap | CRT initializer installs concrete game application; ordered service/startup registration and render stop/join | [Bootstrap/tasks](../engine/subsystems/bootstrap-and-tasks.md) |
| Resources | RAT load/relocation, namespace/precedence, dependency requests, publication/failure distinction, tag unload and record release | [Resources](../engine/subsystems/resource-dispatch.md) |
| AI | Native-derived tree/data schema, Lua score/priority selection, name-based action lookup, controller dispatch/retirement | [AI](../engine/subsystems/ai-runtime.md) |
| Action families | All 109 registrations → 48 factory/constructor families → 112 method targets, with original debug parameter labels | [Action index](../evidence/engine/pass-06/ai/ACTION-INDEX.md) |
| Physics | Owned SDK/scene, collision queue, gameplay frame → scene timing/simulate/fetch and normal release | [Physics](../engine/subsystems/physics-and-collision.md) |
| Animation | Vector/rotation channel fields, key selection, finite half conversion and SIMD blend boundary | [Animation](../engine/subsystems/animation-data.md) |
| Rendering | GPU handlers → model command → record interpreter → indexed draw; render arena publication/worker/PresentEx/completion | [Rendering](../engine/subsystems/rendering.md) |
| Shaders | Complete 766-name S/T pair registry → all 490 cached bytecodes | [Rendering](../engine/subsystems/rendering.md) |
| UI | Six startup lifecycle tables; title/menu state, sprite/font ring, movie textures and GPU consumers | [UI](../engine/subsystems/ui-and-screen-tasks.md) |
| Input | Active-task polling; four-controller selection/fallback; deadzones, action masks, repeat; keyboard/mouse acquisition | [Input](../engine/subsystems/input.md) |
| Audio/video | XACT/XAudio COM identity, bank/stream/cue/update/release; DirectShow graph, texture conversion, event and GPU path | [Audio/movies](../engine/subsystems/audio-and-movies.md) |
| Save/progression | Three local files, exact-count I/O and separate success/completion; checkpoint snapshot and section header | [Saves](../engine/subsystems/saves-and-progression.md) |
| Platform | Windows thread/event/file boundaries; Steam context, callback pumping, stats callers and configuration | [Platform](../engine/subsystems/platform-and-services.md) |

Earlier actor, mission and particle maps remain connected foundations. This pass
extends them; it does not replace those reports or erase their bounded unknowns.

## Reproducible process and tooling

Host: macOS on Apple Silicon. Analysis: **Ghidra 12.1.4**, headless with bundled
**JDK 25.0.4.1**, and the existing x86 project. Python 3 handles PE/table reads,
corpus checks and evidence generation. The saved LLVM instruction listing supplies
independent navigation and instruction-boundary checks. No Wine/Steam launcher or
running game was needed for this static pass.

1. Follow previously established owners and dispatch sites into native methods.
2. Read Ghidra pseudocode alongside exported x86 instructions. Check implicit
   registers and stack arguments when C types or signatures omit information.
3. Verify virtual targets against raw image pointers; use original strings to
   identify names where available. Retain addresses/numeric fields otherwise.
4. Apply native-derived schema formulas to the full relevant corpus, or compare
   a numerical helper to an independent implementation, rather than relying on
   a few visual samples.
5. Record the connected path, lifecycle and remaining uncertainty in subsystem
   notes, with editable Mermaid diagrams. Maintain logs during the work.

Analyzed image base is **0x400000**, SHA-256:
`bdaff1746d3092215699b09eab11315c6adf8fb71eaa8f46c280b11b3cd448fd`.

### Native export slices

**23 slices, pass-06a through pass-06w**, contain **1,170 accepted unique function
exports** after excluding two rejected fragments. This count includes callees and
incoming callers selected by the exporter and overlaps prior passes; it must not
be added to earlier totals to claim an engine-wide unique count.

Every slice preserves `seeds.txt`, `command.json` (full executable paths and Java
environment), `console.log`, Ghidra logs, `status.tsv`, references, pseudocode and
instructions. The generated [function index](../evidence/engine/pass-06/FUNCTIONS.md)
links the latest accepted evidence for each address.

Reproduce a slice with a fresh name, using verified function starts:

```sh
python3 research/tools/engine_slice.py pass-new 568f20 569170
```

The wrapper invokes the established headless project with `-noanalysis` and
[ExportEngineSlice.java](../experiments/engine-ghidra-scripts/ExportEngineSlice.java).
It refuses an existing output directory. The exporter can create a missing
function at a seed: use instruction boundaries, not approximate addresses.

A separate read-only
[ExportEngineIndex.java](../experiments/engine-ghidra-scripts/ExportEngineIndex.java)
produced the [navigation index](../evidence/engine/pass-06/native-index/command.json):
**11,291 recognized functions / 275,940 references** at that checkpoint.
This is not exhaustive disassembly. Function min/max spans can include gaps;
reference owners and actual function bodies take precedence over range guesses.

### Corpus and byte audits

Run from the project root:

```sh
python3 research/tools/ai_runtime_audit.py
python3 research/tools/ai_action_audit.py
python3 research/tools/shader_registry_audit.py
python3 research/tools/animation_scalar_audit.py
python3 research/tools/platform_import_audit.py
python3 research/tools/engine_pass06_audit.py
```

[engine_image.py](../tools/engine_image.py) verifies the analysis-image hash before
address reads. The action audit checks registry triples and exports all first-nine
method pointers; its diagnostic format strings are not asserted as complete
parameter schemas. The platform audit uses the saved reference checkpoint, so
its caller list reflects recognized references at that time.

| Audit | Result | Evidence |
|---|---|---|
| AI corpus | 155 wrappers, 27,079 nodes, 6,817 adjusted entry targets; schema/tree/Lua links checked | [Summary](../evidence/engine/pass-06/ai/summary.json) |
| AI action table | 109 triples, 48 constructors, 112 method targets; every target exported | [Method table](../evidence/engine/pass-06/ai/action-vtables.json) |
| Shader registry | 766 variants, 490/490 cache IDs used, 53 VS 3.0 + 437 PS 3.0 bytecodes; stage/token/hash checks | [Registry](../evidence/engine/pass-06/shader-registry.json) |
| Animation scalar | All 63,488 finite binary16 patterns match independent Python conversion | [Scalar audit](../evidence/engine/pass-06/animation-scalar-audit.json) |
| Native witnesses | 36 direct call/jump edges, 93 vtable slots, six COM GUIDs and CRT table entry match bytes | [Witnesses](../evidence/engine/pass-06/native-witnesses.json) |
| Platform imports | 321 imports across 16 DLLs, 700 recognized references in checkpoint | [Import/caller manifest](../evidence/engine/pass-06/platform-import-callers.json) |
| Research navigation | Engine maps, brief, research index and generated evidence links checked | [Witnesses](../evidence/engine/pass-06/native-witnesses.json) |

The 2,048 exponent-31 half patterns follow the game's helper behavior and do not
produce IEEE infinity/NaN. The original shader scan found 541 occurrences / 539
unique hashes; 49 unique hashes lie outside the now-resolved 490-entry registry.
Neither fact is silently normalized away.

## External references

Preserved Microsoft DirectX headers under
[directx-2010-reference](../sources/directx-2010-reference/provenance.json) support
COM GUID and D3D interface identification. Historical NVIDIA headers under
[physx-2.8.1-reference](../sources/physx-2.8.1-reference/provenance.json) inform
interface interpretation, but are not proof of the executable's exact 2.6.4 ABI.
Steam's official API documentation informs stats/callback interpretation, not
old interface slot numbering. Source URLs, limitations and provenance accompany
the subsystem notes and saved files.

## Corrections retained in evidence

- Input and VSync are **+0x14 active-pass** tasks. The initial working note called
  input a late-pass task. Raw vtable checks correct that; screen drawing uses +0x18.
- The application is not limited to empty base hooks. CRT table entry **0x6c0628**
  invokes **0x401950**, which installs concrete vtable **0x829cd0** before startup.
- PhysX timing/step arguments missing in C were recovered from x86. The timing
  configuration receives `min(dt, 1/15)`; simulation receives the original dt.
- AI call-table values require **+4** to reach their following tree record.
  All 6,817 targets were rechecked with this native adjustment.
- Authored AI numeric codes do not consistently match PC registry indices.
  Native lookup prefers an available action name; audits preserve both fields.
- Seed **0x461785** was an interior fragment; repaired to **0x461770**.
  Seed **0x43a400** was inside an instruction; repaired to **0x43a3a0**.
  Repair commands/logs remain under pass-06 `boundary-repair` and
  `startup-boundary-repair`. Historical exports remain but are excluded from
  the accepted index. The source executable bytes were never patched.
- Rejected seed **0x5079b0** is jump-table data; audio playback wrapper
  **0x5079f0** was exported instead.
- A final edge audit corrected the sprite producer relationship:
  **0x564460** calls both geometry preparation **0x563110** and registration
  **0x569620**. Preparation itself does not call registration.

## Handoff and unresolved work

The [worklist](../engine/WORKLIST.md) and
[open questions](../engine/OPEN-QUESTIONS.md) now carry bounded reconstruction:
mode-specific ordering, exceptional cleanup/cancellation, full native argument and
action schemas, exact shader/pose/math semantics, legacy physics ABI, save/unlock
policy, and runtime fidelity. They no longer ask whether RAT lookup, AI tree
fields, service ownership or the named shader registry can be found.

The existing brief reserves the execution/translation strategy and renderer
implementation for a direction decision. This pass supplies the concrete boundary
map needed for that decision: recovered Lua/data, native gameplay/AI, queues and
ownership, graphics commands, Windows/Steam APIs and middleware services. A
strategy must account for those interfaces and their validation witnesses;
export counts alone cannot select it. No runtime or replacement architecture was
silently chosen during this mapping pass.
