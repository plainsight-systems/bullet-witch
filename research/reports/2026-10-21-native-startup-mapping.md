# Native startup mapping — October 21, 2026

The recovered executable now connects the startup Lua to its native loader,
function registration, coroutine runner, and screen tasks. This extends the
[Lua evaluation](2026-10-21-startup-scripts.md) using the
[offline-unpacked image](2026-10-21-offline-unpacking.md).

The main result is a concrete scheduling contract: an Exec adapter invokes its
native operation immediately; a Wait adapter stores a callback and yields Lua.
The runner polls that callback. **True means remain waiting; false means resume
Lua.** The root driver starts `init`, then repeatedly starts/resumes `main`.

These are static findings from code and data, not observed runtime execution.
Addresses are preferred virtual addresses at image base `0x400000`. Role names
such as “runner” and “task” are analysis labels unless explicitly present in a
string. No game files were changed or executed in this investigation.

## Evidence and reproducibility

Analyzed image: `experiments/steamstub/witch.original.exe.unpacked.exe`.
SHA-256: `bdaff1746d3092215699b09eab11315c6adf8fb71eaa8f46c280b11b3cd448fd`.

[mapping.json](../evidence/native-startup/mapping.json) records the complete
registration table, all 17 startup API addresses, six task vtables, relevant
strings, and the decoded root-driver jump table. The
[mapping tool](../tools/native_startup_map.py) verifies the expected image hash,
table membership, adapter targets, and singleton/vtable connections.

The investigation used Xcode's `llvm-objdump` for x86 disassembly, the earlier
PE and adapter validation output, and small Python byte/table inspection tools.
[native_trace.py](../tools/native_trace.py) selects ranges or literal-address
references from the disassembly; it does not infer complete function boundaries.
Known jump tables were read as data instead of interpreting their bytes as
instructions. Linear disassembly elsewhere can still contain embedded data.

From the project root:

```sh
mkdir -p research/evidence/native-startup
xcrun llvm-objdump -d --no-show-raw-insn \
  research/experiments/steamstub/witch.original.exe.unpacked.exe \
  > research/evidence/native-startup/text-disassembly.txt
python3 research/tools/native_startup_map.py
```

The full listing is a navigation artifact. Smaller, hash-labelled excerpts are
linked below, with their exact intervals in
[witness-ranges.json](../evidence/native-startup/witness-ranges.json).

## Root selection, package loading and registration

The startup path at `0x470d11` passes the literal `root` to `0x43eae0`, which
copies it into the owner object's name field at `+0x7c`. The owner update routine
at `0x43ecf0` has a branch calling the root driver at `0x43fc90`.

That driver creates a script context at owner `+0x118`, passing a buffer at
owner `+0x2ba8` and size `0x10000`. It then passes registration table
`0x959c78` to `0x560860`. The registration loop reads eight-byte name/adapter
pairs until a null name, creates zero-upvalue C closures, and assigns them into
the Lua globals table (pseudo-index `-10001`). This establishes actual static
registration code for the previously candidate pointer table.

There are **65 entries** in this table, ending at `0x959e80`. All **35 native
names referenced by `root.lub`**, including its **17 startup/title/menu names**,
are present. Presence in the table does not imply every function is used during
startup; it also includes later mission and result operations.

The driver passes the selected name to `0x442120`, which formats the logical
resource name `script\%s.dpk`. After the resource readiness check, functions
`0x4409b0` and `0x440a30` use `%s.lub` to obtain a member pointer and length.
`0x560de0` loads that byte buffer and invokes the loaded chunk. The earlier
container inspection found `root.dpk` inside `stable/script/root.cpr` and the
exact `root.lub` member inside that DPK. These are logical resource lookups;
the native format string does not imply a loose `root.dpk` file on disk.

Evidence: [root driver](../evidence/native-startup/root-driver.txt),
[loader and registration](../evidence/native-startup/script-loader.txt),
[original extraction](../evidence/startup/extraction.json).
The complete RAT namespace resolution is still outside this trace.

## Who invokes `init` and `main`

The root driver uses state at owner `+0x108`. Decoding its two-stage jump table
gives the relevant paths:

| Native state | Action |
| --- | --- |
| `0` / `0x16b` | Create/register context, request package, wait for resource readiness |
| `0x17c → 0x17d → 0x17e → 0x17f` | Advance to initialization |
| `0x17f` | Look up `init` through `0x560f20`, then pump its context |
| `0x188` | Continue pumping `init` while it is pending |
| `0x18b → 0x18c → 0x18d → 0x18e` | Advance to `main` |
| `0x18e` | Look up `main`, then pump its context |
| `0x19b` | Continue pumping `main`; when it completes, prepare another `main` invocation |

`0x560f20` creates a Lua thread from the context's parent state, looks up the
requested global, and prepares the runner. `0x5608c0` performs execution/resume.
The driver can start another `main` in the same native call when the preceding
one finishes; it returns when an operation remains pending. This explains why
the Lua `main()` performs only one dispatch and contains no outer loop.

There is also a restart path gated by owner byte `+0x6145a`. It clears that byte
as part of a word write, sets native global slot 1 (`owner+0x61470`) to 1, calls
a reset routine, and re-enters `init`. The Lua then clears slot 1 and chooses
the branch that skips logos/storage and one advertise sequence. Normal state
`0x17f` enters the init call directly, bypassing that restart assignment.
The value of slot 1 on an actual fresh launch has not been observed.

This is a static route to the shipped `root` script, stronger than identifying
its filename alone. The complete process-entry-to-first-update path and update
frequency have not been reconstructed.

## Exec/Wait contract

All inspected adapters pass the incoming Lua state plus a function address to
one of two shared bridges:

| Bridge | Behavior |
| --- | --- |
| `0x5607f0` | Find owning context, decode Lua arguments, call the target with `context+0x80`, marshal return values back to Lua |
| `0x560780` | Find owning context, decode arguments, store callback at `context+0xe28`, call Lua yield with zero results |

Both bridges search a 16-entry context registry, comparing the input state with
context fields `+0x74` and `+0x78`, then fall back to a current-context pointer.
The immediate bridge calls its target synchronously. It does not imply the
requested screen completes synchronously: the Exec targets enqueue native tasks.

The runner at `0x5608c0` uses `context+0xe24` as its execution state. After a Lua
resume, a stored callback moves it into the pending path. It invokes the callback
with `context+0x80`; a true result returns “pending” to its caller. A false result
loops back to Lua resume, clearing the callback and per-operation phase field.
Lua may immediately request another wait during that resume.

The functions at `0x575b40` and `0x575be0` are identified as Lua 5.0
`lua_resume` and `lua_yield` by their control flow, state changes, return values,
and matching coroutine error strings in the official
[Lua 5.0 implementation](https://www.lua.org/source/5.0/ldo.c.html).
These are inferred library names in a binary without symbols. In particular,
the yield routine marks the current call as yielded and returns `-1`.

This is cooperative script suspension/polling, not evidence of a Windows thread
sleep in the Wait bridge. Other services inside native operations may still use
threads or blocking I/O. Exact frame timing requires runtime observation.
Evidence: [scheduler and library routines](../evidence/native-startup/scheduler.txt).

## The six startup screen tasks

Each Exec callback obtains a lazily initialized task object and calls its vtable
slot `+0x20` with argument zero. For all six tasks this slot resolves to
`0x4745d0`, which enters common task scheduling through `0x40b670` and `0x40b5f0`.
The latter inserts the task into a shared list. These pointers were resolved
through the vtable assignments in the singleton getters, not guessed from names.

| Operation | Exec callback | Wait callback | Object getter | Vtable |
| --- | --- | --- | --- | --- |
| Company logo | `0x4787a0` | `0x47d3a0` | `0x43d410` | `0x82a1f4` |
| Memory-card check | `0x478810` | `0x47d460` | `0x43d8e0` | `0x82a2a4` |
| Advertise | `0x478750` | `0x47d310` | `0x43d140` | `0x82a220` |
| Title | `0x4788c0` | `0x47d750` | `0x43e530` | `0x82a144` |
| Character loading | `0x478790` | `0x47d370` | `0x43d320` | `0x82a2d0` |
| Title menu | `0x4788d0` | `0x47d790` | `0x43e620` | `0x82a170` |

Most Wait callbacks test the same task-status helper at `0x40b5c0`. If its
pending bits are set they return true. Otherwise they invoke vtable slot `+0x24`
(`0x4745e0`) and return false. That shared finish path adjusts task flags, invokes
slot `+0x28`, and continues into a common task routine. Full lifecycle semantics
of every flag and virtual slot have not been assigned.

`_WaitTitle` has an additional phase: its first callback stores `0x213` in the
per-operation phase field and returns true before checking the task on a later
poll. A replacement scheduler should preserve this behavior rather than treating
all waits as identical completion checks.

Evidence: [callbacks](../evidence/native-startup/startup-callbacks.txt),
[singleton getters](../evidence/native-startup/singleton-getters.txt),
[common job control](../evidence/native-startup/job-control.txt).
All adapter, callback and bridge addresses for the 17 names are in
[startup-api.md](../evidence/native-startup/startup-api.md).

## State and resource details

`_GetGlobalValue` and `_SetGlobalValue` are switch-based accessors over different
owner/save-related fields, not a single plain array. Slot 1 maps to
`owner+0x61470`, connecting the restart code to Lua's branch. Slot 0 reads a byte
at preferred VA `0x134bc40` and substitutes 1 when it is zero. Setter branches
also call other native routines, so replacing these functions with a dictionary
would omit side effects.

`_GetTitleStatus` obtains a native task status through `0x4999f0`, which maps
task field `+0x94` values 1 and 2 to results 1 and 2 and returns zero otherwise.
`_GetTitleMenuStatus` calls `0x49a560`; `_GetLatestStageSelectNo` calls
`0x4c3eb0`. The Lua menu-status routing remains as documented previously.
Evidence: [state accessors](../evidence/native-startup/state-values.txt).

The task code also references `data\movie\witchLOGO.ssu` for title presentation
and `data\movie\advertise.ssu` for the advertise sequence. Native debug labels
include `TaskTitle`, `TaskCompanyLogo`, `TaskAdvertise` and `TaskMemoryCardCheck`.
These are direct strings/call arguments; the SSU contents, resource resolution,
and visible presentation have not been established by this mapping.
Evidence: [screen entrypoints](../evidence/native-startup/screen-entrypoints.txt).

## What this changes for planning

We can now describe the startup boundary concretely: logical archive/member
loading, Lua 5.0-compatible execution, native registration, coroutine suspension,
argument/results marshalling, task scheduling, and six screen operations. Reusing
the Lua would require preserving these contracts as well as its control flow.

This mapping pass reaches the screen task and resource boundary. It does not
map the complete renderer, input/audio implementations, legacy PhysX startup,
Steam initialization, or every prerequisite before the first root update.
No implementation architecture is selected by these results.

The next bounded static investigation is the first logo/title task's resource
loading and lifecycle: determine how its SSU/material/texture data becomes a
screen, identify the input and completion conditions, and follow actual platform
calls. A runtime baseline would then check the inferred sequence and timing.
