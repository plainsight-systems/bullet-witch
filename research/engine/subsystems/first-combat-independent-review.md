# Independent first-combat review

## October 28 acceptance boundary

The user accepted this work for commit as an initial combat baseline despite
serious unresolved AI behavior in the live walkthrough. A deeper original-source
AI investigation is the immediate next work. The review findings below describe
bounded inspections and identified repairs; they do not establish complete AI
or source behavioral fidelity. The October 28 screenshots record symptoms, not
an established cause. This disposition supersedes earlier finite-closure wording.
Checkpoint restoration remains approved deferred work after the AI investigation.


Review baseline: `772f4a4`, including tracked and untracked first-combat work.
Scope: ordinary Alicia WA0 against authored EA0/WG1/id100, with shared provider,
AI, motion, damage, effects, audio, and defeat integration. This is a source and
ownership review, not an independent native-play acceptance result. Final delta
review includes the shared NPC audio extraction, common contact admission, and
melee pose-semantics repair. All three finding groups below were repaired and
rechecked; checkpoint restoration is now explicitly approved next-slice work.

## Reopened coverage correction

The later live presentation audit invalidated this review's implication of
complete source coverage. In particular, the references below to "joint 54"
were a wrong raw-bone interpretation: the producer uses weighted attachment 54
through `600ae0`/`428640`. The original category thresholds remain applicable,
but the eye-height producer required repair. Missing facing, readiness and
weapon attachment paths were also not identified by this review. Current
source coverage and verification status are in
[first-combat-exchange.md](first-combat-exchange.md); the historical findings
below do not establish final combat acceptance.

## Findings

### P2 — Actor shot queries omitted the source exclusion mask

Originally `src/game/combat/projectile_world.cpp:123` and
`src/game/combat/shot_target.cpp:26` constructed actor queries without the source
exclusion bit 2. EA0 Dodge and Blast publish flags `0xA`; the unfiltered query
could select those actors as a nearer receiver, absorb a projectile, and prevent
the eligible actor behind them from being hit. Filtering after selecting a hit
would not repair that ordering.

Evidence: `pass-338-ea0-perception/functions/005f5cb0.c` sets
`local_3c = param_6 | 2`; `0054b1d0.c` combines the query mask with
`0x80000000` and excludes flagged shapes before candidate selection.
`pass-336-shot-assembly/functions/0062ed80.c` passes zero to that same actor
query for player aiming. Projectile `0061b1d0` does likewise. Scenery
`pass-41-camera-segment-math/functions/005f3a40.c` has a zero caller mask;
the `0x3200` additions in sight query `005abf60` do not belong in shot scenery.

Disposition: repaired during review with shared `kShotActorExclusionMask` and
`kShotDisabledReceiverMask` in `shot_target.hpp`. Both projectile and aim queries
now apply actor masks before nearest-hit selection, and scenery excludes the
disabled bit. Independently rechecked both consumers and the real-provider
regression with excluded nearer actor and scenery. The integrator executed
`runtime-tests` successfully after the filter repair.

### P2 — Damage/death decisions use incorrect actor posture/support facts

`src/game/actors/npc/npc_actor.cpp:773` supplies `movement_category = 0` for
every ordinary hit. `receive_melee` likewise omits live posture facts.
`npc_actor.cpp:908` supplies `contact_lost = false` to the shared death-handoff
predicate. An EA0 whose pose is low enough for a different source category can
therefore select the standing damage/death family; a dying actor whose root
motion leaves support waits for clip completion instead of handing over to
physics at support loss.

Evidence: `pass-323-combat-hit-effects/functions/006867b0.c` calls virtual
`+0x30` for the category before selecting lethal and nonlethal ordinary responses;
`00683d40.c` invokes `006030d0` and admits physical handoff on contact loss.
The actual NPC virtual is `00600e70` (vtable `00852334`), not Alicia's
`00600f80`: Squat and selected DamageReaction slots have explicit categories;
otherwise joint 54 eye height relative to actor position is compared with model
height fractions. Generic airborne status alone does **not** prove category 2.
The defect is replacing those observations with constants, not a demand to map
every airborne pose to category 2.

The adjacent Alicia path has the converse error:
`src/game/world/mission_simulation.cpp:137` maps every nongrounded phase to
category 2. Her actual virtual `00600f80` returns 1 only for StateSquat and 0
otherwise, including StateGrapple. An ordinary lethal shot during her jump
therefore incorrectly bypasses the death motion for immediate physical handoff
(and selects voice 1 instead of 0); surviving jump hits incorrectly suppress
ordinary reaction selection. The authoritative vtable link is
`pass-128-player-death-predicate/references.tsv`, address `008523a8` =
`00852378 + 0x30`, with the exported function in that pass. Existing
`alicia-action-transitions.md` independently records this distinction. A retained
crouched stance while another exclusive action owns the actor is likewise not
sufficient to identify the source StateSquat.

Disposition: repairs independently rechecked. NPC category now uses completed
joint 54 and the original model radius, with the DamageReaction slot override;
category 2 has a real physical-death branch. Support loss hands off the completed
pose after animation follow commands. Alicia uses the exclusive StateSquat fact
for category and death selection, returning 0 for other states. The adjacent
melee policy also no longer imposes an unconditional flight rejection: source
`00685170` permits the ordinary Shooter branch before checking blocking states.
The actual-content Blast witness subsequently passed; the final full repository
gate is owned by the integrator and is not certified by this review.

Final admission review also identified a related boundary:
`world_population.cpp:256` binds hit/physical ownership for all EA0, but
`npc_combat_controller.cpp:123` originally bound the common contact/root-motion
owner only for the selected AI family (`world_combat.cpp:169`). Other EA0 keep
default unsupported contact and skip the collision branch of
`npc_actor.cpp:517`, so an ordinary death can immediately
handoff and a melee Blast cannot advance its phase machine. This is outside the
selected id100 witness but was a real admitted-receiver path. Repaired and
independently rechecked: common contact is now bound in
`world_combat.cpp:165` for every EA0 before the optional selected-AI branch.
The controller no longer binds it a second time.

The physics helper's actual-content witness separately exposed missing ordinary
pose semantics for melee death slots 745–748 and Blast slots 839–850. Its bounded
repair in `npc_pose_semantics.cpp` was also independently checked: only traced
aliases receive ordinary semantics, and conflicting bank representations still
reject admission. The witness rerun passed.

### P2 — Ordinary projectile damage omits victim voice requests

`src/game/world/world_combat.cpp:611` dispatches ordinary impact to the health
and reaction owners without any victim-voice request. Neither
`src/game/world/mission_simulation.cpp:113` nor
`src/game/actors/npc/npc_actor.cpp:752` emits one. Consequently a normal bullet
reaction/death can be silent, and an already playing actor voice is not cancelled
as the original damage response requires. The newly added melee voice calls do
not cover this path.

Evidence: `pass-323-combat-hit-effects/functions/006867b0.c` explicitly calls
`0046f4f0` to cancel matching actor voices and `004704b0` to request the selected
action. Ordinary reaction selects action 1, additive response `0x13`, ordinary
grounded death action 0 (when the physical-family branch does not intercept it),
and category-2 lethal response action 1. This is direct damage dispatch, not a
missing motion-event interpretation. `pass-324-combat-world-responses` exports
`0046f4f0.c` and `004704b0.c`, confirming actor-identity cancellation and voice
creation. Reusing the melee boolean helper's actions 1/2 would not preserve
these branch-specific ordinary selections.

Disposition: repaired and independently rechecked for Alicia and all admitted
EA0 audio owners. `HitReception` carries a value-only voice request. The
integrator also found and repaired the broader admission mismatch: all EA0 had
hit geometry, while only selected AI instances had audio. The extracted
`actors/audio/npc_audio.*` owner is now admitted independently of AI; the optional
controller borrows it. Entry retirement destroys the controller before audio,
then population retires the actor. Selected/nonselected event drains are
exclusive, and the world advances each audio owner once. Damage requests cancel
the old voice before the source-selected action. Branch assertions were added
in `health_damage_test.cpp`. The integrator owns the final full build/test gate.

## User-approved checkpoint deferral

Game-over Yes currently maps to a fresh restart; original outcome 3 restores the
checkpoint. The integrator identified this before independent review. After the
review handoff, the user explicitly approved checkpoint restoration as the next
slice. Fresh restart remains a known current limitation, not original retry
parity. This scope decision does not imply acceptance of the combat walkthrough.

## Ownership and algorithm checks

No additional concrete defect was established in these inspected boundaries:

- The pinned upstream Jolt build options match its actual CMake interface.
  Jolt is a private implementation dependency of `bw_physics`; public provider
  headers do not expose Jolt types. Backend includes put `Jolt.h` first.
- `WorldRuntime` owns the only provider step. Actor motion precedes that step;
  physical pose is consumed after it; query publication then precedes combat
  queries. Game health/AI policy remains outside the provider.
- Provider slot generations, pending-command retirement, body/constraint/shape
  teardown, runtime allocator lifetime, and actor physical-owner destruction
  preserve the inspected borrows. Receiver revocation precedes population
  destruction; the player weapon receiver is explicitly unbound.
- EHIT cooks preserve source region/body fields. Instances preserve full affine
  query transforms separately from rigid skeleton bodies. Projectile age/range,
  query-budget rotation, scenery clipping, and actor clipping are reusable
  algorithms rather than one-encounter distance checks.
- Shared behavior execution, per-actor retained native-action state, route
  scratch, source target requests, and query-only navigation separate decision
  state from actor movement. The documented inferred A* remains an inference,
  not an original-algorithm claim.
- Effect program preparation, fixed particle pools, copied sprite publication,
  retained textures, source particle RNG ordering, muzzle-light ownership, and
  weapon/actor audio ownership were checked against their consumers.
- The two C++ guideline corpora were consulted for borrowed lifetime and bounded
  allocation review. Corpus advice is not treated as original-game evidence.

## Evidence limits and outstanding verification

The review sampled original decompiler/instruction exports where behavior claims
affected findings; it did not rederive every native AI action or every effect
opcode. It did not exhaustively inspect every added test or every line of the
large research documents. Nonordinary weapons, general prop destruction,
cross-actor voice arbitration, and full checkpoint restoration remain outside
the implemented ordinary encounter boundary and are not certified here.

The independent review itself made no production edits. After completing that
review, the integrator separately assigned this reviewer six-file lint cleanup:
braces, named source constants, checked fixture optionals and an error-only
teardown diagnostic. Those changes passed scoped fatal tidy; they do not change
combat algorithms. No commit or native launch was performed by this reviewer.
`git diff --check` passed. The integrator's reported Metal/FACT 720-frame two-way
kill and physical corpse witness is separate evidence. The physics helper's
actual EA0 melee witness passed: flight 840, landing 839 at frame 41, and physical
handoff at frame 46 with three RNG draws. Its ignored diagnostic source and
stdout under `research/evidence/reconstruction/first-combat-physics/melee-actual.*`
were inspected independently; this uses real model/motion/collision/recipe data
and production owners, but direct test dispatch rather than a user melee input.
The helper reports runtime contracts and six-unit fatal scoped tidy passed.

There is no separate actual-content support-loss witness, and no human
walkthrough by this reviewer. The final full repository gate remains the
integrator's responsibility. These limits, the approved checkpoint deferral, and
unreviewed broader families prevent treating this document as unconditional
whole-game or visual acceptance.

## Follow-up review: ordinary traversal and light publication

A separate read-only review covered the bounded wait-posture, terminal-action
and staged-light changes after the live walkthrough exposed additional paths.
It checked the final production deltas against their callers and the original
exports, rather than treating the earlier encounter witness as full traversal
coverage.

Wait and randomized-wait now translate their authored posture through the
original `5d5a00` table, as `5bb040` and `5bb320` do before dispatch. No controller
validation was loosened. Terminal action zero is registered by the source as
`noname` (`5b98e0`): it begins with outcome zero, remains incomplete and permits
reconsideration. Admitting that action after a terminal selector result is not
an invented fallback. Review found one source-order defect in the initial fix:
`5025e0` exits descent on a terminal result before its completed-current sibling
rewrite. That rewrite now excludes terminal results, with a regression that
actually forces the parent reconsideration path. Bounds, control-cycle checks,
priority, action ownership and world-clock admission remain intact. The helper
reported and recorded passing isolated action/runtime tests and fatal scoped
tidy in `first-combat-terminal-action/`.

The light fix preserves the existing scene publication boundary documented in
`lighting-scene-producer-contract.md`: source `5f70f0` constructs the view/global
and actor candidate lists before their consumers. Emitter capabilities now
stage copied values in fixed scene-owned slots. They cannot revoke the last
completed publication while a newly admitted actor samples it. Invalid updates
leave both the accepted pending value and published state unchanged; invalid
views are rejected before the batch commits. Release stages retirement and
invalidates its capability, while reuse resets source depth history without
mutating the old publication. The next successful view publication removes or
replaces those lights, so the retained snapshot is not a permanent ghost.
Retirement uses infallible fixed-slot writes after thread/generation validation;
it does not invoke a throwing validator from the noexcept destructor. No clock,
world-step order or renderer ownership was moved. No additional concrete defect
was found in this bounded review. Focused static/spatial/retained receiver,
invalid-input, clear/release/reuse and fixed-storage checks passed, as did fatal
tidy for the three affected translation units; logs are in
`first-combat-light-publication/`.

The added `startup_corpus.cpp` ordinary route retains the Lua-produced starting
pose assertion and passes that exact pose into world admission. Its 900 frames
use ordinary movement input: 330 forward, 360 right, 210 forward. Forced trap
containment remains unset until the later, explicitly labeled callback tests.
The route requires a running mission, spatial progress and at least two live
population members. Checkpoint capture compares against the same-frame copied
population checkpoint. These are real asset/script/owner traversal checks, not
a claim of a native rendered walkthrough or complete combat acceptance. The
integrator owns the actual-content route, native audiovisual reruns and final
repository gate. This follow-up reviewer made no production changes.

## Re-review after the rejected combat presentation

This section supersedes the earlier source-coverage implication and the old
900-frame corpus-route description. It reviews the repaired ordinary
EA0/WG1/id100 and Alicia WA0 paths; it is not approval of all enemy families or
a substitute for the user's walkthrough. No additional demonstrated defect was
found in the following repaired source and ownership boundaries.

The source audit established consequential omissions, not presentation offsets:
retained Shooter aiming and bounded turning (`62e960`/`62fef0`), separate Runner
model facing (`667cc0`), grounded versus generic root-motion consumption
(`664fb0`/`664be0`), ready-duration and upper-owner fire admission
(`62ebe0`/`630400`), and body/upper/additive capacities of 4/2/1 (`5e0830`).
The integrated code now preserves those distinct states. Face requests update
the retained target; they do not snap the model. The narrowed NPC ready command
does not claim the original hardware camera-mode bookkeeping: the inspected
selected NPC has no camera owner or hardware mode-cycling producer, as bounded
in the exchange report.

The weighted locator repair covers eye, weapon, audio and melee consumers.
`428640` constructs a model-local frame from weighted skin points before model
composition; it does not index an ordinary bone. Immutable prepared locator
definitions own their decoded data and can be shared between instances. Each
actor retains its own playback owners, completed pose and skin palette. Calls
borrow those poses synchronously. Model-facing orientation, retained aiming
anchor and physical fallback frame remain separate; an absent body locator uses
the source physical identity basis and current position. Motion events preserve
their separate positive-slot/model-origin fallback rule.

The shared EHIT repair is equally fundamental: original `5fa290` consumes
`4225b0`'s inverse-bind skin palette for model-bind-space triangles. Raw bone
matrices applied bind placement twice. `ActorHitInstance` now accepts a const
completed `SkeletonPose`, selects skin transforms internally and queues copied
affine values. It stores no borrowed pose. Both ordinary and physical player/NPC
publication paths use that contract. Provider commands copy their input,
validate the batch transactionally and discard pending commands when an
instance retires. Query instances retire before their shared cooked geometry;
the world retains sole provider-step authority.

Weapon presentation now preserves the original consumer split:
`615aa0`/`615fc0`/`618410` attach retained flash, light and audio to the barrel
child, whereas `616530`/`617110` emit the separate per-projectile effect at its
authored discharge locator. The emitter copies both placements. Weapon
retirement stops its retained flash/light/audio; the effect world owns detached
instances through natural expiration. No actor-local solver, renderer mutation
or owning pose pointer was introduced by these repairs.

Original-executable witnesses independently checked retained aiming, ten cases
over purchased eye/weapon locator data, and actual-skeleton skin-palette
composition. Locator cases agree within 1.18e-6 and skin matrices within
6.51e-7. These execute the relevant original arithmetic with explicit synthetic
poses; they are algorithm evidence, not proof of live visual fidelity. Their
inputs, scripts and results remain ignored under
`research/evidence/reconstruction/first-combat-visual-repair/source-audit/`.
The translated-bind EHIT regression would fail with the old raw-bone contract;
the detached-effect regression checks survival across retained-flash movement
and retirement. The ownership review also consulted Core I.4/R.30 and
performance MEM.9; those rules support interface/lifetime judgments, not claims
about the original engine.

The revised `original_combat_admission` corpus uses the independently checked
authored start, ordinary forward/right/approach inputs and bounded camera
control. It waits for real incoming damage before returning fire, retaining
assertions for enemy damage, authored enemy death and physical handoff. A
separate explicitly wounded-player fixture requires an actual enemy projectile
to cause player death. The Lua startup/modal/checkpoint/music/GameOver fixture
remains separate; its redundant movement loop was removed because corrected
enemy fire legitimately interrupted that callback fixture. Mock audio voices
receive explicit controlled completion, which establishes no native duration
or synchronization claim. No production aim, health or collision threshold was
changed to satisfy these cases.

The native auditor separately reports first ordinary shot 885, accepted hit
886/body region 3, source death clip 730 and physical handoff 1107, with inspected
standing/falling/grounded frames. Its final barrel frames also establish the
repaired retained-flash placement. See
[the native evidence audit](first-combat-visual-evidence-audit.md) for capture
details and attribution. Its subsequent 1,800-frame moving-player run records
front/side/back tracking, real incoming damage, a second accepted enemy death
and physical handoff. Correctly attributed SDL3 audio is retained, but GPU
readback capture does not establish live audiovisual timing. This reviewer did
not perform those native walkthroughs. The integrator reports exit zero for
the final repository gate and mission-startup corpus; this reviewer inspected
their recorded successful endings in `check-all-repair-complete.log` and
`corpus-repair-route-complete.log` under the same ignored evidence directory.
Earlier audio captures retain their explicitly recorded attribution limitation.
The native auditor's final crouch/stand check remains separate from this review.

One bounded reuse limit remains: the original can update hit-group visibility
after admission, while the current hit instance admits the initial appearance
groups. No selected EA0 action changing those hit groups was established in
this review; this is not certification for future dismemberment or dynamic
appearance families. Checkpoint restoration remains the user-approved next
slice. The current source review is complete for the repaired selected path;
native evidence and user acceptance determine whether the combat slice closes.
This re-review made no production edits, builds, launches or commits.

## Superseding encounter source audit

The preceding selected-path conclusion does **not** establish encounter
completeness. The subsequent walkthrough exposed stationary enemies despite
authored movement requests. The full encounter audit is open; passing the
earlier exchange tests or observing selected shots and deaths does not close
the following source omissions.

The admitted `no_ea_m_000` resource reaches five additional trees:
`sub_ea_magic_no`, `sub_ea_pursue_m`, `sub_ea_escape_m`, `sub_ea_lv01_no` and
`sub_ea_lv01_follow`, totaling 1,893 nodes. Counting companion actions as well
as primary actions adds shooting, ready and voice consumers that the earlier
primary-only inventory did not describe. All their action names have handlers;
that says nothing about whether their underlying producers and receivers are
complete.

| Source boundary | Demonstrated missing behavior at audit entry | Disposition |
| --- | --- | --- |
| `5ce940` → `5cfcb0` / `5d0430`, `5d08d0`, `5cef00` | `NpcSceneRouteFollower` substitutes whole-route clearance/A* for source retained local steering. Native requests select movement at speed .9, then every result reports no movement. A captured clearance hit is the floor, with normal `(0,1,0)`, because the query starts at ground height with body-radius clearance. | Required repair; not original enemy idling. |
| `5b0980`, `5b1200`, `5b0f40`, `5b0a00` | The original retained same-group neighbor service accumulates cohesion, alignment, separation and forward avoidance. Only two navigation configuration fields are decoded; there is no corresponding live neighbor producer. | Required source steering contract, separate from physical collision. |
| `5d1dc0` / `5d1b90` → `5d18d0`, sensor `5ad700` → `5abd60` / `5acae0` | Navigation collapses source return modes into `moving`, dropping the distinction between turn-only and admitted translation. The shared sensor retains traversable/support bytes at environment +154/+155. The native forced-navigation argument bypasses their rejection; it is not a face-direction option. | Required movement-admission boundary, including retained sensor cadence and source ground/obstacle probes. Ordinary physical collision alone does not supply these AI decisions. |
| `5cf0c0` | Original short collision probe/reflected target adjustment is replaced by plain ground projection in the approach adapter. | Required projection repair; a raised clearance sweep alone does not implement it. |
| Hidecome `5b75c0` → `5ac0c0` / `5ac100` | The completion predicate reuses generic ground-level segment clearance. Source instead tests a capsule between origin/target raised by .3, with the actor radius and exclusion mask 0x1000. Repairing local steering alone leaves this separate old floor gate. | Required distinct shared spatial query contract; do not substitute the movement-admission sensor or a raised ray. |
| `5b1790` plus `5b1ce0` | Cover search considers both authored points and live background-model position/radius candidates. `WorldCombat::cover_obstacles()` always returns empty although admitted trees contain hideaway/hidecome actions. | Missing producer; broad destruction deferral does not justify discarding already placed cover. |
| `5bf090` → global `96b2b0` / `5bf280`, tick `5bef40` | The source keeps ten keyed reaction cooldown entries for ten seconds. Flag bit 1 bypasses a failed admission. Current `can_react` ignores flags and has no arbitration owner. Actual admitted reactions with flags 0 exist. | Required shared reaction admission; this is not a one-at-a-time shooting scheduler. |
| `5bd930`, `5be990`, `46e010`, `4aef20` | Source distinguishes motion looping, completion and current StateReaction ownership. The adapter instead makes `reaction_finished = reaction_active && completed` and waits for `!reaction_active && reaction_finished`, an impossible condition. | Required completion/ownership repair. |
| `4aff70` → `6663a0` → StateReaction `+54` → `6659e0` | Flag bit 4 suppresses model-facing updates during a named reaction. The current command drops flags. This field is a facing policy, not an inferred replay flag. | Required for flags 1 versus 5 reactions in admitted resources. |
| `5dc270` → body symbolic motion slots | Named reactions currently look up uppercase compatibility symbols directly as clip basenames. The original resolves a global name-to-slot table before checking the actor bank. The admitted eight-tree union has 29 symbols, 28 resolved; `AI` follows original unknown-symbol no-motion success. | Required compatibility translation; unknown symbol and mapped-but-unavailable clip have distinct outcomes. |
| `5d85f0` → `5abf60` | Ready-gun obstruction uses mask `0x3200`; the adapter substitutes candidate-perception mask `0xc200`. Candidate perception separately combines that value with the base mask. | Required separation of observation query contracts. |
| `5d40c0` → `68b060` | `getEnemyMoveAngle` measures the enemy relative to the movement-destination basis. The adapter instead measures its own destination relative to model facing, without reading enemy position. | Required predicate operand repair; an admitted angular branch depends on its sign. |
| `5aad90` → `5aa150` → `5a6800` → `5a8690` / `504fa0` | The controller discards the perception transition returned by its brain. Original recognition rise/fall selects authored entries 41/42 with priority arbitration, a queued post-action handoff and bounded action-stack semantics. The fall entry contains actual search/reaction actions. | Required event producer/consumer repair; updating knowledge alone does not execute its authored consequences. |
| Perception construction `5d1620`, scans `5dad90`, selector `5d7f60` | The source owns separate primary mode-0 and secondary mode-1 perception state. The brain owns only one. Admitted `sub_ea_escape_m` Followtocharacter records at data offsets 1164/1212 explicitly use secondary selector -12; event priority considers either owner. Ordinary -2 shooting remains primary. | Required independent state and selector routing; do not alias both targets. Both scans may emit recognition transitions. |
| Perception scheduler `450c70` / `5a9190` | Source shifts the shared ten-bucket bank once per world phase. Current `PerceptionSchedule::next` only increments counts indefinitely, changing scan timing and load distribution. | Required world-owned scheduling phase alongside the separate twenty-bucket movement sensor. |
| Behavior scheduler `450c70` → `5031c0` | The behavior evaluation bank also requires one world-phase left shift/last-slot clear. Current `BehaviorSchedule` accumulates reservations indefinitely. Its 60-slot source selection policy is separate from the 10-slot perception and 20-slot movement banks. | Required shared scheduling update, not an actor clock or frame-rate adjustment. |
| Actor-context audio `44bbd0` → `5a88e0` / `5a8930` → world flush `5a8ad0` → `5a6400` | Ordinary footstep actions 0–5 enqueue positional sound stimuli with authored 10/15 metre ranges; message 20001 selects entry 16. Current character audio plays cues but never publishes this perception input. The receiver also owns sound ID, message position and knowledge 0→1. | Required ordinary encounter perception producer and receiver. Weapon cue `618410` passes no actor context and must not be given this side effect by assumption. |
| Routed `5d1b90` versus direct `5d1dc0` movement receivers | The common adapter always applies direct receiver body offset and failure-reset policy. Routed Follow/angular/cover/side instead use zero offset, path-owner-dependent force, aim fallback and navigation-idle completion. `movetoside` was incorrectly forced. | Final review found this remaining required navigation-policy distinction; exact route request readiness is being traced before claiming the force branch inapplicable. |
| Per-owner recognition dispatch inside `5dad90` | Primary transition delivery occurs synchronously before the secondary scan. Advancing both owners before deriving their priority floors can incorrectly admit primary fall entry 42 when the secondary owner was still known at the original dispatch point. | Required ordered producer/admission update; independent ownership alone is insufficient. |
| Camera-controlled StateDie entry `6839d0` → `5cd880` → `5a6400` | The source emits message 8 from physical position to living actors within eight metres, selecting entry 43. Waiting for later GameOver loses this authored defeat response. | Integration now adds the immediate death-transition broadcast and generic message observation/knowledge receiver; native behavior remains to validate. |
| Authored archetype 101 (`ea0`, `wg1`, `ba_ea_m_000`) | World admission enables a controller only for archetype 100 / `no_ea_m_000`. The same-weapon alternate tree adds `sub_ea_lv01_ba` and grenade actions, so removing the filter without completing that resource closure is also incorrect. | Explicit encounter scope/closure boundary; not an explanation for first-yard groups 100/101/111, which all spawn archetype 100. |

Individual firing pauses are source behavior: `5bf980` / `5bfeb0` use retained
per-action randomized burst period/fraction and visibility checks. The recorded
soldier burst intervals overlap. No one-attacker reservation or animation
budget limit was found in this chain; neither explains the demonstrated
navigation failure. Evidence is retained under ignored engine passes 371–373
and `first-combat-visual-repair/behavior-route` / `clearance-route`.

This table records findings against the rejected baseline. It is not a claim
that concurrent integration has fixed them. Source closure, ownership review
of the actual repairs, ordinary movement verification and user acceptance
remain separate requirements.

Source clarification for sound delivery: `5a6400` applies its affiliation comparison only inside the receiver-actor/message-owner equality branch. Ordinary queued sound has owner zero and a live receiver has a nonzero actor; do not add an unconditional same-affiliation rejection. Broadcast selector -5 independently filters the descriptor bit. The four-slot insertion loop was executed from the original image: inserting priority 100 into four priority-1 entries yields `[new, fourth, fourth, fourth]`. This observed source quirk is not a conventional sorted insertion.


Final navigation review also distinguishes the immutable patrol/archive graph
from the live collision-route search. `5af830` allocates and queues a search
request without consulting archive availability; `5afed0` retains two requests
and reaches route state 4 when both are ready. Worker `5ae960` advances initial
placement, directional stepping and obstacle-boundary following. Therefore
CM01's absent navigation archive does not prove this state unreachable. The
existing bounded A* inference cannot supply this different owner's readiness.
This remains an explicit required source boundary while its finite worker
contract is resolved; it is not accepted as a silent direct-route fallback.

The routed/direct receiver map is concrete: Follow, angular, cover and
`movetoside` use `5d1b90`; flock uses it when flags bit 2 is set, and patrol when
its hundreds mode digit is zero. Other flock/patrol cases use `5d1dc0`. Routed
movement uses zero sensor offset and can retain aim-only behavior after failed
movement admission. Its force decision reads collision-route progress, not
movement posture. Direct movement uses the heading-scaled body offset and its
own sensor-failure completion policy. The unusual direct alignment dot product
is present in the recovered source and should not be silently normalized into
a different policy.


The bounded route-worker trace is now resolved through original functions
`5af930`, `5afed0`, `5ae960`, `5db860` and `5db9b0`, with exports in ignored
passes 378–380. Two same-side jobs retain separate pending/processed status and
search phase. Processing one step publishes readiness; it does not imply
arrival. The world service advances queued jobs, while the actor pair owner
consumes copied results and explicitly requeues them. Boundary walking
publishes the prior point before advancing; its initial legal-start probe
deliberately does not overwrite the retained start. These ordering details
were checked against the new isolated worker implementation. A normalization
threshold discrepancy was reported for correction; the source compares
squared length against its original stored constant. This review does not
yet close the pair, world scheduling or native behavior.

The route-side producer also needs the correct geometric observation.
`5ab8c0` selects the lowest-score overlapping capsule contact and reads that
face's normalized world-space triangle normal through `5491d0`. It does not
read a sphere-support result or a summed penetration correction. When its
horizontal cross-product cannot select a side, `5afaa0` consumes one shared
random draw bounded to two. Pair initialization uses the ray-clipped waypoint
from `5abb40`; the later retreat wrapper's additional ground projection must
not be imported into this producer.


Pair review found and corrected a consequential readiness mismatch: the routed
force consumer reads pair phase 4, including while both jobs are requeued; it
does not require both jobs to be processed at that instant. The pair's delayed
publication and periodic refresh remain separate states. The route-side default
and degenerate-vector threshold were also aligned with the original automatic
side selection. The shared lower capsule query raises endpoints by radius plus
height; the normal traversal wrapper compensates its height, whereas worker
queries pass the original uncompensated height.

The final movement receiver trace reaches `666290` through the registered
MoveTo thunk `65c2a0`. It clears explicit Runner stop, copies the destination,
clears the turn limit for this message and captures the horizontal Shooter
anchor. Upstream `5d18d0` emits that message only when the candidate differs
from its retained destination by at least the original 0.0001 squared
threshold. Navigation result zero emits no stop message. Inferring stopped
state from a separate 0.01 travel threshold was therefore an additional
receiver mismatch, rather than original idle policy. The integrator has
replaced that behavior; ordinary Runner distance-to-target rate selection
remains independent.

Contact ranking has a bounded provider distinction. Capsule source `554850`
first keeps the nearest axis-to-face candidate within each original mesh,
then computes signed capsule support separation; the outer collector ranks
those results. Shared prepared world geometry does not preserve every original
provider traversal/mesh-group choice. Its selected-face ranking must retain
that documented adaptation rather than claim exact original face ordering.
Pass 381 preserves the dispatch, triangle callback and final score evidence.


### Expanded repair review disposition

The current implementation was rechecked after the collision-route pair,
outer navigation owner and MoveTo receiver repairs. The demonstrated omissions
within the six-tree archetype-100 closure now have corresponding producers,
retained state and consumers. No further demonstrated omission was found in
that finite closure. This statement supersedes the earlier selected-shot-path
review without extending certification to unadmitted behavior families.

Ownership is explicit in the repaired navigation path: world admission alone
advances pending jobs; each actor retains its own pair, route state, heading
and movement sensor; shared geometry is borrowed synchronously and queried
without controller-owned physical simulation. Pair results are copied,
per-step worker readiness is distinct from pair admission, and cancellation
retires pending work. The capsule face-ranking policy is separate from physical
contact resolution. The original byte without a demonstrated constructor store
has a documented defined-state initialization, not an invented source claim.

Final ordinary native movement/attention/action validation and the repository
gate remain the integrator's verification boundary. This reviewer did not run
a build or launch the game. The alternate archetype-101 grenade behavior tree,
provider contact grouping/tie ordering and approved checkpoint restoration
remain explicitly bounded elsewhere; this review does not silently close them
or certify full level gameplay.


### Dead-body contact and shared lifetime re-review

The later native corpse continuation reopened the previous finite-closure
conclusion: the reconstructed dead-body model offset had a source-backed
consumer missing from custom character contact. Original `6030d0` uses twice
the authored radius when that radius is at most one and the actor is dead;
`604930` subtracts twice that effective radius from the selected total stance
height. Model publication `602b70` uses the same effective radius as its negative
offset. Applying only the model offset allowed the animated death pose and
subsequent physical bodies to begin below valid terrain.

The current shared `character_body_radius` policy now feeds both contact and
model publication for NPCs and Alicia. Population admission raises the physical
origin using the effective radius but passes the authored radius to the actor,
avoiding double application. Alicia receives copied life state from the
authoritative mission damage receiver; action names do not infer death. The
skeletal physics recipe and provider shapes remain unchanged. Physical pose
publication returns the solved model root, and the NPC recovers its game origin
using the retained publication offset for later observations and retirement.
This reviewer confirmed the source formula and these call sites independently.
The integrator separately reports the post-fix native continuation retains all
15 body origins above valid terrain and the corpse remains supported; that is
native validation by the capture owner, not a launch performed by this review.

The helper-authored audio ownership was also independently rechecked. Source
`46f130`, `46f330` and `46f570` agree with bank-specific admission, global
variation history and stopping requests without clearing that history. The
session owns the pool and immutable prepared policies; the admitted world alone
advances requests. NPC retirement destroys the controller before its audio and
before its actor. Player audio uses an expiring emission capability, while NPC
audio borrows the enclosing world's sink. Both retire retained cues and voice
requests before their environment disappears. World teardown precedes session
audio retirement, and device service remains separate from world time. No
additional demonstrated lifetime or ownership defect was found in these paths.

Defeat notifications retain copied authored amounts in the bounded world queue
and use the player's narrow admitted-frame capability to restore recoverable
magic, rather than mutating progression or current magic from a projectile
callback. The test's relocated wounded precondition is applied once, only after
an actual incoming hit, and still requires subsequent enemy damage to produce
player death and physical handoff. This is explicitly a controlled test
precondition; it changes no production health policy and is not presented as
an unmodified native playthrough. No builds or game launches were performed by
this reviewer. Earlier alternate-family and provider-ordering boundaries remain
unchanged.
