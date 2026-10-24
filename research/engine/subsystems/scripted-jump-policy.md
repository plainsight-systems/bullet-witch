# Scripted Runner jump policy

This is the accepted request route, separate from hardware Dodger input. Source
callbacks are captured in passes75/81/85. The physical controller retains its
existing shape query and bounded simulation step; the game policy owns phases
and source velocity equations, not a second animation clock or physics backend.
The established Unreal CharacterMovement comparison remains documented in
[player collision](player-collision-boundary.md): movement integration precedes
contact feedback, with no invented pose-root displacement in a ballistic phase.

`ScriptedJumpPolicy` accepts the original motion word and captured world velocity.
The walk request664fb0 obtains X/Z from preceding displacement divided by its
timestep and supplies4.2 Y for an explicit scripted jump. That admission belongs
to the script/controller caller; this policy does not bind a button or assume a
walk/run threshold. Actual ordinary families use low nibble0/1; raised families
use the source directional low bits. Catalog preflight must reject absent slots.

| Phase | Source / implemented contract |
|---|---|
| Start |6649c0 selects low nibble OR672, **frame1**, fresh, zero blend.6649f0 checks current completion first and copies captured velocity unchanged to flight. Otherwise grounded support loss copies capture with Y=0. It ignores the start CAN0 marker. While still starting,6030d0 contact and668360 posture run; no5ff410 root message is issued. |
| Flight |664480 selects low nibble OR656, frame0, blend.3.6644b0 computes newVy=oldVy−gravity*dt, Y displacement=(oldVy+newVy)*dt*.5 and X/Z=v*dt, then6026c0. Without support, resolved X/Z displacement/dt feeds velocity with factor4*dt. Vertical velocity is retained. Clip completion alone does not select fall_l or land. Source weapon-raised posture/camera orientation is a separate caller obligation. |
| Contact |Strict impactVy<−.3*gravity enters landing; shallower support returns default owner directly.664800 selects hard landing704 frame0/.3 only when impactVy<−gravity. Otherwise low nibble OR688 starts **frame1**, zero blend. A raised family with motionword<704 collapses to family9 and aligns to the camera. |
| Landing |664900 exits on completion first, then CAN0 preserving the exited slot. Otherwise5ff410 performs source root/contact. Support loss creates fresh default StateJump (family0, zero velocity), rather than copying the landed impulse. |

Positive source offsets are native frames:5e47a0→41f2f0→41f200 forwards the value
unchanged and clamps against raw motion frame bounds. `ActionMotionStart::frames`
converts at prepared clip FPS in the actor adapter. This corrects the earlier
zero-start/full22-frame assumption without confusing frame1 with second1.

The policy never mutates a clock. The controller preflights the returned slot,
applies the plan once, and remains sole owner of each simulation/animation tick.
Pause calls neither phase nor physical advance. Costume changes retain policy,
captured velocity and current clocks. Start/flight do not also consume pose root;
landing may consume it under the original5ff410 predicate. Interrupt releases
phase ownership without clearing the retained pose into bind stance.

Focused in-process witnesses cover simultaneous completion/support loss,
normal/hard/shallow impact thresholds, raised-family landing, cancellation
precedence, lost-support defaults, trapezoidal integration, horizontal contact
feedback and rejected zero-dt division. Actual-script/controller integration and
native acceptance are separate from these policy witnesses.


## Integrated controller verification

The physical player owner now admits explicit generic family0/1 requests,
preflights reachable phase bindings before replacing body ownership, and keeps
that fixed policy through pause and costume changes. It applies captured
velocity only in flight, preserves the source X/Z contact feedback, and uses
ordinary root/contact during landing. Original-root startup integration verifies
no takeoff root displacement, a positive physical apex and completed landing
against the actual stage geometry. Default support loss selects656 instead of
retaining the unrelated Dodger fall_l transition. Raised families are explicitly
rejected at this boundary until the source posture/camera owner is integrated;
playing their pose is not equivalent to implementing that movement consumer.

The current actual-content run also covers ordinary damage retaining ownership
against held movement, completion releasing it, absent sustained motion retaining
the previous pose, and common magic acquiring/releasing its explicit scope once.
These checks do not create health, damage, targets, projectiles or spell effects.


The offscreen native Metal run `actor/scripted-native/run.log` now exercises the
production scene/controller at authored checkpoints0/10/20. All three enter
physical flight and return to support, then present ordinary damage, recover,
pause with held movement/look, resume and walk. Each514-update run checks that
no domain allocation or release occurs during advance; cancellation leaves zero
resident device resources. Captures of checkpoint0 takeoff/airborne and
checkpoint20 damage were inspected directly. These are actual stage/assets and
production rendering, with explicitly injected already-accepted action requests;
they do not assert scripted mission dispatch or original-game visual parity.
The user's live game was not controlled or restarted.

## Captured raised direction

State constructors `5fec70` (flight), `663fc0` (start) and `663ec0` (landing)
initialize the retained direction at state+60 to (0,0,1,0). This is a fallback,
not a per-frame camera-facing replacement. Accepted Walk requests in `664fb0`
build a horizontal basis from Runner+28's anchor forward, invert that basis,
and transform Runner+50's desired direction into it before writing state+60.
The resulting camera-relative direction survives start-to-flight (`6649f0`)
and flight-to-landing (`6644b0`) copies. While raised, flight transforms this
retained direction by the current `613ad0` camera basis and passes it to
`667cc0`; while not raised it restores the retained vector to +Z. An explicit
accepted request therefore needs the captured camera-relative direction. The
fresh default support-loss state may use its constructor's +Z.
