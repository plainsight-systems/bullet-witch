# Direction reference and displayed facing

The user reported Alicia turning to face backwards when evading during left/right
movement. The native production-path probe reproduces this at the initial camera
and after a 75-degree camera turn. Signed travel remains correct in all four
cardinal directions. A missing skeletal reference reset was identified, but
subsequent user review still reports forward/back facing reversal. Correct
travel does not establish correct displayed facing.

Runner setup `0x668230` constructs the pose modifier `0x5e1080`, resolves
`Waist_direction` into modifier +0x30, and sets +0x68 true. Its pre-hierarchy
callback `0x5e3ba0` caches the authored reference rotation, then calls `0x409b70`
on that bone's local matrix with forward (0,0,1) and up (0,1,0). The latter writes
an identity orientation/unit basis while preserving translation. This happens
before its optional upper-body aiming distribution; the orientation reset is
unconditional while +0x68 is enabled. Instructions `0x5e3c0f..0x5e3c31` and
hash-bound constants `0x6c1dc0`/`0x6c1de0` establish the exact arguments.

Root movement separately samples the original authored reference through
`0x664be0 -> 0x5e2060 -> 0x42a5b0`. The reconstruction correctly used it for
travel but also retained its rotation in the displayed hierarchy. Left/right
clips contain +90/-90-degree reference rotations. The missing reset changes
displayed orientation, but it does not by itself close the reported reversal.
The callback space and publication order are now verified below. Original visual
parity remains unconfirmed; input signs or replacement clips must not be guessed
from travel alone.

## Implementation contract

The shared animation evaluator accepts explicit local basis overrides, preserving
sampled translation and applying them after local blend/root locking but before
one hierarchy pass. Alicia supplies the identity basis for its named direction
reference. Root extraction continues reading the immutable original clip.
Overrides validate index, duplicate ownership and finite values before publishing
any pose. No gameplay bone names enter the engine API and no allocation occurs.

The same pose owner exposes a separate world-transform override boundary for the
recovered secondary-motion solver. It stages validated absolute bone transforms,
recomposes untouched descendants from their current local transforms, and rebuilds
skin matrices in prepared parent order. The caller supplies final absolute targets
and retains simulation/contact policy. All overrides in one batch publish together.

Pinned Unreal `f45729c0b6f097038e72b52d366a62baeca3275b`,
`BonePose.h::FCSPose::SafeSetCSBoneTransforms` was inspected. Its core contract
preserves child local relationships when replacing component transforms. We use
our already prepared hierarchy order and reusable staging buffers, avoiding its
per-call scratch-stack collection. This is an engine composition boundary, not
adoption of an unrelated character controller.

Design and implementation query both MCP corpora: I.1 makes local versus world
space and borrowed override lifetimes explicit; E.4 retains the previous valid
pose on rejection; MEM.9 reserves buffers during construction. Source evidence
is in ignored pass100, with a corrected seed-format retry recorded. Native
before captures/logs are under the traversal evidence directory. Verification
results are recorded after implementation; source identification alone is not
native acceptance.

## Verification

The corrected native probe repeats four directional movement-to-evade sequences
at two camera angles. All eight physical position, phase and actor-yaw witness
lines are identical before and after; only skeletal orientation changes. The
initial review overclaimed the visual result: source-reference identity and
signed travel are insufficient evidence of correct anatomical facing. The user
rejected forward/back transitions. Fresh before/start/middle captures and
shoulder/neck/eye world-position witnesses reproduce the remaining behavior at
both camera angles.
The shared pose tests cover post-blend local override, immutable root data,
parent/child world overrides, skin recomposition, invalid-batch rollback and no
runtime domain allocation. Original-root corpus checks each directional clip's
root travel independently from its displayed reference reset. Full required
verification is run with the concurrent shader work before review delivery.

## Remaining facing audit

The actor parent basis stays identity in the ordinary static-player path;
mutable heading lives in model-local space and rendering publishes the original
Y half-turn times composed model world. A single yaw is equivalent within this
boundary; no parent-yaw compensation is justified. Original XInput stick-up is
positive movement Z, matching the native GameController adapter. Native motion
slots 709/712/718/715 resolve the literal forward/back/left/right publications
without PW overrides. These checks reject two plausible explanations; neither
is a demonstrated correction. The pose callback and full transform composition checks below also match the
current implementation. Actual rendered transitions still require comparison
with the original game before calling the remaining behavior faithful.


## Verified local pose and render publication

The normal player model factory `0x5fe070` constructs `0x5e0830`, whose vtable
`0x850d9c` has update `0x5e37f0` at +8, pose evaluation `0x5e5220` at +0x2c,
and modifier dispatch `0x5e1df0` at +0x30. This actual class selection matters:
other model classes have different publication callbacks.

`0x5e5220` calls `0x423d20`, which converts blended local QTS values into the
runtime +0xec matrix array. Its intervening calls `0x423b40`, `0x423c70` and
`0x423cb0` clean up playback/event lists; they do not compose bone hierarchy.
Instructions `0x5e525d..0x5e5270` fetch runtime +0xec through
`0x428600(runtime) +0x4c` and pass that same pointer to virtual +0x30.
`0x5e1df0` forwards it directly to each modifier. Therefore `0x5e3ba0` receives
local bone matrices before hierarchy composition, not component-space or skin
matrices. Its own parent-index walks construct temporary component chains for
optional aiming, independently corroborating that space.

`0x5e37f0` first calls `0x56b850`, publishing `rotationY(pi) * modelWorld` into
the runtime global matrix; it then invokes pose evaluation and modifiers.
The later `0x4268d0` render snapshot copies that global matrix and the local bone
array separately. During draw preparation, `0x427220 -> 0x422d30` composes each
local bone with its parent and then premultiplies each component pose by the
loaded inverse bind. The global matrix remains separate. In row-vector form,
the resulting character transform is
`vertex * inverseBind * componentPose * rotationY(pi) * modelWorld`.
This agrees with the reconstruction's current ordering and supplies no evidence
for another half-turn or an inverse authored-parent-yaw correction.

## Bounded Dodger lifetime and playback check

Player construction `0x5fe970 -> 0x5ff170` first creates Runner, attaches its
modifier with `0x668230`, then registers Dodger with `0x5fd4e0 -> 0x689b80`.
The latter initializes component metadata and the `0x68b420` message registry;
it does not replace the model or modify the Runner-owned reference modifier.
The examined start, airborne and landing callbacks continue using actor +0x1210.
No reference-reset disable or re-enable appears in this bounded path. This is
not a claim that every write in the executable has been exhaustively classified.
The start callback's `0x603aa0` helper changes collision-category bits, while
`0x604600` changes camera/postprocess mode; neither disables the pose modifier.

`0x68a600` selects its directional start slot with
`0x5e4760(slot, blend=0, fresh=1, start=0)`. Through `0x5f9290` and `0x5e47a0`,
this creates fresh playback at the beginning with positive unit rate. There is
no observed reversed playback or hidden initial-frame offset for these starts.
Together with the earlier native-slot/resource and hardware-axis checks, this
bounded audit has not demonstrated a further production correction. A visible
lean, model heading and signed travel must be recorded separately; source
agreement alone does not establish the original game's visual result.

Ignored evidence: `research/evidence/engine/pass-102-player-facing-contract/`,
`pass-103-pose-phase/` and `pass-104-dodger-modifier-lifetime/`, plus existing
passes 05e, 06m, 71, 89, 91, 95, 97 and 100. Pass104's requested address
`0x68a390` resolves to the containing landing function `0x68a370`; it is not a
separate constructor. PE vtable and slot reads use the hash-bound original image
identified by `research/evidence/reconstruction/actor/animation-coverage-bindings.json`.

## Backward run-to-evade ordering check

The later native review specifically rejects the visible turn when running
toward the camera and then evading. Rechecking existing passes 05e, 84, 89,
102 and 104 establishes the following narrower result, without another export:

- `0x68a600` selects a fresh start clip with zero blend and frame offset; it
  does not write model facing. `0x68a720` checks completion first. While the
  clip remains active, it rotates model-local forward toward the stored target
  through `0x6991f0`, writes that basis while retaining local translation,
  dispatches the root-movement message, then calls contact resolution.
- For pure backward input, `0x68a900` still selects the backward clip and a
  camera-aligned target. A preceding nonfiring run facing opposite the camera
  therefore requests a half-turn during the start. The recovered maximum
  angular speed is 25.1327419 radians/second: nominally 0.125 seconds for that
  half-turn, within the 0.25-second start clip. There is no aim/firing branch
  in these three Dodger handlers. The preceding Runner policy does branch on
  actor +0x126c (`0x667000`, `0x667600`), so original footage must distinguish
  directional/aiming locomotion from a nonfiring run.

The phase difference below remains unverified against original playback; the
turn-side discrepancy is now machine-code-verified and corrected as described
in the following section.
Dispatcher `0x568960` processes the current state and its queued messages before
installing a pending state and issuing entry. The reconstruction enters takeoff,
turns, and advances motion in the same simulation substep. The source entry does
not itself turn, so the first turn/sample may be shifted by one simulation tick;
this is not evidence that the original omits the half-turn. Also, `0x6991f0` uses
a dot/cross rotation with a fixed up-axis fallback outside approximately
±0.999 alignment, while the earlier reconstruction used shortest-yaw `remainder`. An
exactly or nearly opposite heading can choose a different turning side. Neither
difference establishes a correction to the selected clip or final target.

The later turn-side correction below does not settle the remaining visible
target-facing question. That needs an observed matching original transition,
with camera heading, movement, aim state and action timing recorded separately.
The [original-game visual comparison](player-evade-visual-reference.md) now records
actually inspected PC footage and why its raised-weapon witness does not resolve
the neutral-run transition.

## Verified near-antipodal turn correction

A bounded original-machine-code witness executes `0x6991f0` and its original
basis/matrix helpers from the hash-bound unpacked image, substituting only
standard sin/cos/sqrt calls. The script and output are ignored
`research/evidence/reconstruction/actor/turn_direction_witness.py/.json`. The
source uses cosine threshold **0.9990000128746033** (inclusive on both signs),
squared-axis threshold **1.0000001111620804e-6**, and fallback axis **(0,1,0)**.
At a 1/60-second maximum step of 0.41887906193733215 radians, current +179°
produces source heading −157°, while the former remainder/clamp gives +155°.
Current +180° produces −156° versus former +156°. Negative 179°/180°,
±177°, ±90° and target-reaching ±10° agree to numerical tolerance. This proves
a turn-side defect without changing the requested final target or clip.

The shared fixed-vector `math::turn_toward` preserves the original snap,
axis-selection threshold, normalization and basis stepping. Game policy supplies
the source constants and fallback axis; no Alicia names or constants enter math.
Inputs and fallback preconditions are explicit. Both design and implementation
query/read Core I.1/I.7 and performance MEM.9. This is direct reconstruction of
an established source helper, not an inferred alternate turning algorithm.

Verification: the existing resource-test target compares 11 independent original
helper outputs, tests the exact inclusive negative threshold and its adjacent
float, and checks caller-selected opposite fallback with double precision. The
resource target passes; the game library builds. Pinned formatting and scoped
fatal clang-tidy pass for the math fixture and controller/header consumer. No
first-tick transition change, animation swap or target change is included.
The integrated `python3 scripts/check.py all` gate passes after the correction,
including all six host suites, pinned formatting, fatal clang-tidy and content
policy. Log: ignored
`research/evidence/reconstruction/traversal/check-all-turn-reference.log`.
The running user session was not restarted; no new native visual acceptance is
claimed for this small correction.
