# Live resources and native HUD verification

October 25, 2026. Implementation based on accepted lighting commit
`3e985bc`. This records verification of the implemented resource/HUD/power-up
boundary, not acceptance of complete mission gameplay or live spell execution.

## Acceptance split

The user confirmed the hand repair works and accepted the HUD work. Accepted
scope includes live resources, ring controls, firing/attachment behavior and
the existing pause integration needed to freeze resources and retire/reinstate
the cockpit. The pause menu itself is explicitly not accepted and is the next
completion slice. The current limited menu and its developer review pages are
not the original completed pause task. Post-result power-up presentation has
no explicit acceptance; its broader working-tree verification below does not
authorize including that subsystem in the HUD commit. Live casting, complete
mission outcomes and world persistence remain outside this acceptance.

The accepted-only snapshot removes post-result power-up files and their Profile,
frontend/native-task and build integration. Existing pause admission, reduced
menu residency and shared text rendering remain necessary HUD dependencies.
That isolated snapshot independently builds the complete host and passes all
six CTest suites, formatting, fatal lint of the changed launch composition,
and repository content policy. Local copies of the already-downloaded pinned
dependency sources/archive were used; the working game data and existing build
were untouched. Exact scope, export patch and check logs are retained under
`research/evidence/reconstruction/hud-accepted-snapshot/`. The complete preceding
working-tree gate/corpus below remains evidence for the shared behavior, not a
claim that the unaccepted power-up subsystem landed with the HUD.

The user reopened responsibility placement: framework-owned ring and cockpit
policy, and platform-owned ring interpretation, were missed by the first review.
Those responsibilities now belong to explicit game owners. The completed gates
below cover the repaired binary; the first review's architectural clearance is
superseded by the [fresh boundary review](gameplay-hud-boundary-review.md).
See [gameplay integration boundaries](gameplay-integration-boundaries.md).

## Completed gates

`python3 scripts/check.py all --jobs 8` passed after the final boundary edits:
format checking, host build, all six CTest suites, fatal clang-tidy with LLVM
21.1.8, Python tooling checks and repository content checks. The content check
reported text-only source/documentation with no ignored artifacts. The final
clang-tidy summary reports zero failures across 202 translation units; this supersedes the earlier targeted
log containing a `startup_corpus` failure.

The fresh independent responsibility review closed all six demonstrated findings.
It checked narrow action capabilities, const world/progression borrows, stable
owner lifetimes, one frame admission window, resource persistence through pause
and costume changes, Profile-only purchase mutation, and surface retirement.
Its report retains the unimplemented world/casting/save-task boundaries.

Original-execution comparisons include resource arithmetic, seven ammunition
draw states, the ring's existing channel samples plus 285 expiry frames and
1,175 lifecycle frames, and 50 category-selector motion frames. The additional
ring observations agree within `2.42e-7` for published visible channels; category
offsets agree within `2.87e-6`. These are numerical observations over sequences,
not thousands of independent tests. Unpublished initializer/retired frames are
excluded from visible-channel comparisons as described in the source reports.

Actual-archive offscreen Metal checks rendered the HUD with pause residency
(88 textures peak) and eight power-up states (46 textures peak). Texture and
device-budget charges returned to zero after teardown. The power-up probe also
checks invalid observation rejection, post-retirement draw rejection and
idempotent retirement. The original font, sprites, localization and production
sprite shaders/mip path are used. Offscreen rendering does not substitute for
the user's controller walkthrough.

## Actual-content corpus

The responsibility-repair baseline passed before the subsequent firing repair:

```sh
python3 research/tools/verify_startup_runtime.py \
  build/host-debug/startup-corpus "$HOME/Games/BulletWitch-Steam" \
  --output research/evidence/reconstruction/gameplay-hud/boundary-actual-content \
  --timeout 5400 --texture-mips
```

This reuses the original-content actor corpus, with resource ownership connected
to the existing fixture's world frames. It checks parsed original customization
and weapon settings and verifies that admitted held fire consumes ammunition.
The previous run was intentionally terminated during implementation and is not
a pass. The final responsibility-repair run completed successfully in 186.62 seconds,
including production `MissionGameplay` begin/advance/present phase guards,
pause retention and resume output replacement, followed by all
four live weapon classes, interrupted melee/lowering transitions, forced target
and posture exits, paused admission and resumed audio. Migrated CPU live storage
returned to zero after teardown. Frozen binary SHA-256:
`cb85fe03590cdeb67f438e0e5e02152932a1a3f6256d0a45fad4140eed0e0699`.
This is the source/fixture corpus; its synthetic renderer does not establish
native gameplay or complete world startup. The separate offscreen Metal checks
above cover presentation on the native graphics path.

## Firing repair verification

The subsequent firing repair passed the full repository gate: formatting/build,
all six CTest executables, fatal clang-tidy over 202 translation units and the
content guard. The first run rejected unchecked test-array indices; checked
access replaced them without disabling the rule. Its failure log is retained
separately from the passing final log.

The final rebuilt corpus passed in 251.72 seconds with binary SHA-256
`ad906ebabd8cd6953fa4f3ec14f489fad482b13a96b2a4322b43a861697a11dc`:

```sh
python3 research/tools/verify_startup_runtime.py \
  build/host-debug/startup-corpus "$HOME/Games/BulletWitch-Steam" \
  --output research/evidence/reconstruction/shotgun-discharge/final-actual-content \
  --timeout 5400 --texture-mips
```

This includes 48 component attachment trajectories and 16 actual Stage firing
trajectories across all four weapons, plus the existing coordinator, pause,
melee, targeting and action-exit coverage. Every Stage firing case consumed
ammunition and published finite hand-relative transforms; memory charges returned
to zero. Attachment palettes match the independently advanced identity-root
reference. Finite hand observations are not a grip-quality oracle. The independent
review also reran the original inert arm callback. Full source/probe distinctions
are in [the firing report](weapon-discharge-attachment.md) and
[independent review](firing-pose-independent-review.md).

These headless results support a corrected user walkthrough; they do not
establish original rendered-image correspondence or accept remaining hand motion.
No game window was launched and no user input was sent during this repair.

## Authorized support-hand improvement

The user authorized a standard solver after the bounded original producer,
sampling, hierarchy, modifier and attachment comparisons completed. The original
PC arm callback calls an empty solver. The replacement uses the pinned Unreal
two-bone algorithm reference, original arm-chain names and selected-weapon
targets, with no per-weapon offsets. It is an explicit improvement, not claimed
original behavior parity. See [the solver contract](support-arm-constraint.md).

Independent actual Stage verification completes 16 weapon/movement/targeting
cases and 20,816 pose evaluations. Stationary firing reaches its authored grip
for all four weapons. Reload, melee, evade and ordinary lowering release the
constraint; costume outputs and the right-hand attachment remain unchanged.
Moving class 2 sometimes places the target outside the unstretched arm's reach:
the maximum residual is `0.114786` world units versus `0.473031` before correction
at that frame. This geometric limitation is explicit; the solver does not
stretch bones or change the weapon position to conceal it. Numerical trajectories
and API ownership review are complete. The user subsequently confirmed the
hand repair works; the documented moving class-2 reach limit remains explicit.

The complete original-content corpus passed on the preceding behavior-equivalent
build. Final fatal lint exposed API-contract and fixture checks; named immutable
borrow inputs and explicit optional guards address them. Targeted fatal lint
passes. The final full gate now passes formatting, build, all six CTest suites,
fatal clang-tidy across 203 translation units and repository content checks.
The final original-content corpus passes in 252.977 seconds with texture mips
enabled and zero remaining migrated CPU memory. Its executable SHA-256 is
`7e7b3c46f25ba34e015f89185fcfc5a5225b0fcdba4638a412233be18d758e6c`.
Evidence is `research/evidence/reconstruction/support-arm/full-check.log` and
`support-arm/final-actual-content/verification-summary.json` under the same
reconstruction directory. The final manifest binds production/test source and
gate evidence; earlier lint-failure and pre-final-API runs are superseded.
The independent transition capture retains its own earlier binary identity and
reviewed API-only changes. No game window or user input was used for verification.

## User walkthrough after verification

Start with the normal `./run` command and enter One Shot Kill through the existing
menu. This remains a scene inspection with original completed capability
inheritance, not a completed mission. On an Xbox controller:

1. Hold RT briefly. Ammunition should decrease with admitted fire; release it and
   press X to reload. The magazine fills at accepted reload dispatch and magic
   pays for the missing fraction, before the reload animation finishes.
2. Press B to change weapons, fire a few rounds, then cycle back. Each weapon
   retains its own magazine and cadence. The HUD switches its weapon notice and
   cartridge presentation with the authoritative selection.
3. Click RS to enter aiming and click again to leave. Check reticle changes while
   retaining the already accepted aim/evade/movement animation behavior.
4. Open pause, wait, change costume and resume. Resource values and equipment
   persist; paused time consumes no recovery or action intervals. Navigation
   must not replay as firing, reload or melee when returning.
5. Tap LB/RB to open the magic ring, release, then tap LB/RB again to page.
   Wait for the opening or page transition to settle before selecting. Y is the upper cancel choice; B/A/X
   select right/down/left. RS withdraws the magic action. Holding one shoulder
   while pressing the other expires ring input. Opening/page transitions may
   finish visually after input ownership ends, as in the original.
6. Select an affordable spell. Until its real target/entity owner exists, the
   request reports that missing prerequisite without spending magic or pretending
   to cast. Waiting restores magic only to its current recoverable ceiling.

Keyboard equivalents are F fire, R reload, Q weapon, T aiming, brackets ring
pages and Y/Q/E/R upper/right/down/left ring choices. Ring input owns those face
equivalents while active. Movement remains WASD; camera remains IJKL.

The post-result power-up task is verified independently with real Profile
transactions and original presentation. It is not added to pause or concept
missions for convenience. No fabricated mission success is provided to reach
it interactively. Full root result-to-world-save routing is still future work.

## Walkthrough findings

**Reopened after the initial firing audit:** the user required the stationary
support-hand defect addressed before another walkthrough. The subsequent
same-frame original executable pose/layer/hierarchy comparisons completed
without identifying another effective correction. The user then authorized the
established solver described above. That closes the bounded source investigation
and implements the requested repair; it does not infer rendered acceptance.

The user confirms that the HUD generally works and accepts spell-ring controls
and navigation. This does not establish successful casting or acceptance of the
whole slice. Live shotgun fire sent Alicia's weapon to her right. The source
attachment trace found that the weapon consumer applied an animated export root
again after attaching it to the hand; the shared identity-root policy now repairs
that discrepancy. The user also reports stationary firing jerks the left support
hand off the weapon, while moving fire appears stable. The broader audit covers
all four weapon classes, stationary/moving firing, aim states, firing transitions
and the original modifier inventory. Its registered player arm solver is inert
in this Steam image, independently verified by native callback execution; other
arm consumers were classified separately. No additional effective player-stage
mismatch was demonstrated. Remaining hand motion is not classified as an original
game defect or visually accepted. See [firing evidence](weapon-discharge-attachment.md)
and [independent review](firing-pose-independent-review.md). Previous actual-
content checks covered action admission and settled aiming, not this reported
transient firing pose, so those passes do not close the visual defect.

Ordinary magic recovery is automatic, with no post-use delay. At effective magic
stat index 1 it restores 2 units per admitted world second, stopping at the
recoverable ceiling. The current initial ceiling is 20 while the HUD scale is
100; reload spends current magic without reducing that ceiling. The development
completed profile grants learned capabilities at level 1, not maximum stats or
a full 100-unit magic pool. Source pickup-driven ceiling growth remains outside
the current scene inspection. See [resource evidence](player-combat-resources.md).

## Remaining acceptance boundary

The requested live casting animation still needs a scope decision: Ancient Wall
requires genuine placement and a wall owner; Sacrifice requires an ally. Those
are the only spells affordable at the source initial magic ceiling of 20. Other
spells require the original pickup-driven ceiling growth and their producers.
The HUD cannot supply successful cast observations itself. See
[the architecture decision](../../../docs/architecture/player-gameplay.md#casting-dependency-requiring-a-scope-decision).

Damage, projectiles, spell effects, objectives, checkpoint restoration and full
mission result production are not verified by this slice. No lighting behavior
was changed or re-investigated here. Captures, executable traces and purchased
content stay in ignored research evidence, outside Git.
