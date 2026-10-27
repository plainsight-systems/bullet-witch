# NPC shared initial motion

October 26, 2026. Bounded source reconstruction for mission population; not an
implementation of NPC decisions or combat. The original evidence remains in
ignored analysis exports.

## Initial receiver

The stage factory table at `0x82c6b4` has human constructor `0x4561d0` at
virtual `+0x40`. This constructor calls `0x5fe070` and the same `0x5ff170`
Runner/Shooter setup as the ordinary enemy constructor. It additionally calls
`0x5ff130` for a human component and attaches weapon slot one. It does not
replace Runner's `StateStart` handler. Consequently the grounded initial
receiver traced in `mission-startup-source-trace.md` applies to these humans:
slot one, `stand_l`, start zero, fresh playback, requested blend 0.3 seconds.
The first selected contributor starts at full weight. Ground-miss behavior
and subsequent authored AI requests are separate boundaries.

The recovered family table selects HM0S for HM0/HM1, HW0S for HW0/HW1, and
HM2S for HM2/HW2 for this clip. This is a per-clip owner mapping, not a universal
human animation bank or a filename-based idle heuristic.

## Retarget preparation and consumption

`0x5f9cd0` retains an actor/owner identity pair. `0x5f9dd0` obtains both model
bone directories and skeletons, then invokes `0x426420` on the target model
with the motion owner's directory/skeleton. Raw instructions resolve the
argument order that the pseudocode omits. The prepared map is indexed by target
bone and contains a matching owner index, translation scale and quaternion
correction. Unmatched target bones retain the target bind transform.

The source HipsBone absolute-height ratio supplies the factor through the
owner's hips index. Later bones use relative-translation length ratios; a zero
source length produces zero. The target length lookup is literally indexed by
the matching owner index, whereas the target rotation uses the target index.
The implementation retains that distinction rather than silently repairing an
unproven source defect. Out-of-range target scale indices reject safely.

The rotation correction is target bind rotation multiplied by the inverse
owner bind rotation. The original inverse uses squared-norm epsilon and
returns zero for a degenerate quaternion. Ordinary pose evaluator `0x423d20`
samples the mapped owner track, scales translation, left-multiplies the
rotation correction, then applies the controller's optional hips translation.
It retains sampled scale. Base-channel admission remains explicit. Hierarchy
composition and inverse binds belong to the target skeleton, not the owner.

The mapper's separately computed absolute hips difference is not automatically
added by the evaluator. Playback obtains its correction from `0x41f850` /
`0x41ed70`. The per-motion producer `0x6931c0` compares the first pointed word
against `0x68773200`, which is not the little-endian ASCII word for `hw2`.
Original-machine execution with the actual six ASCII family identities and
`stand_l` reaches its zero-return branch in every case. Do not apply the
table's 0.15 default or substitute the mapper's absolute hips difference.

EA families take another verified path: `0x5e2110` aliases their support owner
to `ea0` before `0x601250` and shared-owner lookup. The resulting same-owner
pair makes `0x5f9cd0` return no retarget map. Initial EA sampling therefore uses
direct named track association and each stage model's own bind defaults and
inverse binds. `NpcInitialMotion` validates that every target bone name has
the same owner index before admitting this direct association.

## Attachment and initial model state

Common actor construction `0x5fe070` creates body-follow nodes for `RightWeapon`
and `RightWeapon_b` at actor `+0x1264/+0x1268`. `0x6047a0` parents a weapon to
the first node when present, otherwise to the body. Enemy factory `0x4563e0`
selects slot zero through `0x603900`; human factory `0x4561d0` attaches slot one
but does not activate it. Do not display an attached human weapon merely
because its support was loaded.

Weapon activation `0x615900` constructs its model, applies its local Y half-turn
and requests slot zero followed by lowered slot one (or raised slot two).
WG1/WG2/WG5/WG7 each have a noncompressed list referring to a separate
`defaultmotion` publication and no lowered/raised stand publication. The list
archive itself does not contain the clip. The original `0x5e2180` keeps absent
slots null, and `0x5e47a0` returns without selecting them. Therefore slot zero
remains active after the unsuccessful lowered-slot request. Preparation must
retain `defaultmotion`, not claim an empty list or manufacture a stand clip.
These four purchased weapon models each have one bone; normal embedded root
suppression still applies during publication.

Body construction follows `0x5e0830 → 0x5dfd50 → 0x5e0250 → 0x56a030 →
0x420ee0 → 0x42a8f0`. Weapon construction uses `0x5dfc60 → 0x5dfe60` before
the same embedded constructor. The embedded model starts with root suppression
`+0x90 = 1`; the inspected initial receiver does not override it. Keep that
policy distinct from actor placement and the shared model-render Y half-turn
from `0x5e37f0 → 0x56b850`. The weapon's activation-local half-turn is a separate
operation. Root suppression belongs to each actor's pose consumer, not to the
immutable retarget helper.

The same common actor constructor calls `0x602390`, which sets actor `+0x1238`
bit `0x04`; its subsequent configuration write affects bit `0x02` only.
The traced grounded startup and StateRun entry do not clear `0x04`. This
establishes one initial retirement predicate for the population owner, not
the complete distance/flag/death eligibility algorithm.

## Grounded body origin and retirement identity

`0x6037d0` reads `witch/actor/<family>/body` from the customization tree.
Its only family alias is PW to PW0; EA/HM/HW physical data does not follow
shared motion-owner aliases. Body offsets +0x24/+0x28/+0x2c contain standing
height, alternate height and radius. The One Shot Kill sixteen-row witness
finds radius 0.25 for all rows, heights 1.7 or 1.8, alternate heights 1.4 or
1.8, and strictly positive initial life (EA 1, humans 600–1000).
`NpcInitialBodySettings` reads these fields from retained authored data rather
than hardcoding those observed values.

Grounded StateStart `0x665ed0` queries five units downward with tolerance
0.01 before requesting stand. A miss takes another state and does not admit
the grounded stand pose. A hit raises the actor center by the effective body
radius. The common actor vtable `0x852334 + 8` dispatches to `0x602b70`, whose
instructions `0x602d58–0x602dbc` overwrite the constructor's temporary -0.5
model Y with negative effective radius on every update. The multiplier is one
for living actors and doubles for radius <=1 with nonpositive life. Thus the
initial living physical center is ground+0.25 while its visible model origin
remains at the ground contact. The two offsets must derive from the same
settings/state; the temporary constructor constant is not the render policy.

The deferred retirement flag is not authored chara column six. `0x47ef40`
initializes spawn-record word 0x2d from `0x44af80`: PW=1, HM/HW=8, HR=4,
all other families (including EA)=2. Deferred admission `0x445580` copies
0x45 words to actor+0x12e4, placing this word at actor+0x1398. The direct
spawn branch can override the class using authored disposition bits, but
that override is separate from the deferred path. All sixteen authored
column-six values are zero; using those zeros as the complete retirement
classification incorrectly disables retirement. The existing bit4, actor
state and distance gates remain necessary.

## Initial lighting receiver

NPC body vtable `0x850d9c + 0x18` contains `0x5e4430`. The shared weapon
wrapper's corresponding entry (`0x850d68 + 0x18`, inherited from
`0x850d1c + 0x18`) also contains it. This receiver calls `0x440790`
unconditionally, obtaining context `+0x93f8`, the **character field**.
It has no EA/HM family selector. The separate enemy-field owner exists, but
its existence does not justify assigning it to these initial actors. Body
and active weapon each publish their own original transformed receiver bounds.
This is bounded initial factory/virtual-method evidence, not a claim about
every later effect or alternate model class.

## Established implementation comparison

Inspected Unreal revision `f45729c0b6f097038e72b52d366a62baeca3275b`,
`Engine/Source/Runtime/Engine/Private/Animation/AnimationRuntime.cpp`,
`FAnimationRuntime::RetargetBoneTransform` (AnimationScaled,
AnimationRelative and compatible-skeleton remapping). It separates immutable
skeleton mapping/reference data from instance local transforms, uses source/
target reference lengths for translation, and retains target hierarchy
composition. We follow that ownership boundary while preserving the recovered
game's equations and index conventions. Unreal's modern modes and normalization
are not substituted for the original algorithm; no Unreal code is copied.

## Implementation contract

`SharedMotionPose` is immutable prepared game compatibility data. It owns only
mapping entries and copied target bind transforms, never mutable actor state,
resource residency or a clock. Each NPC supplies an already wrapped/clamped
world-driven clip time and owns its local scratch and `SkeletonPose`.
The engine's `evaluate_locals` validates the complete local batch and publishes
hierarchy/skin output atomically using the target's retained inverse binds.
No retarget setup, archive reads or allocation occurs during sampling.

Both design and implementation queried the C++ Core Guidelines and performance
corpora. Retrieved Core I.11 and R.14 make ownership and bounded borrowing
explicit; MEM.9 places mapping/scratch allocation before steady-state sampling.

## Verification

Synthetic public-contract checks pass for named retargeting, hips/segment
scales, relative orientation, unmatched target bind retention, separate instance
scratch, invalid input rejection and atomic local-pose publication. Scoped
production syntax checks pass; final repository gates belong to the integrating
mission-startup work.

An ignored original-machine witness executes `0x426420` against the purchased
six HM/HW skeleton/name pairs and their three shared owners. Each target has
92 bones. Applying the resulting original maps to prepared owner samples at
0, 0.1, 0.5 and 1 seconds agrees with the native retarget helper across 2,208
bone samples, maximum component difference below `2.19e-7`. The sole external
math hook supplies double square root at the original CRT boundary. This
executes the original **mapper**, not the full original controller/model
evaluator. The consumer-equation comparison uses the already prepared owner
samples and separately read evaluator equations. No original rendered-frame
or live AI parity is claimed.

Witness inputs, machine output, native sample output and command source are in
ignored `research/evidence/reconstruction/mission-startup/shared-motion/`;
the image hash is recorded in `retarget-witness.json`. The zero-offset branch
execution is recorded separately in `motion-offset-witness.json`.

The production initial-motion wrapper additionally prepares and composes all
six human targets, three stage EA targets and WG1/WG2/WG5/WG7 at four clocks
using purchased skeletons and clips (`initial-wrapper-witness.json`). This
checks direct EA bone identity and actual weapon default-motion dependencies.
`body-settings-witness.txt` records all sixteen actor rows; weapon list and
exact clip-resolution metadata are retained beside it. These are read-only
asset and CPU-pose checks, not rendered acceptance.
