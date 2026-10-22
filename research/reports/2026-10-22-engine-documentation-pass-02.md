# Engine documentation pass 02

October 22, 2026. [Architecture](../engine/ARCHITECTURE.md), revision 3.

Continued engine mapping as agreed; the standalone graphics milestone remains
deferred. This pass connects previously separate code/data anchors across startup,
tasks, mission scripts, background work and native resource handlers.

## Results

| Area | Newly documented connection |
|---|---|
| [Bootstrap/tasks](../engine/subsystems/bootstrap-and-tasks.md) | CRT -> application entry -> Steam/PhysX checks -> configuration/window/services -> message loop -> three task passes -> owner update slot |
| [Mission lifecycle](../engine/subsystems/mission-lifecycle.md) | Mission/default callback loading -> load -> setup gates -> start -> event callbacks -> outcome/restart routing |
| [Background worker](../engine/subsystems/background-resource-worker.md) | Submission -> 128-slot worker -> callback execution -> owned argument cleanup; lock methods resolved |
| [Stage loading](../engine/subsystems/mission-and-stage-loading.md) | Complete stage job: model and gm selection, cubemaps, area data and extra stage-6 jobs |
| [Resource dispatch](../engine/subsystems/resource-dispatch.md) | Logical names/dependencies -> file loading -> CPR record dispatch -> phase-specific mesh/motion construction |

The graphics-related work here maps native engine responsibilities; it does not
implement a new importer, viewer or renderer.

Key distinctions now explicit:

- Mission status `+0x61410` is multi-valued: clear, return and internal restart
  paths differ. `_WaitMission` polls nonzero status, not a universal teardown flag.
- Background pending count covers the shared queue and includes the running job.
  An empty queue does not by itself certify successful resource loading.
- CPR processing separates record-type initialization from named CALL phases.
  `vfx.motion.create` runs preparation in phase 1; `vfx2.mesh.create` constructs
  its object in phase 2. Their phase-3 paths provide teardown anchors.
- Motion routine `0x41d570` performs pointer fixups. Animation sampling remains
  to be mapped, even though the native loading path is now known.

## Tooling, evidence and verification

Used existing Ghidra 12.1.4 project and the unpacked image with SHA-256
`bdaff1746d3092215699b09eab11315c6adf8fb71eaa8f46c280b11b3cd448fd`.
The downloaded game was not modified or executed.

[ExportEngineSlice.java](../tools/ExportEngineSlice.java) exports seeds, immediate
callees and incoming callers, retaining C pseudocode, instructions, references and
status. Six overlapping exports retain **299 distinct function addresses** after
excluding two interior fragments. This is supporting material, not a claim that
299 functions were fully understood or every inferred boundary is exact.
[Export summary](../evidence/engine/pass-02/export-summary.json).

Each `pass-02*` evidence directory contains its exact command/environment,
seed list and logs. The saved `command.json` arrays can be replayed against the
existing project. A clean script directory in `experiments/engine-ghidra-scripts`
is used for the later runs to avoid compiling the unrelated Unluac Java program.

[engine_pass02_witness.py](../tools/engine_pass02_witness.py) checks the source
hash, task/worker vtable slots and the literal mesh/motion registration sequences;
it writes [table witnesses](../evidence/engine/pass-02/table-witnesses.json).
Reproduce from the project root with:

```sh
python3 research/tools/engine_pass02_witness.py
```

Instruction review corrected Ghidra's callback/argument mix-up in the worker and
its erroneous `unaff_retaddr` submission parameter. Two exploratory seeds began
inside functions: fragments `0x45ef63` and `0x44b63d` are excluded from the current
manifest and removed from the analysis project. The first was reconstructed at
`0x45ef10`; the second surrounding function is left unresolved. Old fragment
exports remain historical evidence, not valid entry points. Repair compilation
failed initially; the corrected run succeeded. See the [work log](../logs/2026-10-22-engine-pass-02.md).

Verification is static: bytes/table checks, callsite/register review and local
link validation. No runtime timing, rendering or gameplay behavior is asserted.

## Remaining work

Continue with actor creation/update and ownership, physical scene stepping, AI
scheduling and animation sampling using these connected paths. Also finish RAT
catalog ingestion, error propagation, startup-task registration and complete
shutdown. The worklist records these as specific remaining connections; engine
mapping remains the active approach.
