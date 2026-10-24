# Jump transition timing and authored landing recovery

This note describes the generic Runner/scripted jump. The later complete hardware
trace proves that the physical evasive leap uses a different state and motion
family; see [player-evasive-leap.md](player-evasive-leap.md). The previous native
A-button mapping to this generic jump was reconstruction policy, not original
controller behavior.

The original Runner jump-start waits for the start clip to finish before entering
physical flight. The reconstructed landing previously waited for the entire
landing clip, missing an earlier authored cancellation event. The latter adds
about half a second of unnecessary recovery lock for the ordinary jump.

## Start and flight

`0x6649c0` selects `(request.motion & 15) | 0x2a0`, forces a fresh motion and uses
zero blend duration. The native motion table maps slots 672/673 to
`@dash_jump_s` / `@jump_s`. `0x6649f0` reads the active controller's completion
predicate `0x41eea0`, which returns clock byte +0x12. `0x41f0a0` raises that flag
when the frame clock reaches the clip duration. There is no named event or
fractional threshold in this start-to-flight transition.

Motion initialization `0x41f2f0` reads the authored rate through `0x41dac0`
(header +12) and initializes both base/effective rates. The original start
therefore does not inherit a reduced analog movement rate from walking. Actual
PW0 compressed motion metadata is:

| Clip | Frames | Rate | Duration |
| --- | ---: | ---: | ---: |
| `pw0_jump_s` | 22 | 60 | 0.366667 s |
| `pw0_dash_jump_s` | 11 | 60 | 0.183333 s |
| `pw0_jump_m` | 85 | 60 | 1.416667 s |
| `pw0_dash_jump_m` | 90 | 60 | 1.500000 s |
| `pw0_jump_e` / `pw0_dash_jump_e` | 44 | 60 | 0.733333 s |

`0x664fb0` captures the preceding world-space movement delta divided by the
current timestep when constructing the jump request. Explicit jump supplies
vertical velocity 4.2; support loss supplies zero. JumpStart retains that request
velocity. On successful start-clip completion, `0x6649f0` copies the request into
StateJump; its support-loss branch instead clears the vertical component.
`0x6644b0` begins trapezoidal gravity integration only in the flight state.

JumpStart's update itself calls the grounded contact helper `0x6030d0` and the
posture helper `0x668360`; unlike walking `0x664fb0`, it contains no explicit
model-root displacement integration. The latter helper adjusts posture, not
actor translation. The generic ownership of model-root displacement during
start remains a separate source comparison; preserving captured horizontal
velocity must not be confused with replacing it by the final start-clip root
sample. No production change was made for that remaining question.

## Landing cancellation event

Landing entry `0x664800` selects the ordinary landing family, or hard landing
when impact velocity crosses its separate threshold. Update `0x664900` exits
on either clip completion or `0x5f8300`. The second predicate checks the model's
motion-parameter channel and a latched event flag, not current stick movement.

The event chain is explicit:

1. `0x5f7c70` binds event channel 4 to the receiver at motion owner +0xb0.
2. The channel accessor `0x41fd40` selects the section at parameter header +20
   and checks numeric section tag `0x43414e30` (`CAN0` in native tag notation).
3. Receiver entry `0x5f85c0` sets its byte +16; reset `0x5f7f40` clears it.
   Consequently the owner-level latch is +0xc0.
4. `0x5f8300` checks the presence of the cancellation section through
   `0x420150` and returns that latch.
5. `0x5f9b10` obtains the active animation frame clock and passes it to
   `0x6ad4c0`. The event dispatcher `0x6acea0` fires on a frame crossing
   `previous < event_frame <= current` and invokes the registered receiver.
6. `0x664900` returns to the default state once the latch is observed, before
   the full landing clip is exhausted.

The original `model.pw0.motionparam.pw0_jump_e` and
`model.pw0.motionparam.pw0_dash_jump_e` USER payloads both contain one
`CANCEL` record at **frame 13**. Thus ordinary landing can leave recovery at
**13/60 = 0.216667 seconds**, plus the actor/model update scheduling boundary,
rather than waiting 44/60 seconds. This is an authored state exit, not a new
movement-cancellation heuristic. Whether the following default state chooses
idle or locomotion belongs to its normal request handling.

The actual raw payloads have `FPA0` bytes at offset 0. The cancellation section
is at file offset 108, with count 1 at section +4 and an absolute file-relative
record-table offset 120 at section +8. Each record is 36 bytes: a 32-byte name
field followed by its signed frame at +32. Accessor `0x41fe30`, reached through
the event adapter `0x6b5290`, confirms that stride and frame offset. The native
end-frame accessor returns -1: entry latches eligibility until motion reset.
A decoder should bound the selected section and table, accept an absent
cancellation section, and reject malformed present records without fabricating
a timing default.

The selected support census confirms one cancellation marker per selected clip:

| Motion family | Cancellation frame |
| --- | ---: |
| Stand, walk, dash, stationary/running flight | 0 |
| Stationary jump start | 12 |
| Running jump start | 7 |
| Stationary/running landing | 13 |
| Hard landing (`largeland`) | 42 |

Hard landing therefore has its own authored recovery marker at 42/60 = 0.7
seconds; it must not inherit the ordinary landing's marker. The ignored
`jump-cancellation-census.json` records original section offsets and counts.

The start parameter files also contain cancellation events: frame 12 for
stationary start and frame 7 for running start. They do **not** replace the
start-to-flight completion condition: the traced JumpStart update does not read
this latch. Keep state-specific use of the same event channel explicit.

`0x5ff410`, called while landing continues, has no stick-intent branch or
walk-state restoration. It can send a motion-related actor message, then runs
grounded collision and requests StateJump on support loss. It does not justify
an arbitrary movement-based early recovery exit.

## Evidence and scope

Read existing passes 74–84, earlier clock/event exports, and one bounded
`pass-85-landing-input-exit`. Small additional disassemblies verify event receiver
and record-accessor slots without broad decompilation. The ignored diagnostic
`jump-parameters-probe.cpp` loads PW0 support through the production content
owner and dumps only selected jump parameter publications under
`research/evidence/reconstruction/actor/`. All retained engine-domain storage
returns to zero live bytes. No production edits or timing constants were made
by this source investigation.

The ordinary hardware-input-to-Runner request chain remains bounded as noted in
[player input policy](player-input-locomotion-policy.md). This evidence proves
these Runner state transitions and authored motion events; it does not label
untraced dodge control as ordinary jump control.

## Implemented correction and timing verification

`MotionCancellation` validates the retained FPA0 cancellation section and all
fixed-stride records, then prepares the first nonnegative marker frame. The
player actor converts that frame using the corresponding authored clip rate.
The marker becomes eligible as the forward clip clock reaches it; changing
motion resets that clock. This reproduces the latched cancellation predicate
needed by landing without introducing a general animation event dispatcher.
Only the landing state consumes this predicate. Jump-start still waits for clip
completion despite its earlier cancellation marker. No gravity, jump impulse,
clip rate or movement-speed constants were tuned.

An actual-content production-renderer probe ran at a 3456 × 1944 scene canvas,
with GPU command completion on every frame and the application's wall-driven
`min(wall_delta, 0.1)` simulation policy. Before the fix, 180 frames took 3.220
wall seconds and advanced 3.201 simulation seconds; the difference is the final
rendered frame. Mean render/completion time was 16.31 ms, maximum 46.37 ms, and
mean scene update was 1.04 ms. No frame reached the 100 ms cap. This run does not
include Cocoa event processing or display presentation, so it cannot rule out
an intermittent window-system stall. It does show that the reproducible landing
lock existed independently of a slow simulation clock.

The same probe after correction recorded request-to-air at about 0.356 seconds,
flight at about 0.857 seconds, and landing recovery at 0.233 seconds including
update scheduling, compared with 0.736 seconds before correction. Mean render
completion remained 16.29 ms, maximum 45.70 ms, with no capped frames. The first
probe used the original incorrect full-clip recovery; both logs remain under
ignored `research/evidence/reconstruction/traversal/` as
`jump-timing-native.log` and `jump-timing-corrected.log`, alongside the probe and
reproducible build command.

The existing original-root corpus now checks that the actual stationary start
retains its 22-frame completion boundary, that physical flight occurs, and that
landing returns within 13–15 simulation frames of contact instead of 44 frames.
Format contract cases cover missing channels, earliest-event selection, negative
frames, truncated records and unsupported tags. Resource tests and the updated
original-root corpus pass; engine-domain charges return to zero after teardown.
