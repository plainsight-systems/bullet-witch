# Engine documentation pass 05 — selected actor lifecycle

October 22, 2026. Current reference: [actor lifecycle](../engine/subsystems/actor-lifecycle.md).
Main entry point: [architecture revision 6](../engine/ARCHITECTURE.md).

## Result and scope

The ordinary actor constructed by **0x5fe070**, vtable **0x852334**, now has a
connected static lifecycle: construction and registration, message/state/AI
updates, model pose and attachments, collision interfaces, deferred spawning,
retirement and destruction. The model branch followed uses vtable **0x850d9c**.
This extends the eight factories resolved in pass 04. It does not establish every
actor variant, every game mode or complete engine behavior.

The mapping now establishes:

- **Hierarchy and frame order:** game method 0x45f070 prepares entity messages,
  walks parent objects before children through 0x44f650, processes retirement,
  services/signals named workers, handles actor overlap and updates effects.
  Actor-specific registration lists serve queries/state processing; they are
  distinct from the main hierarchy traversal.
- **Actor behavior:** dispatcher 0x568960 handles current/pending states and
  component messages. The component/state/message dispatch formula and original
  RTTI identify concrete behavior components, including Runner, Shooter, Grappler,
  Dodger and AiBehavior.
- **AI connection:** AiBehavior construction, logical ai.%s resource binding and
  actor-message callbacks connect to 0x5a7570, behavior/message dispatch 0x503210
  and controller transitions including 0x505310. Node fields, action semantics
  and full Lua execution policy remain explicit AI-01 work.
- **Animation consumption:** the model child advances motion and evaluates pose;
  weapon children read resulting bone transforms. Motion-event channels follow
  the pose path. Codecs and additional evaluation passes remain MODEL-03 work.
- **Collision boundary:** moving-parent correction and actor-pair separation
  messages are connected. Their receiver handlers and the PhysX scene step/fetch
  boundary remain PHY-01 work.
- **Deferred spawning:** 256 stored entries preserve authored actor data,
  transform and life. The consumer can retire eligible distant actors into storage
  and later recreate eligible entries; an entry clears only after successful
  creation. The local count threshold is not a universal actor cap.
- **Retirement:** marked objects leave the hierarchy and pass through three
  buckets. Destruction occurs on the third later maintenance call after admission,
  not necessarily after three rendered frames. Teardown destroys components and
  children, unlinks registrations, invalidates handles and frees through the
  captured allocator.

Two diagrams in the lifecycle note show frame/ownership relationships and the
retirement state sequence. Detailed offsets, branches and addresses live beside
those diagrams rather than being duplicated in this report.

## Process and tooling

All work ran on the Mac against the existing unpacked x86 executable copy under
`research/experiments/steamstub/`. The downloaded game directory was not modified.
The analyzed image SHA-256 is:

```text
bdaff1746d3092215699b09eab11315c6adf8fb71eaa8f46c280b11b3cd448fd
```

1. Followed the prior constructor, vtable and shared-pose anchors using the saved
   LLVM instruction listing and [native_trace.py](../tools/native_trace.py).
   Its `refs` and `ranges` modes locate references and instruction ranges;
   neither range boundaries nor decompiler signatures alone establish functions.
2. Exported eight successive connected slices with **Ghidra 12.1.4**, the existing
   BulletWitch analysis project, **JDK 25.0.4.1**, `-noanalysis` and
   [ExportEngineSlice.java](../experiments/engine-ghidra-scripts/ExportEngineSlice.java).
   The exporter includes seeds, their direct callees and incoming callers.
3. Compared pseudocode with x86 instructions, constructor stores, vtable entries,
   original RTTI and script API strings. Register arguments and aliased decompiler
   temporaries were checked before assigning offsets or ownership.
4. Wrote findings and the AI classification correction incrementally to the
   [worklog](../logs/2026-10-22-engine-pass-05.md), then reconciled the subsystem
   notes, architecture, worklist and open questions.
5. Ran [engine_pass05_audit.py](../tools/engine_pass05_audit.py). It checks the image
   hash, selected vtable slots, relative CALL/JMP instruction bytes, tiny virtual
   dispatch thunks, original RTTI strings and all slice export statuses. It also
   generates the [function index](../evidence/engine/pass-05/FUNCTIONS.md).

Each export directory contains exact command arguments and environment overrides
in `command.json`, selected seeds in `seeds.txt`, status/reference tables,
pseudocode, instructions and logs:

| Slice | Command and environment | Successful function exports |
|---|---|---:|
| 05a | [command.json](../evidence/engine/pass-05a/command.json) | 178 |
| 05b | [command.json](../evidence/engine/pass-05b/command.json) | 317 |
| 05c | [command.json](../evidence/engine/pass-05c/command.json) | 88 |
| 05d | [command.json](../evidence/engine/pass-05d/command.json) | 109 |
| 05e | [command.json](../evidence/engine/pass-05e/command.json) | 106 |
| 05f | [command.json](../evidence/engine/pass-05f/command.json) | 77 |
| 05g | [command.json](../evidence/engine/pass-05g/command.json) | 117 |
| 05h | [command.json](../evidence/engine/pass-05h/command.json) | 53 |

The slices overlap: they contain **823 unique function addresses**, not 823 fully
interpreted functions. All export statuses are `ok`; no unresolved export seeds
remain. The reproducible audit command from the project root is:

```sh
python3 research/tools/engine_pass05_audit.py
```

## Verification and correction

[Native witnesses](../evidence/engine/pass-05/native-witnesses.json) record
**26 selected direct call/jump edges**, **16 vtable slots**, **two virtual dispatch
thunks** and **seven original RTTI names**, checked against the hash-bound image.
This verifies selected structural evidence; it does not test gameplay or every
interpretation in the exported functions. [Document validation](../evidence/engine/pass-05/document-validation.json)
records local-link checks for the current research documents.

An early working interpretation labeled model functions **0x5f9b10 / 0x6ad170**
as AI evaluation. Deeper inspection corrected that: they process motion-linked
channels, with interval callbacks through **0x6acea0**. The worklog and current
map preserve the correction. The actual AI connection is separately supported by
original AiBehavior RTTI, actor dispatch registrations and controller calls.

No game execution or rendered comparison was performed. Parent-before-child
ordering is established within the traced hierarchy pass; worker event signals
alone do not establish completion before the next frame. Other modes and the
second virtual +0xc hierarchy traversal remain separately scoped.

## Continuation

Continue into **AI-01** from the concrete AiBehavior/controller path to decode
node fields, action data and Lua execution policy. Then connect **PHY-01** movement
receivers to scene stepping and synchronization. Keep **MODEL-03** codecs/blending
and other actor variants explicit in the [worklist](../engine/WORKLIST.md).
The renderer milestone remains deferred while the engine map advances.
