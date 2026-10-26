# Reviewing Alicia in the running game

The running build connects traversal, aiming and weapon/attack presentation to
live controls. Retained diagnostic code supports previews for reactions, spells,
individual body/upper/additive/weapon clips and expressions, but its new
user-facing interface is not implemented. Live fire and reload
use authoritative ammunition and magic; explicit menu previews remain isolated
presentation review. Neither route creates hits, projectiles, spell effects or
combat outcomes. The
[acceptance report](../research/engine/subsystems/alicia-animation-acceptance.md)
separates native probe evidence from interactive review.

## Start the review

From the repository root:

```sh
./run
```

`./run` is untimed and fullscreen. `--developer-views` selects the retained
diagnostic build configuration, but currently admits no new debug interface.
The [initial tools design](architecture/developer-tools.md) is separate from
normal pause; no debug activation key or layout has been selected.

Confirm through the title screen, then select **Concept Mission → One Shot Kill
→ Normal**. Wait for the loading screen to finish. Use A/Return to confirm and
the D-pad/arrow keys to choose entries. The controller must be connected to macOS,
and the game window must have focus.

## Current bindings

| Action | Xbox controller | Keyboard / mouse |
| --- | --- | --- |
| Move relative to camera | Left stick | W/A/S/D |
| Walk / run | Partial / full left-stick deflection | Movement keys give full input; no separate walk modifier |
| Look | Right stick | Mouse movement |
| Evasive leap | LT | Space |
| Toggle crouch / stand while grounded | Left-stick click | Left Shift |
| Fire (held) | RT | Left mouse |
| Reload | X | R |
| Melee presentation | A | Right mouse |
| Switch weapon (grounded) | B | Mouse wheel down / up (forward / backward) |
| Toggle ordinary / targeted camera mode | Right-stick click | E |
| Open/page magic ring | Tap LB/RB | Middle mouse opens; wheel changes page |
| Ring right/bottom/left choice | B/A/X | C/X/Z |
| Ring cancel | Y or right-stick click | Middle mouse or E |
| Open pause / resume | Menu | Escape |
| Menu navigation | D-pad or left stick | Arrow keys / mouse wheel |
| Menu confirm | A | Return, Space or left mouse |
| Menu back | B | Escape, left Shift or right mouse |
| Toggle fullscreen | — | Control–Command–F |

LT/Space is the directional evasive leap, not a generic jump button. Release it
between attempts: it is a held action and can request another evade after
returning to a grounded state. RT is fire intent; LT is not aim. The desktop
bindings match the original PC defaults. Xbox View remains unbound.
Release the shoulder after opening the magic ring, then tap again to page;
holding one shoulder while pressing the other closes it. Face buttons select
directly, after the opening/page transition settles; the sticks and D-pad do
not navigate the ring. The user has confirmed these controls work. RS/E cycles the two original unenchanted targeting modes. First click selects
the targeting camera; second click leaves targeting immediately and returns to
the ordinary camera through the source quarter-second transition. Weapon posture
can remain raised afterward; that separate post-fire/upper-animation lifetime is
not the targeting mode. Enchanted extra modes and target/hit resolution are not
implemented. While
raised, horizontal look also drives the authored idle-turn animations. The host
applies the saved turn speed, mouse bias and camera inversion from Options → Controller Configuration.

The normal pause root contains **Help**, **Options**, **Return**, **Stage Start**,
**Retire**, and **Exit**. Exit resumes. Return/Stage Start/Retire ask for
confirmation, initially No. Options owns settings, controller configuration,
brightness calibration and the approved Costume addition. Back leaves a child;
settings edits can ask whether to discard. Start in Options follows its save/exit
policy. Do not expect one button to dismiss every nested confirmation.

Developer clip catalogs, reaction previews and synthetic spell-phase controls
remain retained implementation capabilities, with no current user-facing entry.
They are not rows in the original pause menu. The new inspector design must
choose their eventual controls separately.

Normal B or the mouse wheel cycles through them with the original upper-body
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
2. **Camera and directional evade.** Orbit with the right stick or mouse. From a
   fixed view, move forward, backward, left and right; briefly press LT/Space
   during each direction and release it. Repeat after rotating the camera.
   Check takeoff, flight, landing, return to movement and the attached weapon.
   Running toward the camera into evade remains a specific visual-parity issue:
   record an unexpected facing reversal rather than treating it as accepted.
3. **Crouch.** Stop on the ground, click the left stick or press left Shift, then move.
   Press again to stand. Expect the crouched pose and movement; crouching is not
   admitted during an airborne/exclusive action. An evade from crouch returns to
   the standing evade path.
4. **Pause and resume.** Pause while moving or in an evade. Try movement/look
   while paused: the player and camera should stay frozen. Resume, release
   LT/Space, then make a fresh input. The resume input should not also launch an
   evade. Active game cues pause with the scene.
5. **Costumes.** Pause → Options → Costume, choose another of the six entries, confirm,
   then resume. Repeat walking, crouching, turning and evading. Position, camera
   and the paused action should survive the swap. Check attachments, outfit
   motion and obvious mesh failures. Some dress/body surface intersections
   remain known; the source secondary-bone solver is not a guarantee of cloth
   surface clearance. Stage Start and checkpoint-zero Return perform full scene reentry; Retire
   returns to title. Post-result power-up is a separate, unaccepted slice.
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
   scheduling success alone did not establish audible balance. Check native menu navigation/confirmation sounds while the world remains
   paused, and live BGM/SFX gain changes through Options.

8. **Weapon presentation.** Hold RT/left mouse, release, then press X/R and A/right mouse
   separately. Observe raising, the admitted discharge/reload animation and the
   close strike. Move toward the current facing direction and try the directed
   strike. B/mouse wheel cycles weapons while grounded; each change should retain position,
   camera, costume and aiming state, and play the switch animation. Check it
   standing, crouched, moving, and with the weapon raised. While stationary,
   hold RT and turn right then left, then look up and down: the gun should follow
   pitch, Alicia should retain floor support, and no unsolicited jump should
   replace the aiming pose. Do not expect a developer status overlay in the normal build. The live HUD reports real
   ammunition consumption; no hit result follows from these animations. The
   current walkthrough found shotgun weapon displacement and stationary
   support-hand jerking that was absent during movement. The duplicated Shotgun
   root transform is repaired; the all-four-weapon firing and modifier audit is
   recorded in [the firing report](../research/engine/subsystems/weapon-discharge-attachment.md).
   The user confirmed the subsequent authored-target support-arm repair works.
   That authorized improvement is documented separately from original parity;
   native pause requires its separate walkthrough and acceptance.

   For the reported interruption, click RS/E to target, release, then click again
   to leave targeting. The camera should return to ordinary view independently
   of the retained gun posture. When the gun begins lowering, press A/right mouse: the
   lowering upper-body action should retire as melee starts, without continuing
   over the strike. RS/E during that melee is not admitted by the original
   Shooter state. Repeat with all four weapons. Lighting has its separately accepted baseline and documented residual
   visual limitations; this pause review does not reopen them.
9. **Pause settings and reentry.** Open Help, read a topic and return. In Options,
   change volume and verify it live; Back and confirm discard should restore the
   entry values. Change a binding or camera sensitivity, save via Exit and its
   acknowledgment, resume and verify the actual consumer. Test Stage Start and
   Return separately, then pause again after each fresh entry. Retire should
   return through the original root to title. Nonzero saved-world restoration is
   not admitted until its real world checkpoint owner exists.

Catalog/reaction/expression and synthetic spell-phase review will return through
the separately designed developer interface; no current activation sequence is
advertised for those retained capabilities.

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
