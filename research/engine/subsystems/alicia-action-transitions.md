# Alicia action transitions beyond locomotion

This is the bounded source follow-through for the [animation coverage
matrix](alicia-animation-coverage.md), not a claim that selecting every catalog
clip implements its action. Event interpretation is covered separately in
[motion event timelines](motion-event-timeline.md). The working port preserves
original execution placement; modernizing the CPU/GPU split is a later pass.

## Confirmed ownership

The original `MagicUser`, `Damager` and `Runner` are actor-side action owners. Their typed
state registries bind entry, update, exit and request callbacks; state constructors
share a generic vtable, so reading a constructor name or that vtable does not
identify the transition algorithm. Source snapshots: pass110 family owners,
pass114 transitions and pass115 lifetimes. They are hash-bound to the unpacked
Steam executable used by the catalog census.

| Family | Confirmed source behavior | Required presentation contract / unresolved part |
|---|---|---|
| Magic, Ancient Wall witness | `0x60b730` registers the typed Ancient Wall entry wrapper `0x60a890` → `0x69cb80`. Admission checks availability and acquires a retained target; failed admission terminates the action. Success chooses slot 788 (`magic_ancient_wall`) and effect category 1, calls common entry, and saves/reorients the player camera anchor. Exit `0x69cf60` restores that anchor before common cleanup. | Action admission must return a real result; previewing the pose cannot report wall creation. Camera/weapon/effect ownership is scoped to the action and restored even when interrupted. |
| Common magic pose lifetime | `0x613570` sets up optional weapon/facing behavior from state flags, marks the model action flag, selects the requested motion at **frame 1** with a 0.3 blend (the argument is a start offset, not playback rate), and optionally owns an effect handle. Update `0x613880` tests the current motion's completion latch when the state's completion flag is set, optionally runs the state-specific update, then calls `0x5ff410`. Exit `0x6137c0` releases the retained effect, restores weapon ownership, clears the model flag and input mask. | Preserve state flags and source completion, not a universal cancellation-marker exit. `0x5ff410` still drives grounded contact and source actor updates. Magic poses do not suspend grounding by default. |
| Magic input/request routing | `0x613300` consumes explicit request bits, queries selection, dispatches nine distinct admission helpers, and resets the pending selection to sentinel 11. Selection 9/10 and the generic failure branch have distinct behavior. | Nine spell admission helpers and each spell-specific update are not reconstructed by a twelve-clip enum. Resource/magic consumption and created world effects remain separate from animation selection. |
| Rare sustained hit witness (not ordinary Alicia damage) | `0x681760` projects the incoming direction horizontally, falls back to actor forward when degenerate, and selects slot 918/919 (`dm_beehive_ba_l` / `dm_beehive_fr_l`) by the dot sign against the actor basis vector at model offsets +0x70/+0x74/+0x78. It takes a body-setting value and initializes a source timer, then executes grounded correction. `0x681c60` handles subsequent damage input: timer state selects ordinary slot 749/750 (`dm_ba` / `dm_fr`) or the sustained-hit pair; it avoids restarting an already-selected slot. | Incoming direction, timer and hit-class inputs belong to the game action owner. Repeated impacts may change the selected clip without restarting identical motion. Damage amounts, hit creation and combat resolution are not supplied by the pose evaluator. |
| Blast state caution | Entry `0x66e5c0` and `0x66e670` select four-direction tables at `0x856300` and `0x8562f0`, with 0.3 and 0.1 blends. Their slots resolve to `blast_*_m` and `blast_*_e`, **absent from PW0**. Exit clears source flags / only restores ownership after leaving related blast states. | These are shared actor handlers, not proof Alicia has those clips. Do not invent aliases, repurpose unrelated Alicia motion, or admit an unavailable slot as successful. |
| Death and close attack | Typed death selection/lifetime and StateGrapple entry/update are now traced below. | The compatibility `at_stagger` family is a close attack, not proof of a hurt/stun action. Dynamic death handoff and hit acceptance remain explicit dependencies. |

The damage-side direction comparison above is recorded literally. The labels
`front` and `back` in asset names do not justify changing the recovered comparison
axis or sign without the owning basis contract. Numerical source constants and resolved table rows are saved under
ignored `animation-events/family-motion-tables.json`.

## Implementation boundary

Use a game-owned action state that selects a catalog slot and explicitly owns its
entry, update, interruption and exit. Reuse the engine's prepared clips, ordered
pose layers, clock/controller chains and event timeline. Weapon and camera state
are borrowed capabilities with scoped restoration; source world-effect handles
must have explicit lifetime outcomes. Pose-only inspection may exercise a missing
action's assets but must remain distinct from an admitted gameplay action.

The accepted-observation interface below now selects ordinary directional damage
and common magic presentation, with Ancient Wall as a concrete source witness.
It does not implement projectile/health logic inside the animation engine.
Preserve explicit unsupported outcomes for unavailable slots, unimplemented
spell admission and effect creation.
The additional bounded policies below implement presentation decisions. Their
combat admission, world-effect and dynamic-body owners remain separate.


## Implemented accepted-action policies

`src/game/alicia_reaction_policy.*` now supplies bounded value policies for the
actor adapter. This is original game policy, not a new generic engine state
machine. It reuses prepared motion/playback/event contracts described in
[actors and animation](actors-and-animation.md) and the existing `ActionMotionPlan`.
No health reduction, accepted spell creation, hit result or ragdoll is fabricated
by observing an animation. Passes122–128 and131–132 contain the additional
hash-bound traces. The latter two disambiguate ordinary damage from the rare
sustained-hit branch after actual PW0 admission exposed unavailable slots.

| Policy | Recovered lifetime and implementation |
|---|---|
| Ordinary damage | Generic impact admission6867b0 calls66d8e0(direction,class) for an accepted reaction. It creates StateDamageReaction; Runner667910 registers65c380→virtual+68→665880 entry and62a590→virtual+50→664290 update. Selector687e40 projects incoming Y, uses **world+Z** below the source squared threshold, normalizes and compares model+70/+50 with cos(.7853981852531433). Class0/1/2 source tables are `[749,750,752,751]`, `[753,754,756,755]`, `[757,758,760,759]`. Entry starts0/fresh with state+58 blend0. Nonrepeating completion returns ordinary or StateSquat; CAN0 may exit only noncrouched reactions. Source low-health recovery and forced-idle predicates are supplied by their owning gameplay state, not fabricated from the animation. This is the `DamageReaction` policy used by `AliciaActionOwner::begin_damage`; the rare timer branch remains separately named `SustainedDamageReaction`. |
| Rare sustained damage (unavailable PW0 entry) | **Native admission caught918/919 absent in PW0; this is not the ordinary player reaction.** Entry681760 projects the incoming direction horizontally, using model+c0 forward below squared length1.0000001111620804e-6. Dot against model+70 selects918 at nonpositive,919 at positive; nonpositive reverses the facing target. Authored body+158 supplies recovery; the sustain timer is `2.5 * random_unit + .3`. Update681980 decrements both in float seconds. A later accepted impact681c60 resets recovery and selects749/750 only after sustain expires; identical slots are not restarted. Crossing source frame10 from an ordinary reaction selects the opposite sustained pair before the recovery-expiry check. This is a frame-crossing predicate (`5c7170`), **not a threat/proximity test**. Ground correction and original turn toward the target at25.1327419281rad/s remain actor responsibilities. |
| Close attack (`at_stagger`) | The asset name does not mean a damaged actor's stagger. RTTI66be80 identifies **StateGrapple**. Hardware receiver66c140 chooses706 or707 from accepted request bits and a camera-transformed direction test. Entry66bf30 starts the selected state motion at0, fresh, blend.3. Update66c040 first consumes a queued follow-up at its authored frame crossing, then observes completion on the newly current owner; completion wins over CAN0 cancellation. Cancellation preserves the exited motion slot in the return-state request. The API separates `take_followup` from `advance` so an adapter can publish the new owner before querying its flags. Slot708 is an explicit accepted alternate publication; its hardware selection is not yet traced. Grounding continues. |
| Death selection | 687ff0 consumes the accepted impact direction literally (no additional projection/normalization). Nonpositive alignment samples body+124..138 weights against slots731/733/735/736/737/738; positive samples+13c..144 against730/732/734. 6880f0 subtracts weights in table order, strict negative selects, endpoint/all-zero falls through to the final entry. Player vtable852378+30 resolves600f80: current StateSquat returns1, StateGrapple explicitly returns0. This selects740 for nonpositive and739 for positive. Death entry6839d0 starts an explicitly selected slot at0/fresh/.3. Update683d40 hands completed **or repeating** animation to dynamic-body ownership unless actor flag100 blocks it; loss of support also requests handoff. The helper reports that dependency rather than returning to idle or claiming ragdoll implementation. Native player camera/death task, body shutdown and effect lifetimes remain unimplemented gameplay. |
| Common magic | Accepted source state inputs retain completion flag1, weapon scope flag2, camera-facing flag4, special update flag+5c and effect category. Common613570 starts the selected motion at frame1/fresh/.3. Common613880 checks completion only when flag1 permits it, executes optional special actor update and continues grounding. Normal completion and interruption both yield one scope-release obligation; caller restores attachment/effects/input mask and spell-specific camera state in source order. Spells bypassing common entry (for example69cfd0 selects771 at frame0) must not use this policy. Ancient Wall remains the concrete common-entry witness; this contract is not twelve fully implemented spells. |

The direction labels in compatibility names are preserved literally; they do
not rename or substitute the source comparison axis. Death weighted selection
uses the supplied original RNG sample so an eventual game RNG owner can retain
source sequence order. Pause/costume changes preserve these value states in the
stable action owner, independent of appearance preparation.

Focused in-process witnesses cover damage timer expiry and repeated-hit phase,
frame10 ordering, degenerate direction, rejected-update preservation, close
attack follow-up/completion/cancellation, weighted zero/boundary cases, crouched
death selection, dynamic handoff gating and magic completion/interruption.
These tests establish presentation policy, not full native combat acceptance.

## Actor integration contract

`src/game/alicia_action_owner.*` implements the bounded actor adapter below.
The controller owns it and borrows the stable `PlayerActor`; both survive a
paused appearance transaction, so no second playback owner or duplicated event
timeline is introduced. Controller physical admission/exclusion wiring remains
with its implementation owner. Source current state at actor+100
is exclusive: `600f80` distinguishes StateSquat from StateGrapple, and the
respective Runner, Damager, Grapple or MagicUser update owns base selection.
Independent upper/weapon rings remain separate owners; no blanket clearing is
inferred merely because a body action starts.

1. Accept a typed external observation, validate required current movement state
   and resolve every necessary catalog slot **before** relinquishing the old
   owner. Copy policy values to a stack candidate, derive its entry plan and
   check the prepared binding first. Unsupported motion or scope capability
   returns an explicit admission result with no partial body-owner change.
   No debug button silently becomes a successful damage or spell admission.
2. Publish the source entry plan, then mark base selection owned by damage,
   grapple, magic or death. Controller ordinary idle/locomotion requests,
   crouch/evade entry and their playback-rate writes are suppressed while this
   ownership is active. Do not return early from the movement update: advance
   current body/upper/additive/weapon clocks, consume current body root exactly
   once, resolve the existing static body and publish corrected pose/events.
   `5ff410` (ordinary damage664290, grapple66c040 and common magic613880)
   dispatches actor root movement unless the current base controller is both
   repeating (`41eeb0`, controller+0x10) and completion-latched (`41eea0`).
   This predicate reads the retained pre-clock owner, not clip-name suffixes
   or the next sampled pose. It always calls6030d0 for contact; a nonzero
   support-loss result requests5fdb30→5fec70, whose RTTI identifies StateJump.
   End the replaced action scope and enter the existing support-loss fall
   route without a user-jump impulse. Rare sustained damage681980 instead
   calls6030d0 directly; death683d40 owns its separate dynamic-body handoff.
   Treating active actions as frozen poses or always applying root movement
   would both lose source behavior.
3. At the same pre-clock state-update point used by existing traversal, derive
   current-owner completion, CAN0 and frame crossings from its retained sampled
   clocks. For original5c7170, normal forward crossing is
   `previous < marker && marker <= current`; wrapping is
   `marker > previous || marker <= current`. Convert seconds with the current
   clip's FPS. Grapple consumes its pending follow-up first, publishes the new
   plan, **then** queries the new owner for completion/CAN0. Rare sustained damage resets its
   sustain timer only on the literal frame10 crossing. Do not dispatch old
   outgoing controllers' event streams or use their completion flags.
4. Rare sustained damage returns a facing target; controller applies recovered6991f0 turn
   semantics at25.1327419281rad/s before consuming root and grounding. The input
   direction/target is distinct from movement stick direction. Common magic's
   camera-facing and special-update flags remain explicit scoped capabilities;
   acquire/restore real attachment/camera/effect owners, not copied booleans
   that claim those mutations occurred. Ancient Wall restores its saved anchor
   in69cf60 before common6137c0 releases the effect/weapon/input scope.
5. Normal completion or interruption releases policy/scoped ownership once.
   Retain the final body contributors until the next source owner selects its
   replacement; clearing the body ring to bind pose would add a visible snap.
   Resume ordinary selection on the next owner update, not by manufacturing a
   universal stand request in the pose system. Preserve body, policy and event
   clocks through pause and appearance replacement.

The currently implemented controller only has the full static grounded route
and original evade/fall route. A damage/magic admission during an airborne
transition requires its corresponding source handoff, not silently setting the
physical phase to grounded. Expose `requires_airborne_transition` until mapped.
Likewise death reports `requires_dynamic_body` when683d40 requests handoff;
retain/report that boundary instead of returning to walking or inventing a
ragdoll. A common magic request requiring an unavailable scope reports
`requires_magic_scope`; rendering its pose can be a separately explicit
presentation inspection, not successful spell execution.

Recommended adapter verification extends the existing original-root scenario:
start an externally accepted grounded damage reaction, tick past its completion
while walking input is held, and verify ordinary requests cannot replace it
prematurely; verify root/contact and body/upper event clocks continue. Repeat
with grapple completion/CAN0, and a supported common-magic scope test double
that observes one acquisition and one release on completion/interruption.
Switch costume while paused in an active action and preserve its current slot,
clock/timers, contact and camera; resume the same policy. These are additions to
the existing in-process/original-root contracts, not new integration scenarios.


`AliciaActionOwner` preflights each body alias, reports unavailable publications
without disturbing the active owner, and tracks body generation so an unrelated
external body replacement cannot be overwritten by stale action policy. Its
optional RNG input is required only for the rare sustained crossing that
actually resamples a timer; ordinary damage does not consume RNG. A missing
required sample is a retry result, not permission to invent one or consume the
step. Magic scope changes are returned as obligations; process release before
acquire before the next simulation tick. Destructor/lifetime teardown must
explicitly consume `interrupt()` when a scope is active. Dynamic-body and
low-health recovery dependencies retain exclusive body ownership until their
real owner is supplied or an explicit interruption occurs. Source current
skeleton appearance is irrelevant to policy ownership.

### Positive start offsets are frames

Instruction-level follow-through corrects the earlier second-one interpretation.
`5e47a0` forwards its positive start argument unchanged into `41f2f0`;
`41f200` clamps it against raw motion start/end fields at+4/+8 and publishes
current/previous frame clocks. The resource end fields are authored frames,
and the update supplies the FPS multiplier separately. Thus common magic starts
at **frame1**, not one second. `ActionMotionStart::frames` keeps source units
explicit until the actor resolves prepared-clip FPS. Ordinary scripted jump
start and landing use the same frame1 argument; flight and hard landing start0.
Existing initial-zero requests are unaffected.


### Hardware close-attack direction selection

The bounded `0x66c140` instruction audit requires normal active input
(+0x20 bit0) and the A press edge (+0x28 bit1). It creates StateGrapple with
slot706. No stationary-only predicate or direct airborne predicate appears in
this receiver; the state dispatcher still controls which owner receives it.
The reconstruction currently admits this hardware route only while grounded;
this is not evidence that the original accepted it from every airborne state.

Processed input XYZ at +0x40 is compared against squared threshold
`1.0000001111620804e-6` (raw float at `0x8485b0`). At or above that threshold,
normalize by reciprocal length, transform through the current camera basis,
and dot against the model's current +0xc0/c4/c8 basis. A strict greater-than
comparison with float(cos(double at `0x854f48` = `0.7853981852531433`)) selects
707. Equality, backward/sideways directions outside that cone, and absent/tiny
movement retain706. This is the source's 45-degree cone; it does not imply a
hit or require a target. Slot708 remains an explicitly requested alternate,
not a fabricated third hardware rule.

`select_close_attack_variant` exposes that pure selection separately from
button/state admission, using shared vector/matrix math. Its source-equation
witnesses cover the tiny-movement boundary, strict cone equality, camera
rotation and nonfinite rejection. The direction contract retains the original
model comparison basis magnitude rather than silently normalizing it.
