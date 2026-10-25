# Stationary fire layer audit

October 25, 2026. Read-only supporting investigation of the reported left support
hand jerk while firing stationary, with moving fire reported stable. This audit
is independent of the demonstrated attached-weapon root transform defect. It
makes no visual acceptance claim and introduces no animation correction.

## Source action and actual publications

Shooter `630400` admits fire only with no active upper owner and weapon result
zero. It requests an ordinary upper discharge, independent weapon slot 6 and
an additive recoil, each fresh with requested blend 0.3 seconds. The upper
family is `(29 + class * 2 + crouched) * 16`, retaining the body direction nibble.
Ground recoil selects walk/dash/other families `d0/e0/f0`, or `100/110/120` for
class 2. Air selects `2c1`. These formulas match `accepted_shot`.

Actual standing publications with direction nibble 12:

| Class | Upper discharge | Additive from ordinary standing family |
| --- | --- | --- |
| Gunrod, 0 | `1dc`: absent | `dc`: `shoot_walk_vib_ri_add` |
| Shotgun, 1 | `1fc`: `shot_gun` | `dc`: `shoot_walk_vib_ri_add` |
| Cannon, 2 | `21c`: `cannon` | `10c`: absent |
| Gatling, 3 | `23c`: absent | `dc`: `shoot_walk_vib_ri_add` |

The direction-dependent walking rows use `shoot_walk_vib_fr_add` for forward/back
and `shoot_walk_vib_ri_add` for sideways, with Cannon-specific forward/back and
left/right deltas. Shotgun forward/back upper rows use `walk_shot_gun`; sideways
rows use `shot_gun`. Cannon moving upper rows are absent. Native missing slots
preserve existing ownership; they do not authorize substitute clips.

Consequently a persistent all-four-class symptom cannot be explained solely by
upper discharge or its body mirror: classes 0 and 3 have no such upper clip.
The unexpectedly named sideways recoil for stationary fire is an actual source
alias. The installed `shoot_stand_fire_add` remains unbound in this source table;
its name is not evidence to replace that alias. Recoil family selection uses the
current body family, so a body still in a mirrored upper-action family follows
native “other” selection rather than the ordinary walk family.

## Request, sample and model order

Original Shooter constructor `62e300` stores component priority 1 at `+1c`;
Runner `663c50` stores 2. Registration `5fd9d0/5fd920` walks backwards by priority
and inserts through `40f1a0`; ascending forward dispatch in `568960` therefore
visits Shooter before Runner. The current `StageInspection::present_input`
before `PlayerController` ground selection matches that relation.

Runner `6667e0` mirrors the active upper family into the body only when stationary
(direction 1 or 12), using the upper controller's current frame. Movement keeps
independent locomotion. The current selector supplies the upper sampled seconds;
all relevant Alicia resources share 60 Hz. Body requests remain fresh=false.
Source `5e47a0` and current `PlaybackRing::request` retain a matching resource
without restarting, even when the requested alias changes. A newly selected
body mirror and its upper owner both subsequently advance in the model child.
There is no demonstrated duplicate clock advance or unconditional same-clip body
restart in this inspected path. Different intervals/subdivision policies remain
distinct from a proven stationary hand defect.

## Pose and aim comparison

Inspected original `423d20/425540` versus `SkeletonPose::evaluate_layers`:

- Authored per-bone channel words gate ordinary and additive contributions.
- Ordinary channels replace in ascending order; each chain is oldest first.
- Additive samples compose local quaternion/scale products and translation sums;
  the source additive loop does not multiply these deltas by blend weights.
- Higher-channel release captures the composed ordinary-plus-additive locals,
  before model modifiers, then decays by `3 * dt` after the higher layer leaves.
- `668230` installs the current case-sensitive aim chain and separate
  `Waist_direction` reset. The shared torso correction follows composition and
  precedes hierarchy and attached-weapon publication.

No additional divergent body-layer algorithm was demonstrated by this bounded
comparison. In particular, no source evidence here supports adding hand IK,
reducing recoil amplitude, replacing idle clips, or weakening additive weights.
A subsequent numerical witness should compare the support hand relative to the
weapon attachment across complete stationary and moving trajectories, separately
from weapon-local root motion; a world-space hand displacement alone does not
establish lost grip.

## Evidence and limits

Original image SHA-256:
`bdaff1746d3092215699b09eab11315c6adf8fb71eaa8f46c280b11b3cd448fd`.
Existing ignored evidence: native export passes 05e (component constructors and
registration), 90 (Runner hardware), 103 (pose evaluation), 108 (model modifier),
117 (Shooter), and `reconstruction/actor/animation-coverage-bindings.json`.
The current adapter, pose and controller files were read directly. No new native
execution, game launch, input, source modification or acceptance test was
performed for this supporting audit. The known weapon root defect is owned by
the separate investigation and is not presented as proof of every reported case.

## Subsequent modifier inventory

The initial comparison above did not establish a complete modifier inventory.
The later [attachment and firing investigation](weapon-discharge-attachment.md)
and [independent review](firing-pose-independent-review.md) traced the remaining
arm consumers. Alicia registers a separate arm callback, but the inspected PC
solver is an empty return; six native callback executions and an independent
rerun left the pose unchanged. An active arm/shoulder sway modifier belongs to
the eb0 enemy factory, while a separate attached-model owner reads Alicia's arm
pose without changing it. Those findings do not justify adding a new player IK
solver or claiming original-image correspondence. See the later reports for
the actual-content component and Stage trajectory evidence and its limits.
