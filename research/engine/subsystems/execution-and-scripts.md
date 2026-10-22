# Execution and script runtime

[Architecture](../ARCHITECTURE.md) · Updated October 22, 2026

Status: core script scheduling and selected task interfaces are statically
confirmed. [Bootstrap and task dispatch](bootstrap-and-tasks.md) now connects the
message loop and virtual update passes; shutdown and complete service ordering remain partial.
Research names below describe observed roles rather than recovered C++ symbols.

## Context and orchestration

| Object / field | Observed role | Evidence |
|---|---|---|
| Owner getter `0x432130` | Lazily initialized orchestration object | [Getter](../../evidence/native-decompilation/functions/00432130.c) |
| Owner `+0x7c` | Root script name | [Startup trace](../../reports/2026-10-21-native-startup-mapping.md) |
| Owner `+0xbc` | Mission name copied by `0x43ea50` | [Mission selector](../../evidence/native-decompilation/functions/0043ea50.c) |
| Owner `+0x108` | Root-driver state | [State witnesses](../../evidence/native-startup/root-driver.txt) |
| Owner `+0x118` | Embedded root script context | [Startup trace](../../reports/2026-10-21-native-startup-mapping.md) |
| Owner `+0xf48` | Embedded mission script context at registration | [Registration witnesses](../../evidence/script-corpus/registration-call-witnesses.txt) |
| Owner `+0x61410` | Cleared by StartMission; nonzero lets WaitMission complete | [Mission note](mission-and-stage-loading.md) |
| Context `+0x74` / `+0x78` | Parent Lua state / active coroutine | [Bridge trace](../../evidence/native-startup/scheduler.txt) |
| Context `+0x80` | Native operation argument/state block | [Bridge trace](../../evidence/native-startup/scheduler.txt) |
| Context `+0x84` | Operation phase; callback sees this as block `+4` | [Bridge trace](../../evidence/native-startup/scheduler.txt) |
| Context `+0xe24` | Pump state: resume / waiting / done | [Pump pseudocode](../../evidence/native-decompilation/functions/005608c0.c) |
| Context `+0xe28` | Pending native callback | [Wait bridge](../../evidence/native-decompilation/functions/00560780.c) |

Field names are analytical. Widths and offsets are observed where noted; this is
not a complete class declaration or proof of object ownership beyond embedded
storage. The mission name setter also clears the root name and owner `+0x114`;
the implications of those mode changes remain part of lifecycle mapping.

## Coroutine execution contract

```mermaid
sequenceDiagram
    participant Driver as Root/native driver
    participant Pump as Lua pump 0x5608c0
    participant Lua as Lua coroutine
    participant Bridge as Wait bridge 0x560780
    participant Callback as Native operation callback
    Driver->>Pump: Run / resume context
    Pump->>Lua: Resume Lua
    Lua->>Bridge: Call a yielding API
    Bridge->>Bridge: Decode arguments and store callback
    Bridge-->>Lua: Yield with zero results
    Lua-->>Pump: Suspended with callback set
    Pump->>Callback: Invoke with context + 0x80
    alt Callback returns nonzero
        Callback-->>Pump: Pending
        Pump-->>Driver: Pending
        Note over Driver,Pump: Later driver invocation can poll again; exact cadence unresolved
    else Callback returns zero
        Callback-->>Pump: Complete
        Pump->>Pump: Clear callback and operation phase
        Pump->>Lua: Resume within this pump invocation
    end
```

This is a static sequence extracted from the bridge/pump. It specifies no frame
rate or thread affinity. The pump can poll immediately after Lua yields, and can
resume Lua again in the same invocation when a callback completes.

The immediate bridge at `0x5607f0` decodes arguments, calls its target and marshals
results synchronously. An immediate API may enqueue work that outlives the call.
Which bridge an API uses is determined from the adapter bytes, not its name.

## Registration and Lua entry points

Root, mission and AI tables contain 65, 165 and 60 entries respectively. All use
`0x560860`, which installs zero-upvalue C closures as Lua globals. Addresses and
aliases are in the [native API index](../../evidence/script-corpus/NATIVE-API.md).

The root driver `0x43fc90` loads the root chunk, prepares `init`, then repeatedly
prepares/pumps `main`. Root states are decoded in the
[existing startup map](../../reports/2026-10-21-native-startup-mapping.md).
The verified root Lua and mission scripts supply their own control flow; the
native driver supplies resumption and scheduling.

## Screen-task boundary

The six mapped startup tasks use singleton getters and vtables. Their Exec
callbacks invoke slot `+0x20`, resolving to `0x4745d0`; this reaches common task
insertion through `0x40b670` and `0x40b5f0`. Most Wait callbacks poll `0x40b5c0`,
then invoke finish slot `+0x24` (`0x4745e0`) on completion.

`_WaitTitle` first records phase `0x213` and returns pending before the later
status check. This is an explicit phase boundary to preserve. The task dispatch passes, lifecycle states and update slots are now mapped in
[bootstrap and tasks](bootstrap-and-tasks.md). Concrete screen ownership, title/menu transitions and the shared text/sprite/movie
render path are now in [UI and screen tasks](ui-and-screen-tasks.md). The six vtables and exact addresses remain in the
[startup evidence](../../evidence/native-startup/mapping.json).

## Local work remaining

- `EXEC-01`: CRT/application/service/startup registration and normal shutdown
  are connected; exceptional exits and secondary destructors remain.
- `EXEC-02`: [mission lifecycle](mission-lifecycle.md) maps load/start, callbacks
  and status writers; continue through event producers, restart details and release.
- `EXEC-03`: concrete service/screen methods and task passes are connected;
  mode-dependent ordering and frame cadence remain unmeasured.
- `EXEC-04`: reconstruct native argument/result block types and ownership.

Continue through the [mission/resource map](mission-and-stage-loading.md).
