# Actor, model and root interval order

This is a source-order audit for the ordinary Alicia hierarchy. It establishes
an integration mismatch to resolve, not a new timing algorithm or Windows
runtime parity claim.

## Proven call order

| Phase | Source evidence | Consequence |
|---|---|---|
| Parent actor update | Game45f070 calls hierarchy45ce80/44f650. The walker invokes object+8 before recursing through its children. Actor602b70 calls65a570/568930/568960. | Actor state and prepared component messages run before its model child. |
| State/component movement |568960 invokes current-state update, then578850 for queued message/component pairs, then pending-state exit/install/entry. Runner664be0 reads model+4a80. | Root consumption reads a retained matrix, not a newly sampled interval in that handler. |
| Child model clock | Model5e37f0 begins56b850, which publishes the model transform through42bb10 and calls42aec0(dt). The latter calls41e440 for admitted controller chains. | Motion clocks advance after the parent movement pass. |
| Root interval publication | Model virtual+28 is5e5280. It gives only the current body ring entry the model+4a80 destination through41f840, then calls428ab0. |41f840 only stores a destination pointer;428ab0 samples previous/current root transforms and writes their relative matrix through422460. |
| Pose and events | Model virtual+2c5e5220 evaluates423d20, retires finished tracks423b40 and invokes postpose consumers.5e37f0 then calls body and upper5f9b10. | The pose and events describe the just-advanced model interval; movement has already run. |
| Descendants |44f650 recurses after model update. Weapon attachment56b610 reads the owning model's bone. | Attachments see the resulting pose. |

The parent relationship is explicit in actor constructor `5fe070`: actor+1210
receives the model; model+10 is inserted into actor+40's child list and
model+90 points back to the actor. This is not inferred from naming alone.
The actor's model wrapper inherits model update `5e37f0`; a specialized wrapper
`4f6530` calls it before its additional simulation work.

## Retained interval and first tick

For this ordinary hierarchy, parent movement consumes the interval published
by the preceding child-model update. Advancing the current model interval and
then applying that interval's root during the same parent update changes the
source phase. Motion selection also changes controller state in the parent
pass, while the retained root matrix is owned by model publication; a new
selection is not proof that the retained matrix already belongs to that clip.
The model constructors initialize this retained matrix to identity (machine
stores5e0181..5e01b9 and5e05d1..5e0609). The ordinary selection path
5e4760/5f9290/5e47a0 resets controller clocks, not this retained matrix.
Body ring vtable850ce4 uses5e1d80/427060 to unlink the reused controller and
5e1990/425400 to insert it; neither clears the model interval. A fresh model's
first parent update therefore consumes identity, and a selection preserves the
preceding published interval until the subsequent model update.

The cached interval is raw bone-zero relative motion. Parent664be0 queries the
**current** selected controller's Waist_direction through5e2060, flips its
forward Z and reconstructs the reference basis, then transforms the cached
translation through that basis and the model world transform. Retaining an
already-reference-transformed delta would also change behavior when selection
changes the live reference. Keep the raw matrix and resolve its reference at
parent consumption. The cached orientation remains part of that raw matrix.

`5ff410` may suppress root dispatch when the selected controller is both
repeating and complete; it still performs contact. Other state handlers have
early-return and root-before-transition contracts. Those gates remain separate
from the model clock phase. Scripted flight integrates its stored velocity
and does not consume the ordinary root interval.

## Complete simulation subdivisions

The controller now executes each consumed subdivision as one complete source
parent/model pair: state/contact, deferred state request, child clock and raw
root interval, corrected model pose/secondary/attachments, current-owner events,
then controller retirement. The next parent can observe cancellation, visibility,
facial and sound event state from that completed model update. Retained GPU
buffers are submitted once after the outer update; submitting those buffers no
longer determines when simulation events become visible. Raw clip review uses
the same model completion order without physical movement.

The prior code advanced multiple model clocks while deferring pose/events until
the outer frame ended. A landing cancellation crossed early in a 50 ms frame was
therefore invisible to later parent updates; a replacement could reset its
undispatched timeline. `complete_model_step` and `publish_completed_pose`
separate these responsibilities explicitly. No second clock advance occurs in
either operation. Paused zero-time publication consumes no new event interval.

## Bounded time and retained interval duration

The old min(remaining, double 1/60) loop created an 8.69e-10 second second step from
a nominal float 60 Hz host interval. That second parent consumed a full retained
root interval and divided its displacement by the numerical tail. The correction
adopts the complete balancing rule from pinned Unreal revision
`f45729c0b6f097038e72b52d366a62baeca3275b`,
`CharacterMovementComponent.cpp::GetSimulationTimeStep`: above the maximum,
choose min(maximum, remaining/2); otherwise consume the remainder. The maximum
uses the source/host float 60 Hz representation promoted to double. The final pair
is balanced instead of leaving a tiny tail. All accepted elapsed time is
consumed, with no loop epsilon or velocity clamp. The existing 0.25 second input
bound makes iteration count bounded. Unlike Unreal's minimum-tick/iteration
fallback, this bounded adapter never rounds elapsed upward or discards time.
The manual review phase-edge splitter uses the same rule.

Balanced subdivisions can still have different widths. Each raw root matrix now
retains its producer duration across selection and appearance changes. The
parent still consumes the preceding interval, preserving source phase, but
velocity derived from authored displacement divides by that interval's duration,
not the current consumer duration. Fresh/reset identity carries duration zero
and velocity zero. This duration association is an explicit reconstruction
adaptation for variable host/subdivision widths, not an additional recovered
field in the original executable. Ballistic integration and contact feedback
continue to use their actual current simulation duration.

Evidence is retained under native passes05b/05c/05d,94-evasive-root-basis,
111-animation-controller-chains and112-animation-clocks. Relevant exact
functions:44f650,5fe070,602b70,65a570,568930,568960,664be0,56b850,42aec0,
41e440,5e5280,41f840,428ab0,422460,5e5220,5e37f0.

## Regression evidence

The actual-content actor fixture checks identity and zero producer duration
before the first model tick, nonzero retained walk movement after a tick,
preservation of its raw matrix and duration across selection of a different
authored reference, and unchanged clock/pending motion during a paused appearance
candidate.

The scheduling regression then drives the authored evade until its landing is
just before the frame-25 cancellation marker. A float 50 ms host update crosses
the marker and returns to grounded control within that same update. This checks
that the next parent observes the completed preceding model's cancellation,
rather than waiting for GPU publication at the end of the host frame.

A second actual-content fixture compares two independent retained scenes: one
receives 120 mixed host frames (float 60/30 Hz, 16.8/31 ms jitter, 50/100 ms long
frames); the other receives their individual balanced intervals. Body binding,
unwrapped clock, movement phase, position and velocity agree after every host
frame. The actual prepared audio receivers emit the same nonempty ordered
bank/cue sequence and consume the same source random draws. The batched scene
submits exactly 120 pose updates, while the individual scene submits more:
model completion does not require an intermediate GPU submission. Each observer
has its own draw list, sharing only immutable resource metadata, so one scene
cannot clear or replace the other's retained geometry.

`runtime-tests` and the complete original-root corpus passed after these changes;
`scheduling-unit.log`, `scheduling-batch-build.log` and
`scheduling-batch-corpus.log` retain the results under
`research/evidence/reconstruction/interactive-animation/`. The corpus finishes
with zero live tracked memory. These are CPU/source-event contracts using actual
assets and controlled rendering/audio outputs, not a claim of Windows visual or
audio timing parity. Native review and the final combined gate are recorded by
the integration owner separately.
