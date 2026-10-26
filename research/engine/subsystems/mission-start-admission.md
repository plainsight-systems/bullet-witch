# Mission start admission and scripted player placement

The current animation review scene does not satisfy the original mission-start
admission contract. The user explicitly authorized **only the initial position**
as the sole temporary shim for this slice. `src/game/mission/initial_placement_shim.hpp`
and `.cpp` isolate that exception by name. The function accepts only fresh
`cm01_00` / `st04_c`, rejects checkpoint restoration and all other identities,
and returns the original fresh `start()` placement `(-61.91, .65, -97.57)` with
yaw `-.28` degrees. It does not execute Lua or report world setup as ready.

The production checkpoint coordinate table was removed. Playerless static
inspection now requires an explicit caller-supplied pose; it has no hidden
One Shot Kill fallback. Independent authored coordinates in tests are fixtures,
not a second production spawn policy. This is a fixed-value compatibility
adapter, not a new camera/placement algorithm; the original startup audit below
is its provenance and removal plan.

Remove this module and its framework call when genuine mission admission can
execute the original player placement/direction commands against the live actor.
Full mission startup and checkpoint restoration remain incomplete; under this
specific user exception they do not block animation-slice closure. No other
ownership or lighting finding inherits this exception.

The shim's focused runtime checks cover the exact fresh pose plus rejection of
other mission/stage names and checkpoint restoration. The existing original-root
corpus constructs static inspection with an explicit fixture pose and finishes
with zero retained domain memory. Scoped fatal analysis passes the shim and its
callers; the final integrated lighting gate is recorded centrally when complete.

## Original ordered gates

The hash-bound Steam function `0x43eea0` retains the same Lua context from
`load()` through `start()`. The following is the post-load path; hexadecimal
state numbers are source provenance, not proposed engine enums.

| Source state/path | Required operation | Current owner and gap |
| --- | --- | --- |
| `load` coroutine completes; `600` through `0x25b` | Reset mission control fields and loader state | `MissionDriver` genuinely completes original `load()`, then stops at `awaiting_world_setup`. |
| `0x26e` | Wait for global input owner; `0x446320` resets world-placement manager; `0x4829c0` enables loaded table manager | Native input sampling exists, but mission gameplay input ownership and the runtime world-placement manager are not equivalent to the review controller. |
| `0x27b` → `0x480ea0` | Queue `0x480ec0` on the original setup task queue | Resource definitions are resident. The set-table actor construction task is absent. |
| `0x289` → `0x481040` | Wait until that queue is empty | Cannot be acknowledged merely because encoded/model resources loaded. |
| `0x291` | Request loading presentation exit through `0x4a75e0`, wait for `0x4a6b30` | Loading art and its exit mechanism exist; their completion must follow actual setup completion. |
| After `0x291` | Restore service/score state; enable cockpit; initialize callback and message owners; apply checkpoint/player state | Profile bytes and animation review state do not implement these runtime consumers. |
| Final preparation | Clear input-owner byte `+0xa`; set mission `+0x61462`; prepare unchanged `start()` coroutine | No production start admission should occur before the preceding owners are real. |

`0x446320` requires the input owner, resets its manager header, clears 256
placement work records and 32 auxiliary records, and activates two retained
resource identities. `0x4829c0` is only a write of one to manager byte `+0x44`;
it does not construct the actors. These two functions were checked directly in
the complete native disassembly, avoiding inferred pseudocode arguments.

`0x480ec0` visits both retained set-table slots. For every row in their `chara`
sections it calls the original actor factory `0x600ff0`, with the row's actor
identity and selected customization. The player branch installs additional
bindings. The installed enemy table contains **16 character rows**, alongside
58 group, 39 trap and five level rows. The primary mission table has three trap
rows. These are already decoded/resource-preloaded by the load pipeline; they
are not instantiated mission entities. The manually constructed Alicia review
actor cannot stand in for this complete queue.

Post-load callback initialization is also substantive: `0x482000` clears its
64 records and registers `clear` and `gameover`. `0x43c1e0` handles actual
checkpoint/player upgrades, health/magic, weapon and serialized world/callback
state where present. Calling neither while reporting their gate complete would
erase source behavior rather than defer it.

## What the unchanged script requests

`cm01_00.start()` first pauses pad input and hides the cockpit. Checkpoint zero
then calls `_SetBgObj` **before** selecting the player pose. The pose branches
are checkpoint ranges `[0,10)`, `[10,20)` and `[20,+∞)`; the actual coordinates
belong only to original Lua and independent test fixtures.

After placement, the script sets the magic mask, registers goal/checkpoint
traps, sets the goal target, makes the player/cockpit visible and manipulates
gameplay/pad pause, waits and fades. The fresh path also waits for an authored
message before resuming pad input and advancing the checkpoint. A placement
request therefore is neither evidence that `start()` completed nor permission
to override the later visibility/input gates for ordinary gameplay.

`_SetBgObj` (`0x47ba80`) dispatches the retained background definition through
`0x449640` / `0x449460` into `0x445370`. That creates manager-owned placement
records carrying the complete matrices and state words. Runtime construction
through `0x44a260` owns model/physics objects and their subsequent behavior.
The review scene already reproduces the 276 initial prop transforms, draw masks
and frozen PHIT contacts, but it has no corresponding dynamic/destruction
lifetime. A static snapshot is useful evidence; it is not complete execution of
this native request. See [stage scene presentation](stage-scene-presentation.md).

## Reusable placement boundary

The independently useful design is a typed, owned player-placement command
produced by the original Lua binding and consumed by the mission player owner:

1. Validate and copy the four numeric arguments into `ScriptedPlayerPose`.
   Positions retain source units; yaw is degrees. Do not map a mission name or
   checkpoint to C++ coordinates.
2. Associate the request with the live mission generation and an ordered
   command identity. A pending request cannot borrow Lua stack values.
3. Require a live player and world-placement consumer. Apply the complete
   authoritative transform and world-region placement hint atomically; reset
   dependent contact/camera state under an explicit teleport contract.
4. Acknowledge only after that consumer has accepted the command. Pending and
   unsupported work retain the coroutine and an exact diagnostic. Cancellation
   invalidates outstanding generation tokens before releasing the VM/world.
5. Test the actual binary chunk across the three checkpoint ranges, command
   ordering, rejected/nonfinite inputs, cancellation and retry. These tests
   cannot substitute for the missing production start gate.

Original `_SetPlayerPosition` (`0x47c470`) looks up the current player, supplies
the position to `0x446080`, builds a yaw matrix from degrees, and invokes actor
virtual `+0x1c` with the complete pose. It does not perform a checkpoint lookup
or special-case this mission. The current review-state restoration API is not a
mission teleport API and should not be reused without this lifecycle contract.

## Scope decision

Two honest routes remain:

- Implement genuine world startup: set-table entity registration/lifetime and
  construction queue, gameplay input/pause ownership, callback/checkpoint
  initialization and the required start consumers. Existing resource, Alicia,
  loading and static-scene components can be reused. NPC/entity behavior,
  dynamic prop lifetime and mission callbacks are additional production owners,
  so this is broader than a coordinate or Lua binding fix.
- Keep the existing animation review session explicitly separate while those
  owners are built. A generic capability-limited original coroutine can expose
  typed requests and stop at the first unsupported consumer, but must retain
  blocked mission status and every earlier ordered effect. It may not skip
  `_SetBgObj`, pretend setup completed, or silently restore gameplay input and
  visibility to harvest a spawn. This route does not resolve genuine startup.

No review-only extractor, fake completion callback, or production coordinates
were added during this audit. The next implementation choice depends on the
intended scope of the upcoming gameplay ownership/HUD work.

Evidence: original `cm01_00` binary and recovered text; engine passes
`pass-concept-launch`, `pass-14-placement-registration`,
`pass-50-background-presentation`, `pass-74-player-traversal`; full native
disassembly for `0x446320` and `0x4829c0`. The set-table counts are independently
recorded in [mission load consumers](mission-load-consumers.md). Generated and
recovered evidence remains ignored. This audit changes documentation only and
claims no new production or native verification.
