# Background resource worker

October 22, 2026. [Architecture](../ARCHITECTURE.md) ·
[Stage loading](mission-and-stage-loading.md).

## Identity and layout

The constructor `0x43a520` names this object **witch ScrBgThreadReal**. Its getter
is `0x43ad00`, cached at `0xcb3234`. Public wrappers add four bytes before entering
worker methods. **Offsets below are relative to that adjusted worker subobject**,
not the outer pointer returned by the getter.

| Offset | Static role |
|---|---|
| +0x30 | Critical-section wrapper vtable |
| +0x34 | Windows critical-section storage |
| +0x4c | Stop-request byte, cleared on worker exit |
| +0x50 | 128 callback pointers |
| +0x250 | 128 argument pointers |
| +0x450 | 128 owned-argument flags |
| +0x4d0 / +0x4d4 | Consumer / producer indices, wrap at 128 |
| +0x4d8 | 0x4000-byte argument arena backing storage |
| +0x44d8 | Allocator object managing that arena |

Constructor and [table witnesses](../../evidence/engine/pass-02/table-witnesses.json)
resolve lock slot `+4` to `0x415e50` (EnterCriticalSection) and unlock `+8` to
`0x4163e0` (LeaveCriticalSection). Worker virtual slot `+8` is `0x475880`.
The generic thread creator `0x4166e0` uses CreateThread with entry `0x416660`, which
invokes that slot. Exact startup ordering of this worker remains a separate trace.

## Submission, execution and completion

`0x475a10(callback, argument)` publishes a slot and advances the producer index.
`0x475a90(callback, source, size)` allocates/copies the argument first and marks
successful submissions as owned. `0x475b20` and `0x475b30` adapt the outer object.
`0x475990` initializes the arrays, indices and argument arena.

```mermaid
sequenceDiagram
    participant Caller as Stage/resource requester
    participant Queue as Worker queue
    participant Worker as Worker loop 0x475880
    participant Job as Submitted callback
    Caller->>Queue: Allocate/copy argument; publish callback and argument
    Worker->>Queue: Lock; read consumer slot; unlock
    alt Slot occupied
        Worker->>Job: Call callback(argument) at 0x475900
        Job-->>Worker: Return
        Worker->>Queue: Lock; clear callback; free owned argument; clear slot; unlock
        Worker->>Worker: Advance consumer index modulo 128
    else Slot empty
        Worker->>Worker: Sleep(16); retry until stop requested
    end
    Caller->>Queue: Count nonzero callback slots under lock
```

The callback executes outside the critical section, and its pointer stays nonzero
until it returns. Thus pending count includes queued **and running** jobs. The
count scans this shared queue without any stage identifier; it is not stage-local.
The stage-6 branch can enqueue additional work on the same queue.

[Worker instructions](../../evidence/engine/pass-02b/instructions/00475880.txt) ·
[Submission/count instructions](../../evidence/engine/pass-02/lifecycle-and-queue-witness.txt) ·
[Thread trampoline](../../evidence/engine/pass-02d/functions/00416660.c).

## Evidence corrections and failure boundaries

Ghidra's current pseudocode incorrectly presents the argument pointer as the
callback in `0x475880`, and calls a submission argument `unaff_retaddr`. The
instructions are decisive: `0x4758a5` loads callback from `+0x50`, `0x4758b0` loads
argument from `+0x250`, and `0x475900` invokes the former with the latter pushed.
Do not reuse the inferred pseudocode signature without correcting it.

The worker ignores the callback return value; queue completion itself communicates
that execution ended, not that every resource loaded successfully. Invalid size,
allocation failure and a full producer slot return -1 on submission. The inspected
full-slot branch returns without the normal critical-section release, and the
copied-argument failure path has no visible free. These are static exceptional-path
observations; their runtime reachability and wider recovery have not been tested.

The copied-argument owned flag is set after the inner publication helper returns.
Preserve this ordering in the map; do not assume publication/ownership is one
atomic operation. Cancellation, producer synchronization and safe shutdown with
pending jobs remain unresolved.
