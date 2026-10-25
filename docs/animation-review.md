# Reviewing Alicia in the running game

The running build connects traversal, aiming and weapon/attack presentation to
live controls. The pause menu supplies explicit previews for reactions, spells,
individual body/upper/additive/weapon clips, and expressions. Live fire and reload
use authoritative ammunition and magic; explicit menu previews remain isolated
presentation review. Neither route creates hits, projectiles, spell effects or
combat outcomes. The
[acceptance report](../research/engine/subsystems/alicia-animation-acceptance.md)
separates native probe evidence from interactive review.

## Start the review

From the repository root:

```sh
./run --max-seconds 3600
```

This builds the current Mac host and launches fullscreen for up to one hour.
Plain `./run` uses a five-minute limit. Add `--windowed` if preferred. Do not use
`--inspect-camera` for these checks: it selects a fixed inspection camera.

Confirm through the title screen, then select **Concept Mission → One Shot Kill
→ Normal**. Wait for the loading screen to finish. Use A/Return to confirm and
the D-pad/arrow keys to choose entries. The controller must be connected to macOS,
and the game window must have focus.

## Current bindings

| Action | Xbox controller | Keyboard / mouse |
| --- | --- | --- |
| Move relative to camera | Left stick | W/A/S/D |
| Walk / run | Partial / full left-stick deflection | Movement keys give full input; no separate walk modifier |
| Look | Right stick | I up, K down, J left, L right |
| Evasive leap | LT | Space |
| Toggle crouch / stand while grounded | Left-stick click | C |
| Fire (held) | RT | F |
| Reload | X | R |
| Melee presentation | A | E |
| Switch weapon (grounded) | B | Q |
| Toggle ordinary / targeted camera mode | Right-stick click | T |
| Open/page magic ring | Tap LB/RB | Tap `[` / `]` |
| Ring right/bottom/left choice | B/A/X | Q/E/R |
| Ring cancel | Y or right-stick click | Y or T |
| Open pause / resume | Menu | Escape |
| Menu navigation | D-pad or left stick | Arrow keys |
| Menu confirm | A | Return or Space |
| Menu back | B | Escape in front-end menus; see pause exception below |
| Toggle fullscreen | — | Control–Command–F |
| Mouse look / mouse attack | Not applicable | Not bound |

LT/Space is the directional evasive leap, not a generic jump button. Release it
between attempts: it is a held action and can request another evade after
returning to a grounded state. RT is fire intent; LT is not aim. The desktop
letters are host review aliases. Xbox View and mouse look/attack remain unbound.
Release the shoulder after opening the magic ring, then tap again to page;
holding one shoulder while pressing the other closes it. Face buttons select
directly, after the opening/page transition settles; the sticks and D-pad do
not navigate the ring. The user has confirmed these controls work. RS/T cycles the two original unenchanted targeting modes. First click selects
the targeting camera; second click leaves targeting immediately and returns to
the ordinary camera through the source quarter-second transition. Weapon posture
can remain raised afterward; that separate post-fire/upper-animation lifetime is
not the targeting mode. Enchanted extra modes and target/hit resolution are not
implemented. While
raised, horizontal look also drives the authored idle-turn animations. The host
uses normal look and the recovered initial sensitivity; saved sensitivity and
inversion options are not yet wired.

The pause root contains **Resume**, **Costume**, **Weapon Class**, and
**Animation Review**. Costume and Weapon Class publish a candidate transaction
and return to the pause root; failure keeps the old resources and displays an
explanation. B returns from a submenu; from the root it resumes. **Menu or Escape
resumes immediately from every pause page**. Space can confirm without causing
an evade on resume; release LT/Space before making a new evade request.

Animation Review contains named action families plus Body, Upper, Additive and
Weapon clip pages drawn from the actual prepared catalogs. Clip pages contain
twelve entries and Previous/Next navigation. Incompatible pose representations
remain visible but disabled: body/upper use proven ordinary poses, Additive
uses proven deltas. Unresolved consumers are disabled in every bank. Accepted selections resume in the
same scene; rejected selections stay paused. The lower-corner status identifies
the active preview. **Stop preview** restores its entry placement and locomotion;
RS/T can also release a menu-created targeting selection. Check that the camera
returns to its ordinary field of view after the second click even if Alicia's
weapon remains raised. **Next phase (synthetic)** supplies an explicitly synthetic external observation
for spell phases that would otherwise require gameplay. These are review
controls, not completed mission logic.

The Weapon Class page lists **Gunrod (default)**, **Shotgun**, **Cannon**, and
**Gatling**, in that order. The paused selector resets action presentation to
neutral. Normal B/Q instead cycles through them with the original upper-body
switch animation, preserving body clocks, aiming, stance and physical state.
While idle, the body follows that switch animation; while moving, locomotion
continues underneath it. The selected weapon changes at the start of the action,
as in the original. All four resource sets are retained during mission loading;
normal switching activates a selected instance without pausing the scene or
rebasing its clock. Mission state owns selected availability/upgrade levels;
each weapon retains its own magazine and cadence across switching. Reload spends
magic according to the missing magazine fraction. Magic regenerates without an
ordinary post-use delay, up to its current recoverable ceiling rather than the
HUD maximum of 100.

## Checks and expected results

1. **Movement and contact.** Move gently with the left stick, then fully, and
   release. Keyboard users can check running and stopping. Walk toward a wall,
   continue diagonally along it, and cross nearby slopes or low steps. Alicia
   should remain supported, stop or slide at solid geometry, and return to idle
   after release. This checks the custom static contact path; movable/destructible
   props and full mission behavior are not implemented.
2. **Camera and directional evade.** Orbit with the right stick or J/L. From a
   fixed view, move forward, backward, left and right; briefly press LT/Space
   during each direction and release it. Repeat after rotating the camera.
   Check takeoff, flight, landing, return to movement and the attached weapon.
   Running toward the camera into evade remains a specific visual-parity issue:
   record an unexpected facing reversal rather than treating it as accepted.
3. **Crouch.** Stop on the ground, click the left stick or press C, then move.
   Press again to stand. Expect the crouched pose and movement; crouching is not
   admitted during an airborne/exclusive action. An evade from crouch returns to
   the standing evade path.
4. **Pause and resume.** Pause while moving or in an evade. Try movement/look
   while paused: the player and camera should stay frozen. Resume, release
   LT/Space, then make a fresh input. The resume input should not also launch an
   evade. Active game cues pause with the scene.
5. **Costumes.** Pause → Costume, choose another of the six entries, confirm,
   then resume. Repeat walking, crouching, turning and evading. Position, camera
   and the paused action should survive the swap. Check attachments, outfit
   motion and obvious mesh failures. Some dress/body surface intersections
   remain known; the source secondary-bone solver is not a guarantee of cloth
   surface clearance. Restart, power-up and mission-exit options are not wired.
6. **Idle and face.** With Original selected, stop and orbit to the front. The
   stand motion's authored facial events can close and reopen the eyes; they may
   be difficult to see at the gameplay camera distance. The native close-up
   verifies this path. Costume indices 3 and 5 have no authored facial shape
   streams, so identical facial deformation is not expected there. Waiting
   motionless for just over 60 seconds exercises the standing special-idle
   transition; movement interrupts it. This is not a separate random-blink system.
7. **Listen.** Move and perform separate evasive leaps. Listen for movement cues
   and an evade voice, their timing, distortion and pause/resume behavior.
   Voice arbitration can suppress or vary a request; a new voice on every
   press is not required. This is the first subjective listening check; native
   scheduling success alone did not establish audible balance. Menu BGM and
   confirmation sounds are still separate work.

8. **Weapon presentation.** Hold RT/F, release, then press X/R and A/E
   separately. Observe raising, the admitted discharge/reload animation and the
   close strike. Move toward the current facing direction and try the directed
   strike. B/Q cycles weapons while grounded; each change should retain position,
   camera, costume and aiming state, and play the switch animation. Check it
   standing, crouched, moving, and with the weapon raised. While stationary,
   hold RT and turn right then left, then look up and down: the gun should follow
   pitch, Alicia should retain floor support, and no unsolicited jump should
   replace the aiming pose. Check the status line
   for unavailable animation
   bindings rather than expecting a substituted clip. The live HUD reports real
   ammunition consumption; no hit result follows from these animations. The
   current walkthrough found shotgun weapon displacement and stationary
   support-hand jerking that was absent during movement. The duplicated Shotgun
   root transform is repaired; the all-four-weapon firing and modifier audit is
   recorded in [the firing report](../research/engine/subsystems/weapon-discharge-attachment.md).
   The user confirmed the subsequent authored-target support-arm repair works.
   That authorized improvement is documented separately from original parity;
   the unfinished pause menu is not accepted with the HUD work.

   For the reported interruption, click RS/T to target, release, then click again
   to leave targeting. The camera should return to ordinary view independently
   of the retained gun posture. When the gun begins lowering, press A/E: the
   lowering upper-body action should retire as melee starts, without continuing
   over the strike. RS/T during that melee is not admitted by the original
   Shooter state. Repeat with all four weapons. Costume 5 remains visibly bright
   in current captures; that appearance has not been accepted or claimed fixed.
9. **Catalog and reaction review.** Pause → Animation Review. Select a named
   reaction or an actual Body/Upper/Additive/Weapon clip. Review the pose in the
   scene, then use Stop preview before another sequence. Try every page, including
   the final short page. Facial preview capability depends on the costume;
   unsupported expressions remain paused with an explanation.
10. **Spell phases.** Select a named spell preview. Reopen Animation Review and
    choose Next phase for an external observation. Thunder uses one request to
    accept a target, followed by five cinematic acknowledgements. Meteor uses
    two cinematic acknowledgements and one world-completion acknowledgement.
    Tornado uses one camera handoff, then waits for its animation to finish. That action is deliberately synthetic; it does not prove a target,
    effect, cinematic owner or mission outcome. Stop preview restores the entry
    placement. Death previews likewise do not supply the dynamic-body handoff.

## Remaining gameplay boundaries

There are no enemies or combat outcomes in this bounded traversal route.
PlayerResources owns live ammunition and magic; presentation consumes snapshots
and a narrow action transaction capability. Projectile spawning, damage, spell
world effects, full targeting modes and dynamic death handoff remain absent.
The original magic-ring controls are connected; requests missing genuine target
or world prerequisites report that boundary without spending magic or pretending
to cast. Campaign checkpoint selection and full mission callbacks remain separate
work. `--inspect-camera` changes a camera, not the checkpoint.

The interactive route now reaches the prepared presentation mechanisms, but
compilation, unit tests and offscreen probes are not substitutes for user review
of the resulting motion, timing, sound and appearance. Record rejected visuals
or unavailable bindings explicitly; do not treat them as completed fidelity.
