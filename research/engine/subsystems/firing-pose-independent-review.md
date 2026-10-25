# Firing pose independent review

October 25, 2026. Review of the attached-weapon root repair and the separately
reported stationary support-hand motion. The bounded original-executable pose,
controller and palette comparisons are complete. The PC arm callback has no
effective solver; the user authorized an established two-bone fallback using the
original registered chain and authored weapon targets. This explicitly changes
PC behavior. Independent numerical and ownership findings appear below, followed
by the integrated transition validation. This report does not substitute for
user visual acceptance or claim original-game visual parity.

## Source and responsibility assessment

The attached-weapon root repair is supported by the original constructor path,
not by a visual offset adjustment. Independently inspected exports show weapon
activation `615900` constructing through `5dfc60`, `5dfe60`, `56a030`, `420ee0`,
then `42a8f0`. The model initializer sets its root policy field at `+90` to one.
The final local-matrix conversion in `423d20` writes identity for the first
root when this field is nonzero, while converting the remaining joints normally.
The weapon wrapper's separate attachment parent and activation transform do not
replace this policy.

`PlayerActor::evaluate_attachment_palettes` now passes
`RootPosePolicy::identity` to the existing weapon pose evaluator. This is the
appropriate responsibility boundary: game-owned attached-model policy is explicit
at the caller; the engine evaluator implements the reusable root policy. The
completed body bone supplies the attachment transform. Inverse bind is applied
only to skin matrices, after hierarchy composition. The change neither alters
body animation nor substitutes a frozen weapon pose. Internal weapon joints and
retained animation transitions continue to evaluate. No new allocation, mutation
capability, controller clock, or clip-specific offset is introduced.

The review consulted full Core Guideline I.1 and performance MEM.9: the concrete
application is an explicit pose-policy input and reuse of already allocated pose
storage. These rules do not themselves establish animation fidelity.

## Composition comparison

Direct comparison of `423d20`, `425540`, `630400`, and `6667e0` with
`src/animation/pose.cpp`, `src/game/alicia_action_policy.cpp`, and the actor's
layer assembly found no additional demonstrated divergence in these inspected
operations:

- Ordinary channels admit authored per-bone masks and replace in ascending
  channel order. Within a chain, the oldest contribution starts at weight one.
  The unusual first-admitted fractional-weight branch copies rotation and adds
  weighted translation/scale; the implementation preserves that branch.
- Additive channels skip missing tracks, compose local rotations and scales,
  sum translations, and do not apply controller blend weights to the deltas.
- Higher-channel release saves composed ordinary-plus-additive local poses and
  their admitted mask, then decreases release weight by three times the model
  interval after that owner leaves. Aim and hierarchy follow composition.
- Shooter admission requires no active upper owner and a successful resource
  fire result. Stationary Runner selection mirrors an active upper action's
  family/current sample for direction 1 or 12; moving selection retains
  locomotion. These are separate consumers, not one animation clock.

The stationary `dc` recoil publication is an actual original alias. Its filename
does not authorize replacement with the unbound `shoot_stand_fire_add` resource.
No hand IK, recoil reduction, or fixed-grip invariant is justified by this review.
See also [the separate policy audit](stationary-fire-layer-audit.md).

## Evidence assessed so far

Original image SHA-256:
`bdaff1746d3092215699b09eab11315c6adf8fb71eaa8f46c280b11b3cd448fd`.
The inspected ignored exports are passes 24, 70, 90, 102, 103, and 117.
Generated probes are under
`research/evidence/reconstruction/shotgun-discharge/`.

The initial transient probe spans four weapon classes, two stances, three
movement categories and aim enabled/disabled: 48 cases. Independently parsed
before-results show weapon palette differences from the identity-root reference
of 1.69020498–1.89182281 for the twelve Shotgun cases, and zero for the other
36 cases. This is a matrix-component discrepancy, not a distance measurement.
The old linked game archive is identified by
`176b7ab2cf38e505f8d70829116ca243c9eef4cd2602338084a8aa01d4ed3eb1`.
The matching after-results independently parse as zero discrepancy in all
48 cases, with zero live tracked bytes after teardown.

Two verification boundaries were raised with the implementation agent:

1. The initial probe forces accepted shots every ten frames, bypassing the live
   upper-owner and resource cadence gates. It is an adversarial attachment
   consistency check, not a live firing sequence.
2. Its initial hand output contains world translations only. Those values cannot
   establish whether the support hand loses its relation to the weapon. Actual
   admitted-fire trajectories and the full hand transform relative to the
   completed weapon attachment are needed to assess that symptom.

The reference also uses the reconstructed pose evaluator. Agreement with it
validates caller policy and palette assembly, not independent numeric parity of
that evaluator with the original executable. The `before-hashes.json` source
hash describes the already patched source beside the retained old library;
it must not be read as an unpatched-source build attestation.

The subsequent `live_trajectory_probe.cpp` uses actual resource settings,
`PlayerResources`, `WorldClock`, and the active-upper gate. It records all 16
components of `LeftHand * inverse(RightWeapon)` in the row-matrix convention.
This addresses the first probe's forced-acceptance and world-translation limits.
It still manually invokes action plans and ground selection instead of the live
`StageInspection`/`PlayerController` consumers, uses a fixed forward direction
and aim target, and uses the ground input's default weapon movement scale.
The original Gatling movement scale is 0.9. Consequently these are admitted
component trajectories, not full live-controller parity evidence.

The durable `discharge_attachment_trajectory` addition in the existing startup
corpus follows that same component boundary. Its identity-root palette check
is a useful regression for the demonstrated defect. Its finite relative-hand
check is correctly documented as a corruption guard: finite output does not
prove correct grip, recoil, or animation transition behavior.

The reviewer independently reran that headless admitted-component probe against
the installed original data. It exited zero with 48 cases, maximum palette
component discrepancy zero, and `LIVE 0`; no visible host was opened.
Probe SHA-256:
`1c415d37f04afb231ec071139df788aaebf278c35837aaa229a52c4a0757b77f`.
Captured stdout SHA-256:
`f3ce198712eeb64b1d06125445e6527699ea69d217994a7ac3f0464158685b34`.
This independent execution retains all component-versus-consumer and
reconstructed-versus-original oracle limitations above.

## Additional modifier inventory

The earlier policy comparison was not a complete modifier inventory. The review
therefore followed the named hand/arm consumers, player constructor, callback
registrations, enable producer, and alternate actor factory. This is a bounded
inventory of those paths in the identified Steam executable, not a claim to
have proved every dynamically reachable modifier in every game state.

| Source mechanism | Owner and effect | Disposition for this repair |
|---|---|---|
| `603b40 -> 5e09d0 -> 5e43f0` | Alicia construction resolves `LeftArm`, `LeftForeArm`, `LeftHand` and retains a three-joint modifier at player `+1244`. Raised-posture time `604540`, fed by Shooter and expiry, enables it. | Real registered player hook, but its callback's solver is inert in this Steam image; no effective pose correction is missing from the reconstruction on this inspected path. |
| `68e1f0 -> 68e150 -> 68be90` | Two active modifiers on `LeftArm` and `As_LeftShoulder`, driven by `68e680/68e470`, apply procedural angular sway through `68e0d0`. | Alternate `eb0` actor setup; not Alicia's factory. Do not add this actor-specific behavior to the player. |
| `61f4a0 -> 623c00` | A separate model retains the player's arm index, reads its matrix into local storage, scales axes and updates its own transform. | Attachment consumer, not a write to the player's hand pose in the inspected update. |
| `RightWeapon` lookups in `5fe070` and preview `48af90` | Bind the completed body attachment used by weapon presentation. | Already represented by the body-to-weapon hierarchy boundary; root policy remains the demonstrated defect. |

The first candidate initially appeared to establish missing effective IK. That
intermediate interpretation was withdrawn after following the actual callback:
`5e3840` copies three input matrices, reads bind lengths and target points
100/101, calls `436ab0`, then copies the matrices back. **`436ab0` is a literal
three-byte `ret 4` (`c2 04 00`).** It does not solve or alter the packet. Examined
literal references include shared empty callbacks, with no patch writer found;
this is not an exhaustive proof against arbitrary runtime patching.

The reviewer independently reran `arm_modifier_witness.py`, which executes the
original callback and leaves that solver's bytes untouched. All six fixtures
(enabled/disabled, three different matrix offsets) preserve pose buffers byte
for byte. Enabled cases reach the original solver once; disabled cases do not.
Hooks supply read-only bind-pose/target matrices and external scalar square root.
This verifies the callback behavior under the recorded fixtures, not actual
original-game whole-character pose output. Witness source SHA-256:
`bc532877a70b2138bf95d7fd3c96eb977bffde69ca441f3e44821d591143cdeb`;
result SHA-256:
`90c0c920a01117f642094a9ae2c1702153c4dce86ec96f0b11a06323a4da1d95`.

For the second candidate, concrete factory `4562b0` is the `eb0` name-class
factory at game virtual offset `+4c` (see [actor factories](actors-and-animation.md)).
It calls `5ff2e0`, which constructs Shooter `5fd6f0` with vtable `8523d8`;
its `+58` callback installs the sway. Alicia's `45b490 -> 5fe970 -> 5ff170`
route constructs Shooter `5fd9d0` with vtable `855ad4`, whose `+58` is `66c3d0`.
This alternate active implementation must be considered when reconstructing that
enemy type; it supplies no evidence for a missing Alicia hand constraint.

## Live consumer coverage and ownership

`live_discharge_trajectories` in `tests/startup_corpus.cpp` now supplements the
48 component cases with 16 actual `StageInspection`/`PlayerController` cases:
four weapon classes, standing stationary/sideways moving, targeting off/on.
Each advances 240 frames, requests fire during frames 90–209, checks ammunition
was consumed and observes full relative hand matrices. This exercises the live
consumer order and source weapon movement scaling. Crouch and dash categories
remain component-level coverage, not additional live-controller cases.

The focused actual-stage runner passed all 16 live cases plus the 48 component
root cases, with zero tracked bytes after teardown. The reviewer inspected its
output and consumer setup; only the component and original callback probes were
independently rerun. `stage-trajectories.log` SHA-256:
`3d264751ed2b5797228f1c09e2d43f2d5e6e59ddf061649d5f2dc4b07db59191`.

Asset owners outlive each borrowed scene; scene destruction precedes resource
and clock destruction. Action resources are bound through the existing action
boundary; observed poses are immutable. The fixture creates no additional
production mutation authority. Relative-hand step values are observations, not
acceptance thresholds or a source-derived fixed-grip invariant. Finite matrices
and consumed ammunition cannot establish correct grip or visual equivalence.

## Initial root-repair validation and disposition (before fallback)

The first full gate passed its six CTest targets but found a bounds-style lint
violation in the new corpus's dynamic `std::array` access. The owning agent
replaced it with `.at()` and formatted; the first failed log is retained as
`full-check-first-failed.log`. The earlier actual-content run passed in
253.042 seconds against binary
`2fb3193ef1f4fdcb9a1f7c6858cf4101eed6323dc4d2c1614b4258b674bf2268`.
It is retained separately as pre-tidy evidence, not validation of the revised
test source. The final full gate passed: six CTest targets, 202 clang-tidy commands and
repository-content verification of 704 file versions. `full-check.log` records
that run. The rebuilt final actual-content corpus passed in 251.717752625
seconds against binary
`ad906ebabd8cd6953fa4f3ec14f489fad482b13a96b2a4322b43a861697a11dc`.
The reviewer inspected `final-actual-content/verification-summary.json` and its
stderr evidence: all 16 live Stage firing cases consume ammunition; the broader
melee, targeting, posture and coordinator checks continue to pass; tracked CPU
bytes return to zero. These are headless execution checks, not rendered visual
acceptance. Final corpus source SHA-256:
`2c7c6c162a5953c9b48146e958a36c88fe3b12ba1a1ea03a88fb21e31ec9ea15`.

Production source reviewed:

- `src/game/player_actor.cpp` SHA-256
  `7824c075c7fa4776072ed786bc05ba4869952004fdefeb634db2c7a63b7f4d41`.
- `src/game/player_actor.hpp` SHA-256
  `c27502d971050837cf2b45a0fa7bbff65246dba2a1a81d3e5a693b8f274b1bba`.

At the initial root-only review, the root defect had an independently confirmed
source basis and regression coverage. No additional effective player modifier omission was demonstrated by
the bounded inventory. **The stationary support-hand finding was still open at this stage:**
there was then no original combined-pose oracle or accepted corrected
walkthrough, and the root-only repair did not change the body's hand pose.
Neither source-policy agreement nor the inert callback establishes that the
reported hand motion is an original-game defect. The review has not launched a
game window, sent input, edited production code, committed, or pushed.

## Reopened direct executable comparison

The subsequent investigation executes original `41d570` motion relocation,
`423d20` pose evaluation, original vector/quaternion samplers, `425540` blending,
release, `417bb0` local-matrix construction and `56ab10` hierarchy/attachment
query. There are **zero hooked routines** in this witness. Raw original motion
payloads are mapped directly into the emulator; descriptor wiring, captured
controller states and supplied skeleton data are explicit fixture inputs.

The implementation agent captured actual `StageInspection` evaluation inputs
and read-only outputs for four weapon classes, standing stationary/moving,
untargeted: 241 evaluation calls per case, 1,928 calls total. The independent
reviewer replayed all of those calls with retained original release state and
compared original outputs against the captured C++ outputs. Per-bone local
Q/T/S discrepancy is at most `4.168398742843138e-7`, excluding the separately
controlled root and direction-basis bones. Relative
`LeftHand * inverse(RightWeapon)` matrices, using original hierarchy output,
differ by at most `2.5414768648257535e-5` across all 16 components. The shared
root/direction ancestors cancel in that relative comparison; their separate
basis behavior was not silently replaced with the reconstructed result.

This rules out a substantial sampler/blend/hierarchy discrepancy for the
**supplied controller states** in those cases. It does not establish that the
reconstruction published the same clips, sample times, transition weights or
update order as the original controller. Raw skeleton expansion and absence of
retarget mapping remain producer-verification responsibilities; zero-filled
optional pointers are not proof that original setup omitted them. Targeted
modifier composition, rendered weapon-internal grip transforms and original
controller cadence remain separate boundaries. The hand issue is still open.

Ignored evidence: `reconstruction/firing-pose-native/evaluator.py`,
`compare.py`, `actor-frames.jsonl`, `assets/`, and `compare-raw.json`.

### Native retained-controller replay

A separate ignored capture observes actual `PlaybackRing` request, advance,
rate, finish and clear operations without changing production files. Its replay
runs original `5e47a0`, `41f2f0`, `41e440/41f0a0`, `5e1a00`, `423b40`,
`5e4130` and `41f800`, including original insertion/removal callbacks. The only
replacement is external CRT `memset` at `500138`; request and timing algorithms
are original instructions. An observer stores the original x87 weight return;
it does not replace blend arithmetic. Captured caller-owned rate and
remove-on-completion policy are supplied explicitly, not independently inferred
from this mechanism replay.

Across 19,756 observed operations (32 constructions, 2,157 requests, 2,141 rate
updates, 7,680 advances, 7,680 finishes and 66 clears), linked contributors and
retirement agree. Maximum cyclic sample-time discrepancy is approximately
`2.277e-6` seconds; weight discrepancy is `7.451e-8`. Twenty-four repeat-boundary
flag differences occur only on moving body binding 11, where original float
frames and reconstructed double seconds fall on opposite sides of a repeat
endpoint. No firing/nonrepeating completion or retained-contributor discrepancy
was observed. This does not establish that the supplied gameplay requests match
the original Shooter/Runner producers. No timing retuning follows from this
bounded floating-point observation.

The implementation agent additionally traced null retarget mapping to original
controller construction/reset (`41dad0/41e170`) and Alicia manager modes zero
in `5e0250/5e0830`; `5e47a0` invokes the remap setter only in modes one/two.
The exported bind JSON was compared with raw skeleton records by that agent;
parents match and relative Q/T/S differences are decimal serialization rounding.
These producer checks support the fixture boundaries above. Independent native
skeleton/palette construction remains a separate downstream investigation.

### Targeted live aim composition

A separate headless actual-Stage capture enables targeting for all four weapon
classes, stationary and moving: another 1,928 evaluations, including 1,912 with
nonzero aim weight. The original raw sampler/layer evaluator runs first, then
original concrete modifier `5e3ba0`, then original hierarchy queries `56ab10`.
The source callback's local matrix writes feed the hierarchy directly. The
maximum 16-component relative-matrix discrepancy for LeftArm, LeftForeArm, LeftForeArmRoll and
LeftHand against RightWeapon is `3.0612834028387326e-5`; the maximum full-world
matrix discrepancy across those five bones is `6.102373046701359e-5`.

This is numerical evidence for the actual composed firing trajectory, not only
steady raised idle. Common policy inputs remain explicit: captured aim target,
fade weight and direction-reference local Q/T/S. Callback delta is zero while
its retained weight is set to that captured value; therefore this witness tests
composition, not independently inferred fade activation. Two pointer getters
(`428520`, `508ca0`) expose the fixture skeleton records; external CRT square
root/cosine/sine (`5000f0`, `5000e4`, `5000ea`) use host double elementary
functions. No source sampler, blend, quaternion, aim algorithm or hierarchy
routine is replaced. The original callback performs its own basis reset and
all aim corrections. No aiming adjustment is justified by these results.

Ignored evidence: `firing-pose-native/target-actor-frames.jsonl`,
`target-capture-command.json`, `target-capture.log`, `compare_target.py` and
`compare-target.json`. Capture ran without a window and returned zero live
tracked CPU bytes. All original untargeted/controller captures were preserved.

### Independent inspection of the downstream palette witness

The lead's `skin_palette_witness.py` executes the original skeleton expansion
loop in `419320` (including `417bb0` and `4094b0`) against raw pw0/wa0..wa3
skeleton bytes. Loaded inverse-bind matrices differ by at most `2.429e-7` and
source matrix indices are identity in these assets. The original `422d30`
inverse-bind/palette stage then receives captured world matrices as common
inputs and compares against actual `skin_transforms()` output on all 1,928
untargeted frames; maximum discrepancy is `7.680e-6`. It has no hooks. Independent
inspection confirms that scope and its intentionally isolated common-input
boundary; it does not establish upstream producer or rendered vertex parity.

None of these completed comparisons closes the reported hand issue. They narrow
it beyond the inspected numerical sampling/blending/aim/hierarchy/palette
algorithms. Original gameplay producer state must be independently coupled to
retained native controller state, and final attachment/draw behavior remains
separate from an accepted corrected walkthrough. No compensating offsets or IK
have been introduced.


The secondary-motion publication boundary is structurally excluded as the cause
of this relative hand motion. All reconstructed Alicia secondary output targets
are bones 85 through 124. None occurs in the raw skeleton ancestry of LeftArm
11, LeftForeArm 12, LeftHand 14 or RightWeapon 26. `compose()` commits the
post-aim local matrices, and `apply_world_overrides()` recomposes untouched bones
from those same committed matrices. Therefore these bones retain their evaluated
aim transforms through secondary publication. The structural census is recorded
in ignored `secondary-structural-check.json`.

Target/fade policy is likewise not an open explanation for relative grip. The
concrete source/current chain is Spine 4, Spine1 5, Spine2 6, Spine3 7 and
Shoots_direction 27; only bones 4 through 7 receive corrections. Every corrected
bone is a common ancestor of LeftHand and RightWeapon. Any such ancestor
transform cancels in `LeftHand * inverse(RightWeapon)`, apart from floating-point
rounding. In the captured targeted firing interval, weight is exactly one and
target is `(approximately 0, 0, 1)` throughout. Earlier target-space numerical
witnesses remain applicable; no new hypothetical target/fade gap is asserted.

## Authorized fallback after the bounded source comparison

The user subsequently authorized a proven fallback where the bounded trace finds
no effective original repair, rather than prolonging source investigation. The
implementation gives the original registered arm owner an established
analytic two-bone solution using authored target data and source activation
policy. This is an intentional behavioral improvement over the inert PC solver,
not a newly discovered faithful PC implementation. Earlier statements rejecting
invented IK describe the preceding parity investigation; they do not prohibit
this explicitly authorized fallback. Independent review checks the shared
solver, target-space authority and pose ownership, plus actual hand-to-grip
trajectories and release through reload, melee, evade and lowering transitions.
The preserved original numerical evidence remains the baseline.

The shared fallback header and implementation were independently checked against
that contract and the pinned Unreal reference. A separate ignored deterministic
property probe checks segment-length preservation and quaternion direction
mapping across reachable/unreachable targets, unequal lengths, coincident goals,
zero/collinear poles and equal-length folds. Its independent invariants hold in
4,000 cases for each scalar type: double maximum length/direction errors are
`5.56e-15`/`7.97e-15`; float errors are `1.90e-6`/`2.35e-6`. The probe does not
reimplement the triangle formula as its expected result. Invalid-input and
explicit edge cases also belong to the implementation's focused math test.
Review consulted Core I.1/E.4 and performance MEM.9: explicit space/failure,
owned result, validated invariants and no steady-state allocation. Triangle arithmetic alone does not establish correct authored targets or action
release; those are separately verified in the integration sections below.

### Integration findings and revised ownership

Independent review caught an actual composition defect in the first integration:
applying the arm batch after the costume batch would discard the costume edits,
because `apply_world_overrides()` recomposes untouched bones from committed local
matrices. Simply reversing the calls would discard the arm edits instead. The
implementation now returns three owned arm overrides from read-only input poses;
PlayerActor collects costume and arm edits in prepared storage and publishes one
atomic batch. This resolves the defect without making shared pose mutation
implicitly cumulative.

The current PW0 secondary anchor controls 0 through 18 influence bones 2, 9, 29,
30, 31, 48 and 49; its six collision capsules use 9, 7, 20, 21, 23 and 24.
None is in the corrected arm subtree. Computing both sets of edits from the
clean evaluated pose is therefore valid for this inspected content. The separate
read-only export is `assets/pw0.all-control.json`. Weapon targets 100/101 are
weighted only to their weapon skeleton root in all four weapon classes, and the
weapon attaches to the unaffected right chain: no corrected left-hand output
feeds its own goal.

The first target-only entry blend was also flagged because an unblended pole
could rotate the elbow plane at arbitrarily small weight. Both target and pole
now approach their authored weapon values from the current hand/elbow positions;
zero weight publishes no override. The actor's action-owner permission and upper
motion-family policy are separate from torso fade, so residual aim weight alone
cannot authorize gripping during an exclusive action. Actual-content trajectory and transition verification follows below.


### Closed producer/controller comparison

The implementing agent subsequently coupled original Shooter/Runner request
selection to evolving original retained-controller states for untargeted and
targeted captures. Independent review caught and required a units correction:
the source upper controller stores sample **frames**, not seconds. Its frame
value now remains native through request production; comparison converts the
admitted C++ start time by the destination clip's frame rate. The review also
required comparison of fresh/start/blend fields and preserved-time policy,
rather than checking only binding identity. Both corrected replays complete
without findings; their maximum numeric request discrepancy is `3.078e-6` frame
units. External movement/camera/action admission and unrelated posture/reload
requests remain explicit fixture inputs. Combined with the actual pose-stage
comparisons, this ends the bounded source investigation; it does not justify
claiming an unobserved source fix. The authorized fallback is documented above.


### Final independent fallback integration and transitions

The first actual Stage construction exposed a real integration omission:
`MissionLoading::PlayerWeapon::view()` did not expose its already-owned control
point bytes. The adapter correctly rejected the empty input instead of silently
inventing a target. The owner now supplies an immutable span from its retained
archive; parsed adapter metadata is prepared before evaluation. The initial
failed run is preserved as `grip-review-first-failed.log`.

The independent reviewer then executed 16 actual Stage/controller/resource cases:
all four weapon classes, stationary or sideways moving, targeting initially off
or on. Each runs 1,300 world ticks, with real admitted firing followed by reload,
firing again, melee, evade, re-entering targeting, and toggling targeting off
through the original raised-posture timeout and lowering. Construction plus
these ticks produces **20,816 evaluated poses**. Ignored instrumented copies
observe the production clean pose, authored target and final combined
publication; they do not replace controller inputs beyond the scripted user
sequence or substitute pose results. The runner exits zero, consumes ammunition
in every case and reports zero retained CPU bytes after teardown.

Across those evaluations:

- All 8,524 active evaluations preserve the right-hand attachment and every
  published costume override exactly. Inactive evaluations leave the left-hand
  world position identical to its clean authored pose.
- The 640 settled firing frames pass the corpus's independently reconstructed
  control-point-100 target/reachable-clamp check at `0.0001` world-unit tolerance.
  Stationary firing reaches the authored target in all four classes. Moving
  classes 0, 1 and 3 also have zero observed settled target residual.
- Moving class 2 has an authored target outside the unstretched arm's reach in
  25 untargeted and 23 targeted settled frames. Its largest observed residual is
  `0.114786` world units, down from `0.473031` before correction at that frame.
  This is the explicit no-stretch reach limit, not exact contact. Both cases
  pass the independently computed reachable-endpoint assertion; no settled
  target distance increases. The review does not hide that remaining visible
  limitation behind a generic pass result or invent a longer limb/offset.
- The arm publishes no overrides during 2,324 admitted reload, 912 melee,
  1,353 evade and 1,648 lowering frames. All 16 cases exercise every release
  category. During the source raised-posture timeout it may retain its grip;
  when lowering takes ownership, permission and publication stop together.

`grip-review-summary.json` records the per-case observations and hashes, with no
publication/release findings. The absolute target residual is deliberately
recorded separately from the reachable-clamp contract. The full copied corpus
also runs the per-weapon checks for segment lengths, hand orientation, zero
weight, shared costume publication and no retained allocation. Its successful
run is `grip-review-run.log`; raw final outputs are `grip-review-frames.jsonl`.
All these files are local ignored evidence under
`research/evidence/reconstruction/firing-pose-native/`.

Independent transition binary SHA-256:
`024cdc084b67a353a1ba3097844f35c6f5ab78172948a30059eec0ecc5dc3fa5`.
Captured output SHA-256:
`c7eede3358037d7d22400729aa00ed2c154caea8eae66f88f1087b43f9986a58`.

This executable predates the final lint-driven named-input wrappers
`SupportArmPoses`, `SupportArmSource` and `TwoBoneDirectionChange`. Independent inspection confirms
those preserve const inputs and the same arithmetic/composition. The final
`SupportArmPoses` uses named `reference_wrapper<const SkeletonPose>` fields,
explicit caller designators, and immediate read-only `.get()` borrows; it retains
no input reference. Setup similarly uses named immutable `SupportArmSource`
fields and designated callers; its constructor parses owned control metadata
and retains only bone indices and prepared scratch. The body skeleton optional
is checked through the same stable local borrow before access. These setup
changes preserve the inspected ownership and mapping. Reviewed final source
identities are recorded separately in
`final-review-source-hashes.json`. These source-only changes do not
make this capture the final production binary. The parent owns the final rebuilt
full gate and original-content corpus and records their distinct identities.
The independent algorithm/ownership/release review is complete, with the explicit
class-2 reach limit above. A rendered user walkthrough remains the visual
acceptance step. No game window was opened, no inputs were sent to the user's
session, and this review made no production edits, commit or push.
