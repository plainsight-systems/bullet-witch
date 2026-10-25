# Weapon discharge attachment and firing audit

October 25, 2026. [Stationary layer audit](stationary-fire-layer-audit.md) ·
[Action policy](alicia-action-policy.md) ·
[Animation layers](alicia-animation-layers.md).

A live Shotgun discharge exposed a missing model root policy. This was a
reconstruction defect, independent of the broader reported support-hand motion.
The repair applies the original attachment policy to every weapon; it adds no
weapon-specific offset, replacement animation, damping, or hand IK.

## Original ownership and transform order

Weapon activation `615900` constructs the animated model through
`5dfc60 → 5dfe60 → 56a030 → 420ee0 → 42a8f0`. The embedded model constructor
sets its root-suppression field at `+0x90` to one. The inspected weapon
construction/activation chain does not override that field. The wrapper's own
`+0x90` owner pointer is a different object field, not this pose policy.

The original evaluator `423d20` tests that model field after local pose
composition. With suppression enabled, it replaces the first local matrix with
identity before building the hierarchy. Other weapon joints retain their
animation. The completed body `RightWeapon` frame supplies the attachment;
activation-local and model-render half-turns remain separate source operations.

The reconstruction already had an explicit identity-root policy in the shared
pose evaluator. The weapon consumer omitted it and inherited animated-root
behavior. Actual WA1 `shoot_after` contains a nonidentity animated root matching
the body `shot_gun` hand trajectory: at 0.2 seconds its translation is about
(-0.2921, 1.2377, 0.1492), while the corresponding body attachment is about
(-0.2923, 1.2378, 0.1499). Applying this exported frame again after attachment
explains the gun leaving Alicia's hand. Neutral weapon clips have identity
roots and concealed the missing policy during earlier settled-pose checks.

`PlayerActor::evaluate_attachment_palettes` now supplies
`RootPosePolicy::identity` to the existing weapon pose evaluation. Immutable
clip data and internal joint animation are unchanged. The body, resource owner,
world clock and renderer ownership are unchanged. Both design and implementation
consultations retrieved Core I.1 and performance MEM.9: the consumer explicitly
selects the coordinate-space policy and reuses its existing prepared buffers.

## Scope of the broader firing audit

The requested audit covers all four weapon classes, standing/crouched poses,
stationary/walking/dashing selections, raised aim on/off, and admission,
repeated discharge and exit. Source comparison covered the Shooter-to-Runner
order, body mirroring of stationary upper clips, independent weapon playback,
channel masks, additive multiplication, release snapshots, torso aiming and
secondary-pose publication. The accompanying stationary-layer report records
these source paths and the deliberately absent publications.

The additive owner has one retained controller, matching the original capacity;
repeated fresh shots cannot accumulate outgoing recoil controllers in this
owner. Original additive composition ignores contribution weights. Substituting
another recoil clip based on its name or weakening its delta would depart from
that evidence.

An additional pipeline inventory found a separate original support-arm modifier
that the initial known-stage comparison had not covered. Player setup `603b40`
resolves `LeftArm`, `LeftForeArm`, and `LeftHand`, allocates `5e09d0`, and stores
it at player `+0x1244`; `5e43f0` configures the three joints and target indices
100/101. Its enable path is real. This owner is separate from the torso aim
chain and must not be conflated with that already reconstructed mechanism.

However, the actual PC callback `5e3840` copies the three input matrices, reads
bind lengths and target points, calls `436ab0`, then copies those matrices back.
In the inspected Steam executable `436ab0` consists of `ret 4` (bytes
`c2 04 00`). The independent address-reference check found no literal-address
patch writer. Thus the retained arm owner is present but its inspected solver
has no matrix effect. Implementing two-bone IK here would add behavior beyond
this PC source, not reproduce a missing effective solver. This finding does not
establish how the Xbox 360 version behaved.

A native callback witness executes the original `5e3840` and its unchanged
`436ab0` body. Only read-only bind/target getters and the external square-root
routine receive explicit fixtures. All six enabled/disabled and distinct-input
cases leave the complete pose buffer byte-identical; enabled cases call the
native no-op once. This rules out the inspected PC modifier as an effective
missing correction, rather than assuming an IK implementation from its shape.

The inventory also found an effective `LeftArm` / `As_LeftShoulder` modifier
through `68e1f0 → 68e150 → 68be90`, with callback `68e0d0`. It multiplies a
retained local rotation into each configured bone; `68e680/68e470` advance
stochastic angular targets and apply full/half-angle sway. This is not the
inert three-joint callback above. Receiver selection is decisive: independent
factory tracing connects this alternate Shooter table (`8523d8`, setup slot
`+0x58 = 68e1f0`) through `5ff2e0/4562b0` to enemy `eb0`. Alicia's
`45b490 → 5fe970 → 5ff170` path uses table `855ad4`, whose corresponding setup
is `66c3d0`. The observed enemy sway owner therefore does not establish a
missing Alicia modifier and is not transplanted into her firing pipeline.

The static inventory did not establish a second effective body-layer mismatch.
The user rejected leaving the reported support-hand defect to another visual
check. The investigation therefore remains open and now compares literal
original executable stages against captured live Stage frames, as described
below. Neither the inert solver nor a passing attachment test closes that
defect; no original-game classification has been established.

## Verification and evidence boundaries

Ignored evidence lives under `research/evidence/reconstruction/shotgun-discharge/`.
It contains the actual decoded clip/root observations, probe sources and exact
compile commands, source diff, library hashes and per-frame outputs. The
pre-rebuild library hash and the already-patched working source hash are
explicitly different provenance observations: the original exploratory probe
linked the retained old library, while `root-fix.diff` records the working patch.
It is not claimed that the patched source produced that old library.

The adversarial component probe deliberately forces accepted-shot plans every
ten frames. It checks attachment consistency through blends, rather than
claiming live resource cadence. Across 48 class/stance/movement/aim cases and
10,080 frames, the old consumer disagreed with an independently advanced weapon
pose attached to the completed hand only for WA1 (maximum matrix-component
error 1.69–1.89). After the repair every case has zero error. All temporary
memory-domain charges retire.

A second component probe uses parsed original customization, a real world clock,
`PlayerResources`, and the native no-upper admission gate. It records full
`LeftHand * inverse(RightWeapon)` transforms as well as accepted shots and the
weapon-palette comparison. It also covers 48 cases/10,080 frames with zero
weapon-palette error and no retained bytes. This probe manually composes the
policy/model steps; it is not the complete Stage consumer and initially uses
unit movement scale. Its motion observations are not a source-output oracle.

The durable regression is part of the existing original-root startup corpus.
It checks all four weapon owners with real resource admission and compares every
published weapon joint palette through transient discharge, completion and
lowering. It also verifies that complete hand-relative matrices remain finite;
finite output is explicitly not a fixed-grip or visual-quality assertion.
The separate Stage trajectory coverage in that same corpus uses actual
`StageInspection`, `PlayerController`, source weapon movement settings, world
frames and resource admission. It observes all four weapons stationary/moving
and targeted/untargeted, without adding production observation/mutation APIs.

The focused headless runner loads the original `cm01_00` mission through
`MissionDriver` and executes those same durable corpus helpers. It passed all
16 real Stage trajectories (3,840 frames) and all 48 component attachment cases
(10,080 frames); every Stage case consumed ammunition and all memory charges
retired. The Stage observations include source movement scaling and the real
raise/input/admission ordering. For targeted standing fire, maximum observed
one-frame relative support-hand translation steps were approximately 0.061,
0.075, 0.037 and 0.061 metres for classes 0–3. These numbers describe remaining
motion, not pass thresholds. Untargeted cases also contain the actual raise
transition and must not be compared as if already settled.

The integrating agent subsequently ran the full repository gate successfully:
format/build, all six CTest executables, fatal clang-tidy over 202 translation
units and the content guard. A first test-only unchecked-index lint rejection
was repaired with checked access; its failed log remains preserved separately.
The final rebuilt original-root corpus passed in 251.72 seconds, binary SHA-256
`ad906ebabd8cd6953fa4f3ec14f489fad482b13a96b2a4322b43a861697a11dc`.
It includes the durable component and Stage cases above and retains the existing
pause/coordinator/melee/targeting checks; migrated CPU live storage returns to zero.
Logs are `full-check.log` and `final-actual-content/` under the evidence directory.
The prior successful binary run remains under `pre-tidy-actual-content/`.
These are distinct from the focused runner. Nothing here launches a window or
provides visual acceptance.

## Reopened numerical pipeline investigation

The ignored `reconstruction/firing-pose-native/` evidence separates concrete
producer, playback, pose and final skin stages. These comparisons are diagnostic
boundaries, not acceptance thresholds for hand placement.

The original skeleton-construction loop (`419530–4196e0`, inside `419320`)
executes its own transform builder `417bb0` and inverse `4094b0` from the raw
installed skeleton records. Alicia and all four weapon skeletons have identity
source bone-index mappings. Their resulting inverse-bind matrices agree with
the current decoder to at most `2.43e-7` per component. No transform function
is replaced by a fixture.

For 1,928 captured live Stage frames, the original final inverse-bind palette
loop (`422e4b–4233cd`, inside `422d30`) receives the same completed world
matrices as the current skin publisher. Its output agrees with captured
`SkeletonPose::skin_transforms()` to at most `7.68e-6` per component. This is an
isolated downstream comparison: supplying common completed matrices does not
validate the preceding action producer, pose or modifier. The separate native
evaluator and playback witnesses investigate those boundaries. The palette
witness has no emulation hooks, and its original image hash is the one recorded
above. Exact scripts and machine-readable results are
`skin_palette_witness.py` and `skin-palette-comparison.json`.

These isolated results rule out a large decoder or final inverse-bind
discrepancy in the captured cases; they alone do not prove concrete player
state correspondence. The subsequent producer/controller and aiming comparisons
below close those specific diagnostic boundaries. The authorized constraint
has separate integrated verification; numerical source correspondence is not
its hand-placement acceptance test.

## Original numerical replay and the support-hand decision

The later investigation executed original sampling, layer composition and bone
hierarchy code against actual captured actor frames, then closed the request
producer/controller loop. The ignored `firing-pose-native` evidence retains raw
motion/skeleton payloads, read-only instrumented translation units, capture
commands and JSON comparisons. These are local research artifacts, not shipped
assets or replacement runtime code.

For 1,928 untargeted body evaluations, original motion fixup and samplers,
`423d20/425540`, matrix conversion and `56ab10` reproduce local transforms to
4.17e-7 and the hand-relative attachment to 2.55e-5. The independent targeted
replay additionally executes `5e3ba0` and compares the arm/forearm/hand against
the weapon attachment. Its 1,928 frames agree within 3.07e-5 in relative space.

The producer witness executes Shooter `630400` and Runner `6667e0`, feeding
selected bindings and sample clocks from independently evolving original
controller rings. Native request/advance/retirement routines own those states;
reconstructed controller outputs are observations, not state overrides. Both
untargeted and targeted runs cover all four weapons, stationary and moving:
1,920 ground selections each, respectively 186 and 292 accepted shots. Binding,
freshness, blend duration, preserve-phase policy and start-frame publication
agree. The final witness supplies native **frame units** and compares admitted
start seconds after destination-frame-rate conversion; its largest difference
is 3.08e-6 frames. An earlier seconds-as-native-frame shortcut was corrected
before accepting this result.

External movement/camera commands, resource acceptance and non-firing posture
requests are supplied inputs. Resource cadence itself is independently traced
from `616530/617a30` and exercised through the actual PlayerResources owner;
the earlier zero-hook resource arithmetic witness covers magic, not the entire
firearm/projectile owner. That limitation does not introduce an alternative
stationary hand solver or change the observed equivalence for accepted shots.

Concrete setup also agrees: body/upper/additive capacities are four/two/one,
constructor remap modes are zero, and the relevant controller constructor and
reset clear the optional remap pointer. The request adapter only installs a
remap for other explicitly configured modes. Default root suppression is
installed by `42a8f0`. Alicia's concrete component registration uses the player
Shooter/Runner, excluding the separate enemy sway implementation. Dispatch
runs Shooter before Runner and both before child model advancement.

These results justify stopping speculative source repairs. The user authorized
an explicit improvement: supply a standard analytic two-bone constraint where
the PC arm callback calls an empty solver. The original registered arm chain and
selected weapon control-point slots remain the binding inputs. The authored
weapon records place slot100 near the support hand and slot101 near the elbow;
all four are weighted to weapon root0. Slot-role interpretation follows that
geometry and registration order, not a recovered solver body. See
[support-arm-constraint.md](support-arm-constraint.md) for the reference,
algorithm, ownership and deliberate departure from the original PC behavior.
