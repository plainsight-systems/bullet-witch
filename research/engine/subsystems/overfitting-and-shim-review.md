# Overfitting and shim review

Reviewed October 24, 2026: uncommitted tracked changes and authored new files
against `1b4e94e4e92c776b6de74c60918bc590fca4d4ba`. This records the working-tree
state inspected that day; subsequent changes require checking the cited
functions again. Line references below identify that reviewed state.

The requested adversarial review concerns demonstration-specific substitutions,
duplicated policy and missing ownership, rather than another correctness/test
pass. It inspected the changed composition, actor/controller, animation,
collision, rendering, audio and resource paths, with deeper source comparisons
at suspicious boundaries. No game was launched or controlled and no production
code was changed for this review. It does not establish instruction-level
equivalence for every reconstructed routine.

## Current remediation and acceptance

The user made all remaining findings blockers, then explicitly allowed only the
initial-position shim to close this slice. That narrow exception does not cover
other ownership, lighting or fake world-readiness substitutions.
The findings below preserve the original inspected state; this table records the
subsequent scoped re-review, not a claim of whole-engine parity.

| Boundary | Current disposition |
| --- | --- |
|1 ordinary weapon residency/selection | Corrected and scoped re-review cleared. Four retained mission resource owners; original selected-instance recreation; no normal-action global pause or deadline reset. Actual-content rollback, no reacquisition/upload and native independent camera/audio progression pass. See [residency](weapon-selection-residency.md). |
|2 targeting/review ownership | Corrected and scoped re-review cleared. Source targeting/camera owner is independent of raised lifetime; inactive Stop is status-only. Native GameController snapshot evidence covers second-click exit; physical Xbox confirmation remains pending. |
| Pose admission additional concern | Corrected and scoped re-review cleared. Unknown is default; traced canonical receiver identity admits135 ordinary/17 additive resources and leaves9 unresolved. Unknown actor/remapped publication negative tests pass. |
|3 initial placement | Sole explicit user-authorized shim, isolated in `initial_placement_shim`. Exact fresh mission/stage guard; checkpoint calls reject; no hidden Stage fallback. Scoped re-review cleared under that exact user exception. Full mission start remains incomplete, with removal conditions in [mission admission](mission-start-admission.md). |
|4 character lighting | Corrected and scoped re-review cleared, including the final original RGBA ranking edge. Original spatial SH plus current PHIT emitters use source max-four direct selection and original material equations. Actual-data source witnesses, native publication and movement across light influence boundaries pass. Future successful-shot/world-owned emitters remain explicit gameplay dependencies, not fabricated preview outcomes. See [lighting](character-light-sampling.md). |

The subsequent physical lowering/melee report prompted a fresh source-state
review beyond the original table. The complete currently admitted Shooter matrix
and synchronous entry delivery are now cleared by that scoped review; see
[player Shooter state dispatch](player-shooter-state-dispatch.md). Material
consumer review also corrected AMOC green and family-specific environment
visibility: static PS225/226/228/229 do not inherit actor shadow visibility.
That specific cross-family correction is cleared. These findings do not resolve
the still-visible PW5 brightness complaint; same-input shader equivalence and
original same-scene visual parity are distinct. Exposure diagnostics and current
combined verification are recorded in the [acceptance report](alicia-animation-acceptance.md).

## Original findings

All four findings were P2 architectural issues. The original descriptions and
source evidence follow; their historical dispositions must be read with the
current acceptance table above.

### 1. Ordinary weapon switching uses the paused preview loading contract

`src/framework/application.cpp:282–308` pauses StageInspection for normal
B/Q switching, performs a synchronous replacement, then rebases the host clock.
Stage pause also pauses game audio. `MissionLoading::prepare_weapon` in
`src/game/mission/mission_loading.cpp:222–237` reloads model/support and constructs GPU
residency; `prepare_weapon_class` repeats this for each class change, while
`commit_weapon_class` retires the previous resource owner.

This fits ordinary action activation into a paused appearance transaction.
Returning to an already visited weapon repeats acquisition/decode/upload, and
unrelated simulation/audio is suspended while that work is discarded from
elapsed game time. Correct switch animation does not establish a correct
resource or clock contract.

Recovered `603900` and `615900` legitimately retire and recreate the selected
**model instance**. The correction is therefore not a prohibition on model
creation: separate prepared resource residency from selected instance/playback
activation. Normal selection should use the game action owner without the
menu's global pause/clock reset. Keep paused replacement for actual menu work.
Verify repeated switches while independent simulation/audio advances, including
absence of repeated resource acquisition when reusing prepared classes.

Source inspected: ignored
`research/evidence/engine/pass-70-player-weapon-bindings/functions/00603900.c`
and `00615900.c`, plus
`pass-117-alicia-shooter-policy/functions/0062ed80.c`.
See [action policy](alicia-action-policy.md#normal-weapon-cycling-presentation).
**Disposition: planned ownership correction.**

### 2. Live intent and preview state share policy and reset ownership

`StageInspection::prepare_review_observations` in
`src/game/developer/stage_animation_review.cpp:366–380` supplies real weapon settings,
merges `held_aim_mode_` and `review_raised_` into accepted aim refresh, and injects
synthetic spell observations. `present_input:407–412` resolves the two booleans
with a special toggle expression. `stop_review` and neutral weapon replacement
clear both. `PlayerController::restore_review_state` additionally interrupts
owners, zeros velocity and forces grounded state. Normal fire/reload presentation
directly invokes accepted-action policies from the same Stage composition.

The permitted review harness is not the issue. Its synthetic spell observations
are guarded and visibly labeled; this review did not find ordinary gameplay
spontaneously fabricating world/cinematic completion. The issue is that every
live action must coordinate with separate review booleans and reset paths.

Normal input should feed a player/weapon intent owner that publishes accepted
actions and presentation state. A separate review session can explicitly
override those inputs and restore its entry state. Effective targeting/aiming
must have one owner; synthetic observations stay in the review session. Exercise
review-to-live transitions, not only isolated successful previews.

After this review, the lead's source trace separately established that original
`62ed80` cycles targeting mode through `617500` and requests camera behavior
through `604600`, separately from raised-weapon lifetime. The reviewed RS/T path
only refreshed the raised timer. At that point the targeting/camera contract remained under remediation.
The subsequent scoped re-review now clears it, as recorded in the current table. See [action policy](alicia-action-policy.md) and
[review lifecycle](animation-review-lifecycle.md).
**Original disposition: under remediation. Superseded by the current scoped clearance above.**

### 3. Normal launch supplies a mission-specific spawn instead of script state

`src/framework/application.cpp:488–492` constructs the actor with
`kOneShotKillInitialPose`. A separate C++ copy of the three checkpoint ranges,
`one_shot_kill_placement` in `src/game/mission/mission_loading.cpp:40–46`, is not called
by the production launch path. StageInspection also uses the initial constant
as its playerless camera fallback.

The recovered `cm01_00` script's `start()` reads `_GetCheckPoint()` and calls
`_SetPlayerPosition` for the three ranges. It then sequences cockpit/player
visibility and game/input pause/resume. The copied coordinates are faithful;
their ownership and delivery are not reconstructed. Traversal tests supplied
with the same C++ constant do not validate that script-to-spawn boundary.

Keep these coordinates as review fixtures. Implement typed script placement
through the mission's spawn/teleport owner before checkpoint/retry or another
mission grows this C++ lookup into a second mission definition.

Source inspected: ignored
`research/extracted/scripts-recovered/cm01_00.lub--105c3988e33f.lua`.
See [mission launch](mission-launch.md).
**Disposition: authorized bounded traversal scaffold; remove at mission-start
integration, not evidence of a newly broken animation.**

### 4. Actor lighting substitutes static-stage constant irradiance

`StageInspection::append_player_draws` in `src/game/mission/stage_inspection.cpp:742`
uses the static surface `environment_parameters` producer. Lines 302–306
synthesize constant irradiance from stage ambient color; actor location does
not enter this evaluation. `MissionLoading` retains character/enemy `_cl` and
`_el` assets, but they have no lighting consumer.

This displays the actor but does not reconstruct character-light sampling.
Changing locations or adding NPCs carries the substitution forward. Material,
costume or shadow tuning cannot replace the missing lighting producer.

Implement a character/environment light sampler that publishes into the existing
renderer parameters. Trace the SHLT sampling/update consumer first; if the
bounded trace cannot recover it, document a concrete reference implementation
for that unresolved contract. Do not compensate by tuning stage ambient or
individual costume materials.

Source inspected: ignored
`research/evidence/engine/pass-mission-load-first/functions/00568030.c` and the
retained load path. That function proves load ownership, not sampling equations.
See [stage content](stage-content-decoding.md#native-load-call-boundaries).
**Disposition: documented missing consumer; planned fidelity work.**

## Audited-area dispositions

| Area | Result |
| --- | --- |
| Aim references and weapon attachment | No additional unsupported sign patch established. Distinct recovered transforms are not defects because they look unusual. |
| Pose layers, root motion and shared math | Reusable prepared owners exist. No basis to replace recovered algorithms wholesale with another engine. |
| Alicia secondary tables | Original topology/capsule compatibility data feed reusable SecondaryMotion; not per-costume tuning. |
| Character collision | Source contact/probe policy is separated from geometry queries. Dynamic support remains incomplete; source constants are not arbitrary tuning. |
| Placed convex preparation | Concrete pinned Jolt visible-face/horizon algorithm inspected; not a bounding-box substitution. |
| Shadow correction | Pinned Unreal slope clamp and optimal-bias functions inspected. Perspective adaptation is explicitly inferred; no scene-specific depth offset finding. |
| Material selector tables | Appropriate source-program compatibility boundary. Missing actor-light producer is separate finding 4. |
| Audio | FACT/FAudio and FFmpeg behind interfaces; selective cue closure is derived from bank data rather than a list of the observed waves. Unsupported streaming cases reject. |
| Costume replacement | Candidate preparation/atomic publication suit the authorized paused operation. Ordinary switching inherits the wrong clock contract: finding 1. |
| Review clip representation | Additive receiver-slot mapping and the witnessed unaliased delta exception are documented compatibility evidence. |

Reference source inspected includes
`research/evidence/references/convex-hull/ConvexHullBuilder.cpp` at Jolt
`9962c11a647324cc55dc1d29f5bb87e1021cdf23` and
`research/evidence/reconstruction/character-shadow/unreal-vsm-directional.ush`
at Unreal `f45729c0b6f097038e72b52d366a62baeca3275b`, specifically
`ComputeDepthSlopeDirectionalUV` and `ComputeOptimalSlopeBiasDirectional`.
See [placed collision](placed-physics-collision.md) and
[shadow presentation](stage-shadow-presentation.md) for applicability and
documented departures.

One bounded generalization risk remains: `prepare_pose_semantics` defaults
unclassified clips/other owners to ordinary poses. The inspected Alicia corpus
supports its current compatibility entries, including `shoot_stand_fire_add`;
that is not a general classifier for future actors. Preserve unknown semantics
or supply actor-specific receiver metadata before NPC reuse. This was not
reported as another demonstrated current-content defect.

The earlier scheduling defects are not repeated as the outcome of this review.
The central finding is that the shared animation mechanisms are more reusable
than the current gameplay composition. Resolve these owning contracts before
extending the demonstration path with more exceptions.
