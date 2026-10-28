# First combat: visual evidence audit and replacement verification

Status: expanded native movement, pause/restart and supported-corpse verification
complete after the rejected walkthrough; user acceptance remains pending.
The findings below drove shared production repairs by the integrator. The
evidence agent initially changed only ignored diagnostics and this report,
then implemented the explicitly delegated audio, defeat-resource, corpse publication and collision-route
repairs described below. Those repairs require independent review;
this report is not a self-certification of their correctness. User acceptance
remains pending.

## What the previous witnesses actually establish

The retained native fixture is
`research/evidence/reconstruction/first-combat-native/encounter.mm`.
The latest rerun is under
`research/evidence/reconstruction/first-combat-normal-route/native-after/`.
Both directories are ignored evidence. The rerun name does not mean its native
fixture walks the ordinary starting route.

The fixture instantiates Alicia at the first selected group's trigger center,
rather than through the original startup callback. Its player movement remains
zero throughout. After frame 360 it adjusts camera input toward a selected NPC's
placement plus a diagnostic 0.8-unit vertical offset and holds fire through
frame 539. This is a real projectile/damage integration witness with controlled
preconditions, not evidence of enemy tracking a moving player or of the complete
mission launch route.

The distinct ordinary-start corpus check executes the unchanged Lua startup,
acknowledges the instructions, and submits forward/right/forward movement for
900 frames. It checks that the mission remains running, enemies appear, and
Alicia reaches the required region. It uses the corpus renderer and supplies no
native rendered evidence for those 900 frames. Combining its pass with the
stationary native fixture does not establish a rendered normal-route encounter.

The native fixture asserts only that Alicia loses some health, a selected enemy
dies, and a physical handoff occurs at some time. Its booleans do not constrain
facing, upper-body aim, weapon attachment, shot origins, reaction admission,
reaction completion, or the visible pose at those events. It captures every
120th frame, except every third frame between 360 and 450. That sparse record
misses most enemy firing and hit/death transition frames. Nonzero PCM and a
silent retirement tail establish audio execution/lifetime, not synchronization
or cue correctness.

## Images actually inspected

The native rerun frames 240, 360, 393, 420, 450, 480 and 600 were inspected at
their original 960-by-540 resolution. In frame 240 the nearest soldiers are
only about forty pixels high in the distance. Frame 420 shows Alicia's large
muzzle flash; it does not show an assessable enemy muzzle-to-weapon relationship.
Frames 450 and 480 retain distant soldiers, while frame 600 shows fallen bodies.
These establish that actors, effects and eventual deaths render. They are not
sufficient to judge the reported enemy firing pose, tracking, muzzle attachment,
or the complete visible hit-reaction playback.

The user's October 27 3:50 p.m. screenshot provides a much closer view of two
soldiers facing away from Alicia. A single still cannot prove whether they
track over time or establish the timing of a shot. The accompanying live report
is the failure evidence: incorrect firing motion, failure to track, detached
muzzle effects and missing hit animations. None is closed by the previous
fixture's outcomes.

## Replacement verification, without production shortcuts

Reuse the existing native Metal/FACT fixture and original mission startup path.
Keep raw frames, telemetry and audio in ignored evidence. The evidence agent owns isolated capture execution; the integrator owns
production builds. Hashed source/library snapshots prevent competing builds
from changing a running capture. No visible launch or
input injection is authorized by this audit.

1. Start through the original mission script, acknowledge the real instruction
   modal using input, and record the authored player transform and zero initial
   population. Walk through the cave using ordinary movement input, admitting
   the first soldiers through their real triggers. No teleport, forced trigger,
   direct damage, AI target assignment, or modified life/ammunition is allowed.
2. Continue close enough that the enemy body, hands, weapon and muzzle are
   visible at useful scale. Approach in front, cross laterally, then pass behind
   while retaining the target actor identity. Use normal camera input to keep
   the encounter in view. Stop and move again. If the route dies or cannot
   reach the phase, retain that result rather than forcing progression.
3. Record continuous frames through tracking and enemy shots. Correlate the
   source-selected target with actor facing, upper-body aim and actual rendered
   bones. Body and aim need not point identically: compare their distinct
   source contracts, turn limits and admission rules. Deliberately exercise
   upper-body elevation where reachable through normal level movement.
4. Fire short ordinary bursts at an identified enemy, then a sustained burst.
   Log each accepted impact, damage, source response admission/suppression,
   selected bank/clip and frame. Follow a surviving reaction through completion
   and ordinary action resumption, then a lethal response through animation and
   physics. Do not require a stagger for every bullet when source policy
   suppresses it. Conversely, a logged admitted reaction must visibly play.
5. For actual enemy and player shot events, correlate the completed hand bone,
   attached weapon model, authored muzzle frame, projectile spawn and effect
   anchor in the same world/frame convention. Inspect event frames before,
   during and after firing and weapon motion. A finite transform or a nonzero
   flash is not an attachment check. Preserve source offsets if supported;
   never tune placement merely to align this camera view.
6. Verify pause/resume and fresh-stage restart through their existing paths,
   checking actor/projectile/effect/audio lifetime and restored authored
   admission. Checkpoint restoration remains explicitly deferred.

Capture every rendered frame during the close encounter, with event PNGs and
an encoded review clip from the same frames. Do not upsample sparse captures
and present them as continuous playback. Log frame index, simulation time,
inputs, player/NPC identities and transforms, selected actions, animation
banks/frames, aim state, shot and damage events, attachment transforms and
physical transition. Copied observations or isolated diagnostic instrumentation
must not grant broader production mutation authority. Preserve the pre-repair
binary/source hash and equivalent post-repair route/inputs for comparison.

## Completion standard

The reviewer must inspect motion at normal speed and inspect shot/reaction
frames against their telemetry. Record exact frame ranges and visible outcomes,
including failures and source-suppressed events. The initial audit had no passing
replacement native sequence; final capture results are recorded below. Source
audit, finite pixels, nonzero audio,
statistical event counts and successful terminal damage booleans cannot replace
that missing evidence. Human walkthrough remains the final acceptance step.


## Continuous baseline reproduction

A separate ignored diagnostic executable now reuses the real mission driver,
script runtime, gameplay coordinator, world, Metal renderer and FACT output.
The original script supplies Alicia's initial transform and instruction modal;
the probe acknowledges pages through menu input. Original traps admit the NPCs.
The real HUD/pause/message owners use the existing corpus sprite sink: this is
explicitly a world-only video, not verification of UI rendering. Audio uses a
disk device, not injected sound completion. No production APIs were added.

All pre-repair source/header files, static link inputs and shader bytes were
snapshotted with hashes before production repairs began. Diagnostic tooling,
continuous videos, telemetry and images are under ignored
`research/evidence/reconstruction/first-combat-visual-repair/`.

`baseline-route-v1` reproduces the old 900-frame input path with native pixels.
That path reaches a tank retaining wall at approximately x=-42, z=-59 and
stops against it. Actor count and coarse position assertions pass even though
it does not reach an unobstructed encounter. The subsequent attempted forward
movement remains blocked. This is an additional demonstrated limitation of the
old normal-route regression, not a new game collision defect.

`baseline-route-v2` uses ordinary movement south of the tank: 240 frames
forward, then 480 frames right, reaching approximately x=-22, z=-77. From there
it approaches a live soldier using movement input, orbits at a six-unit radius,
and operates the camera using bounded look input toward the actor's copied aim
position. These are diagnostic input decisions, not source AI target changes.
The player fires normally during frames 1620–1859. There is no teleport,
placement change, health modification, forced damage or trigger containment.

Inspected frames 960 and 1200 provide a close view of the first soldier; frame
1800 shows normal player firing. All 481 observations during frames 1080–1560
retain identical facing for soldier identities 1, 3 and 4 while Alicia circles
the first soldier. Identity 1 remains (0.837592, 0, 0.546297); identities 3 and 4
also retain their original directions. The native images and changing player
positions confirm the reported failure to track across player movement. The
closest soldier remains gun-down in the inspected close frames. Firing images
alone do not establish a hit: the visible standing soldier at frame 1800 must
be correlated with actual projectile and reaction receipts.

The baseline is 2,100 continuous frames at 1280 by 720, encoded directly from
each native readback. It is not sparse-frame interpolation. Both capture and
encoding returned successfully, but neither is a fidelity pass. Isolated
instrumented copies of the same NPC implementation add read-only bank, pose,
aim, shot-matrix and impact logging; these do not change production files or
state. The instrumented baseline is assessed below; the repaired equivalent is
recorded against its own hashed source/library snapshot.

## Original-action coverage mapping

Use [the original AI/action mapping](first-combat-ai.md) and
[the encounter scope](first-combat-exchange.md) for expected semantics:

| Visible requirement | Production path to correlate | Required evidence |
| --- | --- | --- |
| Enemy tracks moving Alicia | Perception/target command, retained shooter aim, locomotion turn, final bone pose | Same actor identity across front/side/behind movement; command, body and upper-body pose are independently recorded. |
| Correct firing pose | Ready request, source body/upper/additive banks, fire event | Selected clip/bank/time and rendered sequence before, during and after accepted shots. |
| Flash/projectile originates at gun | Original weapon attachment/locator, completed weapon pose, emission and effect-anchor publication | Shot-frame transforms and visible muzzle; do not substitute an arbitrary skeletal index or tune an offset. |
| Hit animation | Actual collision receipt, source response admission, selected reaction/death owner | Impact/body/health/response followed by clip progression and visible motion. |
| Physical death | Source death animation/support boundary and single physics handoff | Continuous transition from animated body through physical body; terminal life alone is insufficient. |

The selected One Shot Kill soldier has authored life 1. Therefore ordinary
accepted player damage normally requires a death response rather than a
surviving stagger. Do not increase health to fabricate a nonlethal acceptance
case or claim that this mission validates all surviving-hit branches. The
source audit and event receipt must establish the correct response for each
actual shot.


The instrumented rerun (`baseline-route-projectiles`) completes the same route
and records 33 accepted player shot receipts, 33 scenery impacts (user identity
zero), and zero accepted NPC damage receipts. In particular frame 1665's camera
query targets the wall behind the closest soldier, and frame 1668's projectile
hits scenery. Therefore this sequence demonstrates missed shots, not a missing
reaction after an accepted hit. The copied NPC aim point used by the diagnostic
camera is visibly too low for the intended torso shot; the separate source
audit subsequently identifies body eye/aim locator IDs being read as raw bones.
That independent source finding is being repaired by the integrator, not by an
aiming offset in this diagnostic.

Enemy shot telemetry also shows muzzle positions equal to the weapon model
origin. For example, actor 3 at frame 1601 publishes both at approximately
(-23.3993473, 1.49987006, -46.3918343). This corroborates the source audit's
weighted-locator-versus-bone defect. It does not establish what a corrected
muzzle must look like until the native repaired sequence is inspected.

## Repaired equivalent route

`after-route-projectiles` repeats the same 2,100-frame route policy against an
isolated repaired snapshot. Both native execution and video encoding complete.
The copied source aim point changes because the implementation now resolves the
weighted eye locator; there is no diagnostic aiming offset in this comparison.

During the 481-frame orbit, the three original soldiers now have respectively
241, 155 and 242 distinct horizontal facings, instead of one each. Inspected
native frames 840 and 1200 show the closest soldier holding the weapon ready
while Alicia approaches and passes around him. Frames 1424, 1426, 1430, 1446,
1556 and 1560 show his firing sequence during lateral player movement. The
actual world damage path lowers Alicia from 5,000 to a minimum 4,367.06 health.
This route therefore exercises enemy admission, changing pose and damage that
were not established by the earlier stationary witness.

The equivalent camera policy still produces 33 player shots and 33 scenery
impacts, with no accepted NPC damage. The copied half-eye aim point is near the
belt/upper legs in the native image. This result remains an unfulfilled hit/death
coverage requirement, not evidence that accepted damage fails to animate.
A separate ordinary-look torso-aim run is being retained as a distinct input
case; its camera elevation is diagnostic input, not a production aiming or
collision adjustment.

Repaired muzzle and projectile frames are distinct from the weapon model
origin. Their distinct authored locator roles still need to be correlated with
the visible barrel/effect before attachment is signed off. Pose changes, shot
counts and health loss alone do not settle that visible requirement.

The locator discrepancy above is a confirmed additional defect, not an authored
exception. The independent original-source audit distinguishes the retained
flash/light child (barrel/projectile locator) from the separate one-shot effect
(receiver/ejection locator). The implementation used the latter for both. The
inspected shot frames and matrices therefore remain before-evidence for that
final split; they must not be reported as a passed attachment check.

## Hit receiver mismatch exposed by native inspection

The distinct torso-input run also misses. Rather than tune the camera again,
`after-hitgeometry` logs the actual prepared EHIT vertices, selected actor parts
and published query transforms around the real shot. A separate read-only
triangle intersection calculation agrees with the runtime: the shot misses the
registered parts. This is not a replacement collision algorithm.

At frame 1665 the closest soldier stands at approximately
(-25.583, 0.260, -64.688). Its registered head part occupies y=0.889–1.091 and
z=-66.628–-66.385; the registered upper torso occupies y=0.638–1.029 and
z=-66.098–-65.791. These volumes are visibly displaced from the standing
soldier, explaining why aiming at the rendered body hits scenery. The original
source/data audit independently confirms EHIT vertices are bind-model
coordinates and require the completed skin transform. Publishing full bone
transforms applies the bind placement again. The repair belongs at the shared
hit-geometry transform contract, not in aiming, damage or reaction selectors.

This finding reopens the collision-to-reaction witness alongside the retained
muzzle consumer split. Neither is closed by the intermediate repaired captures.

## Final shared-transform and effect-consumer build

The final snapshot includes the shared hit receiver's `SkeletonPose` contract,
which selects skin transforms internally, and separates retained barrel
flash/light/audio from the one-shot receiver effect. `final-route` repeats the
unchanged passive comparison. Alicia takes real enemy damage at frame 954,
reaches zero health at frame 1075 and hands off to physics at frame 1342.
Inspected frames 1200, 1320 and 1440 show the death motion and subsequent
physical body. The continuous run completes without forcing survival.

The previously planned long passive orbit is now lethal because the real
player receiver is correctly placed too. Accordingly, `final-hit-death` is a
separate ordinary-input case: the same original startup and approach, then a
short burst at frames 840–929 while the camera remains on actor identity 1.
It changes neither life, AI, hit geometry nor damage. Its purpose is to keep an
actual accepted enemy death in view instead of allowing the comparison's
passive player death to prevent firing. The completed capture and inspected event sequence follow below.

The final hit/death case completes 1,500 continuous frames successfully. The
first ordinary player shot at frame 885 hits actor 1 at frame 886, body region 3.
The source response consumes its authored one life point and admits death clip
730. The clip advances from 0.033 seconds at frame 888 to 1.233 seconds at frame
960 and 3.233 seconds at frame 1080; physical ownership begins at frame 1107.
Inspected frames 840, 960, 1080, 1200 and 1440 show the standing soldier,
kneeling/falling death, grounded body and physical continuation. No health or
reaction was forced. The real first-shot impact is at the rendered lower body,
using the original half-eye diagnostic look policy without the exploratory
torso offset. Head and torso query bounds now align with the standing body.

Final passive-route frames 980, 982 and 984 show the closest soldier's retained
flash at the gun barrel and the subsequent tracer. Shot telemetry uses the
authored barrel locator for both retained muzzle and projectile-origin frames;
the separately authored receiver effect has its own consumer. This closes the
demonstrated receiver-versus-barrel conflation for the selected weapon.

### Audio evidence limitation

The earlier diagnostic runner set SDL2's `SDL_DISKAUDIOFILE` environment name,
which the pinned SDL3 disk backend ignores. Those captures wrote its default
root output; the files were preserved under ignored evidence. Earlier assumed
per-run audio attribution was wrong. Video and frame telemetry are unaffected,
but those runs do not establish audio synchronization or correct cue selection.
The final bounded moving-player repeat uses the verified SDL3
`SDL_AUDIO_DISK_OUTPUT_FILE` with an absolute per-run path; the aborted attempt
before that correction is explicitly retained and is not a pass.

`final-orbit-sdl3` supplies the final-build moving-player comparison without the
lethal passive dwell: it begins circling at frame 840, finishes the orbit at
1320 and fires a short burst while continuing to observe actor 1. All 1,800
native frames and encoding complete. Inspected frames 960, 1080 and 1200 show
the soldier tracking Alicia as she moves around him; the three soldiers have
209, 150 and 253 distinct facings over that orbit. Actual incoming fire reduces
Alicia's health, and her shot at 1365 admits the enemy's death at 1366. The
physical handoff occurs at 1767. The correct SDL3 disk output is present only
inside this run; no root audio artifact is created. Audio samples are retained,
but this GPU-readback diagnostic does not establish live audiovisual timing.

### Vertical target-height and audio completion

`final-vertical` adds exactly two ordinary input edges: crouch at frame 840 and
stand at 900, ending after 1,200 frames. Native capture and encoding both exit
successfully. Inspected frames 837, 870 and 930 show standing, crouching and
standing again while the nearby soldier retains his weapon aim. The actual
weighted player eye height changes from 1.545 to 0.894 and back to 1.545. The
source half-eye target changes from 0.978 to 0.652 and back to 0.978. The nearby
NPC's retained aim anchor follows that vertical change: its forward Y component
moves from -0.0945 to -0.1485, then back toward -0.0969. No target, pose, height
or health was assigned by the diagnostic. The rendered view is frontal, so the
small pitch change is less visually pronounced than the lateral orbit; its
source-selected target and retained anchor are recorded independently.

The pinned SDL3 backend itself reports F32LE, two channels, 44,100 Hz. Both
corrected disk captures contain only finite samples and nonzero audio. The
vertical capture peaks at 0.9127; the orbit capture's floating-point mix peaks
at 1.3201. In both, the final one second after mission retirement contains only
zero samples. These establish format, execution and retirement silence; no
hardware listening, cue-by-cue judgment, live audiovisual synchronization or
performance baseline is claimed.

## Review boundary

The demonstrated failures now have source-linked repairs and native witnesses:
changing enemy facing/aim around a moving player, lower/raised player target,
barrel-attached flash, real accepted player impact, visible source-selected
death motion and physical handoff. The authored one-life concept mission does
not exercise surviving enemy stagger or every damage direction/variant.
This evidence task does not replace the integrator's production full gate or
original-content integration run, and does not claim new UI, pause/restart,
checkpoint restoration, other enemy families, or audio fidelity coverage.
The world-only clips and read-only diagnostic logs remain ignored. Live user
walkthrough is still the acceptance step; no further diagnostic capture is
required for the specific reported failures based on these results.

## Reopened audit: stationary combatants and source closure

The subsequent user walkthrough found that enemies track and shoot but never
travel. The earlier captures did not validate locomotion. Their successful
shot/attachment/death observations remain specific observations, not acceptance
of the complete encounter.

### Observed movement failure

The existing `final-orbit-sdl3` and `final-vertical` telemetry has exactly fixed
positions for every living selected soldier. Ordinary motion bindings are the
source alias slots (`resolve_action_motion` stores `request.slot`), not guessed
clip identities. Only idle, ready-idle and ready-turn families were selected:
1/12/14, 17/28/30 and 33. There were no walking direction selections. Soldier
bursts overlap, disproving a universal one-enemy-at-a-time firing gate; the
normal repeated-shot spacing is ten 60-Hz observations.

A single bounded diagnostic replay reused the real original startup and ordinary
approach/orbit input, with GPU presentation/readback omitted. Source, headers
and linked libraries were copied into an ignored immutable `behavior-audit`
snapshot before diagnostic-only logging. It made no production edits, injected
no actor state and opened no game window. `behavior-route` completed 1,800
observations with exit zero. Its action/tree, navigation, motion selection and
collision displacement records establish the actual broken handoff:

- Actors select `MovetocharacterAngleNear`, `positioningmoveNear`,
  `flocktocharacterF`, `Followtocharacter`, and their authored companion actions.
  These are not stationary waiting decisions.
- Movement entries supply positive speed, commonly .9, with stop false.
- Every navigation result says not moving. Actor 1 has 459 blocked responses
  out of 480, actor 3 has 343/391 and actor 4 has 359/410. None says arrived;
  the remaining responses also do not move.
- Navigation consequently replaces destination with current position and sets
  stopped true. Idle/turn selection then supplies zero travel direction and
  zero authored root distance. The collision mover receives zero horizontal
  displacement; it is not discarding an otherwise valid walk animation.

A second bounded 600-observation replay logged the unchanged clearance query
and completed with exit zero. The first 40 retained obstruction records all
have normal (0,1,0). At observation 116 actor 1 starts at height .260004, targets
a grounded endpoint at .000004, and clips with radius .25. The result stops at
height .250004, approximately 3.85% along the requested path. This is the floor
being treated as a route obstruction. The integrator and independent AI source
auditor own the source-derived correction; the diagnostic does not prescribe a
guessed vertical offset. Raw records remain ignored under
`first-combat-visual-repair/behavior-route` and `clearance-route`.

### Complementary source/consumer coverage review

The original-source audit is broader than the repaired visible symptoms. The
following distinctions prevent a successful lethal shot from certifying
unexercised consumers:

| Contract | Inspected implementation / source | Evidence and remaining boundary |
| --- | --- | --- |
| Ordinary resource receipt and each accepted projectile | `WeaponResources`, `PlayerResources`, `WeaponEmitter`; original `616530` | Source cadence, partial final burst, per-shot backlog and RNG are represented. Native captures prove selected ordinary emissions; no new all-cadence parity claim. |
| Persistent travel, query ordering, obstruction and actor metadata | `ProjectileWorld`, `CombatWorld`, `ActorHitInstance`; `61ba90`, `61b1d0`, `61bbf0` | Corrected skin-space hit queries now admit real impacts. Captures cover nearby selected actor and scenery contacts; query-budget saturation and every material/body region are component/source checks, not visual coverage. |
| Both directions of health and reaction | `MissionSimulation::receive_player_hit`, `NpcActor::receive_ordinary_hit`, shared damage/reaction policy; `6867b0`, `6879f0` | Original-route incoming damage and player death, outgoing lethal NPC reaction and physical handoff were observed. One Shot Kill life 1 does not exercise surviving NPC stagger/sustained recovery. Reachability of unrepresented branches must be checked against admitted actor data. |
| Completed model, eye, barrel and discharge transforms | Shared `combat_pose`, actor pose banks and weapon presentation; `428640`, `615aa0`, `615fc0`, `617110` | Independent numerical locator evidence and inspected native frames support the repaired split. They do not validate every weapon or animation alias. |
| Impact/discharge sprites and local lights | `CombatEffects`, `SpriteWorld`, `WeaponPresentation`; source rooted effect definitions | Retained flash, projectile and separate discharge are now wired to distinct source consumers. Loaded programs/finite sprite buffers alone are not proof that every particle, contact or cue appears at the correct time. |
| Actor voice and weapon sound | `CharacterAudio`, `NpcAudio`, `WeaponAudio`; `6178e0`, `618410`, `617a30`, actor voice sources | Native PCM is finite/nonzero and retires to silence. Cue-specific timing, original concurrent voice arbitration and hardware listening remain unverified. The actor-audio report explicitly leaves the original two stream/eight memory voice manager and cross-actor history unestablished. |
| Animation-to-physics handoff and corpse queries | `CharacterPhysics`, `WorldPhysics`; `6839d0`, `683d40`, `4f6920` | The selected death sequences visibly hand off. Jolt remains the approved replacement solver; original solver numerical equality is not claimed. Support-loss and corpse follow-up contacts need separate coverage. |
| NPC death retirement | `NpcActor`, `MissionPopulation`; `5a54a0`, `683d40` | **Missing at this audit point.** The source constructor sets a six-second death delay; update later applies nonplayer/flag and camera-relative retirement conditions. Current actor code only hands off physics, while `retire_distant` skips life <= 0. Dead actors therefore retain active population slots until another explicit removal/world teardown. This needs a real source lifecycle owner, not a guessed corpse timeout. |
| Death-source magic recovery | Original lethal `6867b0 -> 4b0000 -> 5a4d40 -> 5a4d90`, registration `612b80`, receiver `60aa70` | **Missing when identified.** The validated attacker receives a queued notification that raises MagicUser recoverable ceiling, capped at 100. `47ef40` obtains the amount from placement `chara` column 27; the actor receives it through the copied placement record. `612f20` initializes the receiver multiplier to 1. Both the notification producer and the narrow resource mutation were absent; the older resource report documented this receiver without implementing it. Actual selected row values and any later multiplier writers still require confirmation. |
| Pause, stage restart and retirement | `WorldRuntime`, `MissionSimulation`, application owner teardown, original-start/restart integration | Source admission gates and reverse owner teardown are present. Existing pause checks cover traversal/population; restart rebuilds script/world owners. These are not a native live-combat pause/resume/restart capture with retained projectiles, firing audio and ragdolls. Checkpoint restore remains the explicitly approved deferral. |

This section records the reopened audit and concrete findings. It is not a
statement that all original combat source branches have been audited or that
all listed boundaries are resolved. The separate AI audit owns behavior-tree
coverage, actor admission, navigation and reachable close-range actions.

### Follow-up source findings during the reopened audit

The death notification is now traced to a concrete resource consumer. The
message constructor `4b0000` retains an amount, `5a4d90` copies it into the
validated source actor's queue, and `612b80` registers `60aa70` for every
ordinary receiver state. That receiver raises MagicUser's recoverable ceiling
by amount times its retained multiplier, with a maximum of 100. It does not
immediately refill current magic. Placement preparation `47ef40` reads the
amount from `chara` column 27; the copied record installs it at the actor field
read by lethal damage. This is reachable ordinary-gun behavior, not a spell
implementation or a speculative pickup feature. The original MagicUser
constructor initializes the multiplier to one; later writers and actual
selected placement values remain a bounded data-verification obligation.

The integrator also found a related corpse-state omission: physical model
publication did not update authoritative NPC placement. Original `683d40`
recovers the actor position from the physical model root minus its retained
model offset. The effective body radius/offset changes for the dead actor.
Retirement and subsequent corpse queries must use that source-derived position;
a correct camera-distance predicate over a stale position is insufficient.

A read-only review of the newly added `DeathLifetime` policy found its ordinary
six-second, strict behind-camera and 5/40-distance gates consistent with the
inspected source branch. Population collection uses copied identities after
actor traversal, then removes combat borrowers, presentation and actor ownership
before the next physics step. This review does not yet validate the new code's
actual-content run, the physical-position correction, or source's separate
timed-effect forced-retirement branch.

The retained weapon sound audit found a separate state-ownership mismatch.
Original wrapper update `6179c0` follows its barrel and updates emitter position;
it does not retire the wrapper on backend completion. `6178e0` tests wrapper
identity and the retained-token flag. Consequently same-action suppression in
`618410` does not depend on whether the native cue is still audible. The inspected
`WeaponAudio::request` additionally tested `output.active`, and tail release
also depended on that backend observation. A naturally completed cue could
therefore change gameplay-owned request transitions. The integrator was given
this exact distinction; it is not an assertion of an observed audible glitch.

One explicit rejected branch is proven outside the admitted ordinary guns:
the existing purchased-content damage-bound witness reports maximum player to
soldier magnitude 750 versus heavy threshold 800, and soldier to player 80
versus threshold 10000. The ordinary receiver's heavy-damage rejection is not
an additional reachable missing branch for those prepared weapons/body scales.
This numerical bound does not establish other weapon or spell coverage.

### Bounded implementation handoff after the audit

The integrator delegated three demonstrated repairs to this audit agent. This
section is therefore implementation provenance, **not independent review** of
those changes. The earlier baseline findings and captures remain unchanged.

- Placement preparation now retains `chara` column 27. A copied first-lethal
  receipt queues that amount for the player source; corpse follow-up hits do
  not repeat it. A separate borrowed `PlayerDefeatResources` capability applies
  the original ceiling-only rule during an admitted resource frame. World
  combat retains at most one interval's active-population defeat notifications
  and drains them at the next admitted actor dispatch; paused frames preserve
  them and world teardown discards them. Queued source delivery is established;
  the precise original intra-frame recipient drain ordering remains a phase
  comparison boundary. No current magic, health, frame, or progression setter
  is exposed.
- NPC common publication retains the source vertical model offset, doubling
  the radius for a dead body whose authored radius is at most one. That initial repair
  did not yet alter the collision radius, leaving the coupled support consumer
  incomplete; the continued-death witness below exposes and follows up that gap.
  It does not reposition a physical skeleton.
  Physical publication recovers authoritative actor position from the completed
  model root minus that retained offset. The constructor also initializes
  radius for admitted actors without a selected locomotion controller.
- Retained weapon request identity now survives natural backend completion;
  source release still clears it when no native voice remains active.

Focused unit expectations cover notification authority, ceiling versus current
magic, invalid input, backend-completed retained requests and authored column
selection. The existing purchased-content encounter now records actual defeat
amounts and requires a real outgoing kill to expand the recoverable ceiling.
Build, formatting, fatal lint and actual-content execution belong to the sole
integrator; no successful result is claimed here before those checks run.

## Collision-route ownership follow-through

The evidence audit subsequently implemented the delegated collision-route job
and pair owners while the integrator retained scene/controller/world wiring.
The job preserves original initialization, straight traversal and bounded
boundary following. Its world-processed readiness is separate from search
completion. The pair preserves two same-side jobs, delayed admission, refresh
and secondary best-point handoff; its following admission persists while either
job is requeued. Independent source review caught an initial output-contract
mistake that equated pair admission with both job-ready flags. That was repaired
before integration and the focused check now distinguishes those states.

The inspected native constructor does not explicitly initialize the secondary
handoff byte. Its reconstructed initial false value is a defined-state
adaptation, not a recovered native store; subsequent set/clear and retention
across cancellation follow the source. Shared vector arithmetic remains in the
math library, and shared collision queries remain outside these route owners.
The isolated worker/pair contract executable passes; it is not native movement
acceptance. The final moving encounter, active-fire pause and fresh-stage restart
capture is prepared and remains held until the integrator freezes the rebuilt
navigation path. No new native result is claimed by this implementation note.

## Expanded-audit native lifecycle witness

The frozen `closure` snapshot records source, headers, 43 linked libraries and
Metal library hashes. Its ignored `closure-native` run completed two continuous
native recordings with exit zero, followed by both video encoders exiting zero.
It uses the original mission script/start placement, real world, Metal renderer,
FACT output and pause owner. Input is ordinary movement, camera, fire, crouch and
menu navigation; no actor placement, target, health, damage, trigger or outcome
is injected. The observer copies positions and presentation state. The native
pause UI runs against a recording sprite output, so this witness verifies its
state/input transition and world suspension, not its displayed UI pixels.

- The first three admitted ordinary soldiers now move. Across the first stage,
  maximum displacement from their initial observations is approximately 20.0,
  9.25 and 4.41 units. Pose telemetry includes changing locomotion clips and
  facing, with the first soldier firing while the other actors choose their
  own actions. The earlier captured invariant positions are no longer present
  on this route. This does not mean every distant or scripted actor must move.
- An accepted enemy shot occurs at frame 766; pause input enters the real owner
  at 767. Resume is observed at 923. During suspension, the probe submits fire
  and movement but requires world time, player position/health/magic/ammunition,
  NPC identity/position/life and accepted enemy shot count to remain unchanged.
  Those checks pass. The inspected adjacent frames show the enemy's barrel flash
  at the firing weapon and the player passing across its view.
- Ordinary outgoing fire hits soldier 1 at 1061, body region 3, reducing its
  authored one-point life to zero and entering the recovered death response.
  Pose telemetry selects death clip 734. The original moving-camera policy then
  follows the next live actor, so this recording alone does not visually prove
  the complete death-to-physics transition.
- A second pause enters at 1133. Ordinary menu inputs select Stage Start and
  confirm Yes; the real owner produces restart transition 2. The world retires,
  mission audio is retired, and a new original script/world starts with zero
  actors at the original spawn. The same session audio/voice manager survives;
  actor identities and mission-owned requests do not survive the world.
- In the fresh stage, incoming enemy fire lowers player health from 5000 to
  3720 by frame 840. The inspected native image shows the blood impact on Alicia,
  with the shooter and its tracer visible. The continued population trace shows
  the soldiers moving after restart. Outgoing death and incoming damage are
  therefore exercised in separate portions of this same native session.

The SDL3 disk backend explicitly reports stereo float32 little-endian at 44.1
kHz. Its captured stream contains finite, nonzero samples and its final full
second is silent after mission retirement. This establishes actual native audio
production and retirement, not hardware listening, cue-by-cue fidelity or live
speaker/video synchronization. Loading time is included in the audio file, so
its duration is not the encoded gameplay duration. A bounded retained-identity
camera continuation is required before extending this result to the visually
completed outgoing death/physical handoff.

### Continued death exposed a physical-contact failure

The retained-identity continuation (`closure-death`) reproduces the real hit at
1061 and keeps observing that same actor through death clip 734 and physical
handoff near frame 1199. It does **not** pass: the corpse falls through the floor.
Its published physical root reaches Y -38.25 by frame 1395. Earlier witnesses
that stopped at a physical flag or changed camera target did not establish a
supported corpse; their scope must not be enlarged retroactively.

A bounded diagnostic copy (`closure-physics-diagnostic-run`) distinguishes the
first violated boundary from a missing mesh or rendering error. At frame 1200,
a provider ray restricted to the physical-terrain layer hits the floor at
approximately Y 0 with normal (0, 1, 0). The two relevant prepared physical
triangles have material 0 and upward winding. All fifteen actual provider body
origins are already below that surface, with Y between approximately -0.077 and
-0.234. The head sphere has radius 0.1 and center Y -0.122, placing it wholly
below the floor. The downward motion therefore belongs to the physical bodies,
not merely their reconstructed skin or authoritative actor position.

The integrator's source follow-through identified the missing paired consumer:
`6030d0` expands the small dead actor's effective collision radius, and `604930`
uses that effective radius when deriving standing height. The earlier delegated
model-offset repair applied the expanded negative model offset while ordinary
NPC collision still retained the living radius/center. That incomplete coupling
placed the death pose below the floor before physical ownership began. The
source-backed collision/offset repair and a repeated physical continuation are
required before this boundary can close. No solver bias, floor offset or
collision-winding change is justified by this evidence.

### Repaired physical continuation

The `supported-corpse` snapshot and `supported-corpse-native-complete` run repeat
that same ordinary route, pause and actual hit at frame 1061. The repair couples
the original effective body radius to both custom collision support and the
model offset, including the player consumer. It does not change Jolt contact
policy, terrain winding or the authored skeletal shape recipe.

At the first sampled physical frame, 1200, the actual provider pelvis is at
Y +0.1457 and the head at +0.13025. All fifteen body origins are above the valid
floor, spanning approximately +0.0295 to +0.1744. By frame 1320 the bodies have
settled and all their observed linear velocities are zero; their positions
remain unchanged through the final body sample at 1830. The reconstructed
physical root settles at approximately Y +0.095145 and remains there through
1848, replacing the earlier unbounded descent.

After the handoff, ordinary player movement goes around the occluding container
while the camera input continues to observe the same actor identity. Native
frames 1560 and 1800 visibly show the intact corpse lying on the ground. Those
images were inspected by both the evidence agent and integrator. This is a
supported physical result, rather than a handoff-flag assertion. The run and its
continuous encoder both exit zero. Captured native audio remains finite and
nonzero, with a silent final second after retirement. The failed continuation
and its diagnostic geometry/body dumps remain preserved for comparison.

The final evidence establishes this original-start encounter's living movement,
tracking/firing presentation, outgoing lethal response and supported corpse,
incoming player damage, actual pause/resume suspension and fresh-stage restart.
It does not claim every reachable AI action was visually exercised, all death
variants were sampled, the original PhysX solver was reproduced, or hardware
listening/audiovisual synchronization was verified. Reachable source closure and
full repository gates are separate integrator/reviewer evidence. Checkpoint
restoration remains the explicitly approved later slice; this native restart
is the real Stage Start operation, not a checkpoint restore witness. No further
captures were run after this bounded repair verification.
