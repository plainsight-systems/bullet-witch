# Raised aiming and posture

This records the implemented Runner/Shooter source contracts and their native
review evidence. Raised state is a retained source lifetime, not an arbitrary
camera-look boolean. Generic jump families 8–11 require this posture capability.

## Original model modifier

Runner setup `668230` requests `Hipsbone`, `Spine`, `Spine1`, `Spine2`, `Spine3`,
`Shoots_direction` in that order. The first literal is exactly `Hipsbone` at
PE address `855ca4`. Resolver `56aab0 -> 428510 -> 41d850` compares names
case-sensitively in the `motion.bil` MBDB table; `5e4db0` skips missing names.
The installed Alicia table contains `HipsBone`, so that first request is absent:
the admitted chain is five entries, beginning at `Spine`. No capitalization
alias is introduced. `Waist_direction` remains the separate authored
reference reset. Constructor `5e1080` initializes a 0.25-second fade, weight 0,
and target (0,0,1). Before hierarchy, `5e3ba0` resets the reference, approaches
weight 0/1 by dt/0.25 according to this frame's enable flag, then clears that
flag after consuming it. The target persists while fading out.

It composes the admitted chain frames through intervening parents. The
terminal frame's forward row supplies the current direction. `407c90` forms a
from-to quaternion toward the supplied target; `406ed0` spherically blends
identity toward it by `weight/(count-1)`. For each admitted chain bone except the terminal marker,
with original component basis R and shared delta D, it writes
`(R * D * inverse(R)) * local`. Translation is excluded from the conjugation.
All corrections use the original cached frames before any writeback. The final
hierarchy pass propagates these local corrections into arms, weapon attachment
and the secondary-motion input. The terminal marker itself is not corrected.

These operations belong in shared pose/math code. The game owns chain names,
enable/fade state and target meaning. Retaining final local matrices lets later
world overrides preserve aimed descendant relationships without decomposing a
possibly scaled matrix back into guessed SRT values.

Before design, inspected pinned Unreal
`f45729c0b6f097038e72b52d366a62baeca3275b`,
`AnimNode_LookAt.cpp::EvaluateSkeletalControl_AnyThread`: it resolves the target
in component space, solves a delta, and publishes ordered bone transforms.
That supports explicit spaces and ordered pose modification. Bullet Witch's
admitted-node distribution, quarter-second fade and local conjugation remain the
algorithm; Unreal target interpolation and clamp policy are not copied.
Core E.4 and performance MEM.9 were read in full for both design/implementation:
validate the complete borrowed request before publishing and prepare fixed
scratch/storage outside the frame loop.

## Target and actor turning

`668360` copies the aim anchor basis. With the player's camera present, it
selects body customization +1c0/+1c4/+1c8/+1cc by sign of camera forward Y and
crouch state. Actual PW0 values are +65, -90, +90, -45 degrees. Limits no greater
than +89.9 degrees enter the sine clamp; if abs(forwardY) exceeds abs(sin(limit)),
XZ is renormalized to sqrt(1-sin(limit)^2) and Y becomes sin(limit). The basis is
rebuilt around world up, then the retained actor+12a0 aim direction is transformed
through that basis and inverse model basis, normalized, and its Y is negated
for the pose modifier convention. This is not simply a yaw rotation.

`613ad0` constructs a horizontal camera basis with zero translation.
`667cc0` first transforms requested facing by the live authored reference query
`667600/5e2060` while raised. Its angular step is dt*15.707963943481445 radians,
with source near-antipodal threshold -0.9990000128746033, camera-side axis
selection while posture is active, and source up-axis fallback. The actor
adapter must preserve this distinction from its existing ordinary facing rule.

## Retained raised lifetime

`62ed80` posts the raised-state lifetime 6 seconds, or 0.75 seconds for weapon
class 3, when its accepted aiming condition holds. `62ebe0` decrements the
lifetime only while positive and the upper current slot is absent. `604540`
clamps it at zero and updates the associated enable flag. Upper action presence
therefore holds the lifetime; it is not decremented unconditionally. Input
acceptance remains separate from firing/ammunition success.

The signed turn-rate supplied to raised idle selection is processed input
message +54, produced by `5fc3f0` and consumed by `6667e0 -> 667000`.
For normal stick assignment it is right-stick X multiplied by
`((settingsA0b6d8 >> 18 & 127) + 25) * 0.019999999552965164`, with configured
horizontal inversion applied afterward. The stick-swap option changes the
source stick. Mouse X, when present, overrides it with
`mouseDelta / (101 - mouseSensitivity)`. It is neither camera angular velocity
nor radians per second. The helper receives the explicit sensitivity value;
no runtime setting is guessed from the PE's uninitialized globals.

## Implemented seam and validation

`aim_posture.hpp` separates target construction, retained raised lifetime,
quarter-second pose weight and source-facing stepping. The caller retains the
last target during fade-out and passes `AimChainStep` to `evaluate_layers`.
The pose owns final local matrix storage separately from sampled SRT, so
subsequent secondary world overrides do not silently revert aimed descendants.
The source maximum chain is eight entries; invalid ordering, indices, targets
or singular frames reject before published pose/skin changes. The consumed
root is excluded from the chain; the installed Alicia chain starts at Spine.

Quaternion interpolation and from-to rotation now live in `bw/math`; the old
pose blend delegates to the same implementation with its original threshold.
Shared orthonormal basis construction retains right-first normalization. No
new per-frame allocations or platform-specific math kernels were introduced.

In-process witnesses exercise the source lifetime hold, quarter-second fade,
authored standing/crouched clamp, processed turn rate, chain-space correction,
secondary descendant preservation, rejection rollback and allocation stability.
The actor now binds the chain and retained aim observation. Native production
Alicia/weapon captures exercise forward, upward and sideways targets, followed
by fade release. The inspected torso/arm/weapon response and terminal directions
match those supplied model-space targets. This verifies the reconstructed
consumer, not original-game visual parity.

The target's model argument is the original wrapper local basis at model+0xa0,
**before** the render half-turn. `668360` loads and inverts that matrix;
`5e37f0/56b850` separately publishes `rotationY(pi) * modelWorld` to rendering.
Ground Walk update `664fb0` runs selection, `667cc0` facing, `668360` target,
then root/contact. Requested facing is Runner+50: normalized movement while
moving, horizontal camera forward while raised idle. Its live reference comes
from `667600 -> 5e2060 -> 42a5b0`, the current body controller's first admitted
ordinary channel for Waist_direction, not the reset display pose or an endpoint.

## Authored marker magnitude

Actual raised idle exposed an incorrect port precondition: the terminal marker
is not exactly unit length after authored chain transforms. Source407c90
normalizes **only the sum of marker and target**, then returns their cross/dot
halfway quaternion without normalizing the marker or result. Source4091d0
retains nonunit quaternion magnitude in its squared-component matrix formula.
The pose now preserves that behavior; it rejects nonfinite/zero markers, not
valid authored scale. Exact opposite sums alone use409b70's reconstructed-up
fallback. The common math contract explicitly permits nonunit inputs.
A half-scale +Z marker aimed at +X witnesses terminal forward (.1,0,-.075),
distinguishing source arithmetic from silently normalized (.5,0,0). Actual
native raised-pose verification uses the production actor and Metal renderer.

The scaled-chain resource regression passes in `source-phase-resource.log`.
Fatal scoped clang-tidy passes for pose, skeletal-pose tests and the actual-content
corpus includer. The repeat native probe initially stopped at its shader-library path; after
using the existing movie.metallib it passed. The integration owner inspected
forward/up/side captures: torso, arms and attached weapon follow the supplied
target, and the posture fades out when released. `native-aim.log` records
terminal forward approximately(.0049,0,-.9995), up(.0042,-.4997,-.8656), and
side(-.4955,0,-.8680). These are native mechanism witnesses, not a claim of
pixel-matched original-game aiming.

The completed physical probe also verifies raised scripted families 8–11 with
a nondefault captured camera-relative direction. Family8 covers placements
0/10/20;9/10/11 cover placement0. Flight/landing, reaction recovery, pause and
camera checks pass without Domain allocation/release during advance. The
placement0 airborne captures for families10 and11 were visually inspected;
this does not claim garment surface intersections are removed.

## Raised-turn model magnitude

Actual installed right-turn motion exposed a second, separate scale assumption
in `raised_facing_step`: after a retained root interval, the model forward was
(0.920855343, 0.000000547249499, 0.388461918), length 0.9994384546.
The native debugger stopped specifically at validation of **current model
forward**; the requested/camera direction and reference were unit/rigid.
This was not the earlier pose-chain marker failure.

`667cc0` reads this model row directly and retains its magnitude through the
alignment dot product, axis selection and rotation. It also leaves the
reference-transformed target unnormalized during those decisions. Both chosen
paths then call `409b70`, which normalizes the final basis forward. The port
now follows that order, admitting finite nonzero magnitudes and normalizing
only its returned forward. It does not normalize the raw operands or enlarge
the unit tolerance. Camera/request validation remains unchanged.

The captured installed input is an in-process regression for the bounded
15-degree step and final unit direction. Separate scaled-current and scaled
reference witnesses distinguish the source snap comparison from premature
normalization. Source evidence is `pass-102-player-facing-contract/00667cc0`
and `pass-100-direction-reference/00409b70`; the exact native stack and input
are retained in `interactive-animation/aim-failure-debug.log`. Both C++ MCP
corpora were read again for design and implementation (E.4, MEM.9); this
correction changes no storage, clock or allocation policy. The existing pinned
Unreal comparison above remains applicable to explicit target spaces, while
these arithmetic choices come directly from the original source.

## Reopened visible aiming review

The user reported a contorted torso and a weapon pointing toward the camera.
Earlier mechanism images and finite-transform checks did not establish correct
visible aiming. This remains open during the current correction pass.

A separate execution witness runs the original `668360` instructions with the
same anchor/model inputs as the portable target contract. Equal level headings
produce target `(0,0,1)`, including the authored initial heading. Pitch ±0.4
produces `(0,∓0.3894183,0.921061)`; relative yaw and crouched downward clamp also
match the stated target transform. The original setup assigns actor+1218 to
Runner+28; `604a40` copies the authored basis directly into that anchor and
`630b40` rotates it directly. Camera offset collision changes position, not the
retained orientation. No opposite camera-forward convention is demonstrated.
Generated reproduction is `interactive-animation/aim_target_original_witness.py`
and its JSON output. This narrows the investigation; it does not establish that
the pose supplied to the modifier or its downstream publication is correct.

The publisher's [PC gameplay livestream at26:24](https://www.youtube.com/watch?v=XAkp-wx6zgY&t=1584s)
was inspected again during this pass. Its firing stance is strongly side-on and
leaned, with exposed left back/waist and the weapon held across the front toward
the target. Side-on torso appearance alone is therefore not a defect witness.
The corrected level native pose was inspected from both sides using only a
diagnostic camera offset: both hands grip the horizontal weapon coherently.
This resolves rear-view foreshortening for that pose. The subsequent steady and
held-fire pitch checks are recorded below; user review remains separate.


## Integrated aiming correction and diagnostic state

Two independently traced consumer boundaries were incorrect. Raised model
facing now reads the current clip's unmodified local `Waist_direction` matrix:
`667cc0 -> 667600 -> 5e2060 -> 42a5b0 -> 417bb0`. The displacement consumer
`664be0` additionally reverses the reference forward Z and rebuilds its basis;
that displacement-specific operation must not be shared with facing. With the
same original input sequence, raised facing now retains the camera heading
instead of turning approximately 180 degrees away.

Weapon publication also omitted a distinct model-render conversion. Activation
`615900` applies a local half-turn and constructs `5dfc60`. Its vtable850d68
update entry is `5e37b0`, whose instructions call `56b850`; the latter first
composes the wrapper and then applies the common render half-turn. Consequently
an attached weapon publishes `renderHalfTurn * (localHalfTurn * RightWeaponWorld)`.
The two half-turns cancel for this activation, but remain separate responsibilities
in the reconstruction. The previous single half-turn reversed the visible gun
relative to Alicia's hands. See [attachment evidence](alicia-animation-bindings.md).
No target-direction flip, per-weapon offset, body-pose retuning or new IK was added.

The integrated space/phase contract is:

| Boundary | Observed contract |
| --- | --- |
| Scene camera | The retained camera anchor rotation is supplied before the current camera update; a changing look input therefore has a one-update view/aim difference. A settled look has identical rotations. Shoulder collision changes translation only. |
| Physical model facing | Source wrapper local basis, before the render half-turn; raised facing reads the raw authored reference. |
| Target | Retained `(0,0,1)` through anchor and inverse wrapper local basis, normalized, then component Y reversed for the modifier convention. |
| Base/upper/additive state | Actual catalog identity and clock, masks and ordered layers are retained; a non-aiming airborne base cannot be diagnosed as steady raised idle merely because RT is held. |
| Pose modifier | Original five-entry chain, cached component bases and quarter-second fade; then hierarchy and secondary overrides. |
| Body publication | Source render half-turn times physical wrapper world. |
| Weapon publication | Activation local half-turn and common model-render half-turn, attached to the completed `RightWeapon` body bone, followed by independent weapon skeletal pose. |
| Visible bore | Installed weapon meshes are long along model X; inspected muzzle ends are positive X. Thus the rendered bore is measured from transformed model +X, not `Shoots_direction` +Z. The latter is a source pose-control marker and points opposite the rendered bore in this steady pose. |

Actual installed raised-idle chain and attachment records use constant AB7
SRT channels. An independent byte comparison matches the prepared constants
within 4.9e-10 printed precision. Executing original `417bb0` instructions on
those constants matches matrix reconstruction within 1.2e-7. Executing original
`5e3ba0` on the actual premodifier locals matches component matrices within
1.1e-6; the final native body publication matches this isolated source execution
within 7.7e-6, including world translation. These tests identify the actual
consumer inputs and downstream matrices; they do not substitute for rendered
pose review.

The production Stage input probe now separates steady raised look from its
longer held-fire/turn sequence. For all four weapon classes, the settled
up/down controls remain on body slot12 (`shoot_stand_l`), and the native side
views show coherent hand placement and muzzle elevation. At camera pitch
+41.67 degrees, the transformed model +X elevation is approximately +42.4;
at -41.67 it is approximately -40.9. The small offset is authored gun/hand
alignment, not a patched target. Native evidence is
`interactive-animation/aim-steady-after/class0-up-steady-side1.png`,
`class0-down-steady-side1.png`, and `class3-up-steady-side-1.png`.
The original-root corpus now independently builds all four weapon poses in the
completed body attachment frame and compares every published skin matrix,
catching the missing second conversion. It also checks raw facing and
displacement references remain distinct. Both corrected actor/controller files
and the extended corpus pass scoped fatal clang-tidy; the actual corpus passes.

The earlier suspicious up/down held-fire frames are preserved, not discarded.
They used body slot656 (`dash_jump_m`) after stationary turning falsely lost
support. The debugger captured the first failure at frame349: previous and
proposed XYZ were identical, while the retained root interval contained
Y=4.43987751e-16 and its derived vertical velocity was 2.66392641e-14. A shared
query incorrectly used positive velocity to suppress *grounded* floor support.
Original `6030d0` has no such gate. The query now receives an explicit grounded
or airborne mode from the active movement state; no tolerance, root clamp or
pose retuning was introduced. See [contact reconstruction](player-collision-boundary.md).

The exact original turn/fire sequence was replayed across all four weapon
classes after the correction, with no support-loss transitions. Raised up/down
held-fire snapshots remain on grounded body slot12 and their side views show
aligned hands and a muzzle following camera pitch. Shot additive motion explains
small differences from the settled raised-idle bore measurement above. Genuine
scripted ascent and landing remain covered separately, so support preservation
is not claimed for ascending flight. The new original-root regression executes
this same stationary raise/pitch/turn/fire sequence and requires grounded contact
throughout. Evidence is retained as `interactive-animation/aim-contact-fixed`,
`aim-contact-fixed-run.log`, and `grounded-contact-before-debug.log`; final coherent
build repetitions are recorded under `final-aim`. Overall user acceptance remains
pending after integrated automated/native verification.
