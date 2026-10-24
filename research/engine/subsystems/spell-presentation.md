# Spell animation presentation

This is the source-backed animation phase boundary, not an implementation of
spell admission, targets, damage, projectiles, cinematic cameras or effects.
`SpellPresentationPolicy` consumes accepted external observations and emits
native-slot motion plans. A caller must first acquire the declared scope,
preflight the motion resources, and then publish the policy. Missing required
observations preserve the state; a finished clip is not evidence that a world
operation succeeded. No hardware spell button is enabled by this policy.

## Installed resource disposition

The original PW0 corpus contains these twelve magic resources. Counts and raw
bindings are reproducible through the ignored `actor/animation-family-inventory`
and `actor/animation-coverage-bindings` evidence. All twelve are 60 fps; only
`magic_thunder_m_l` has the resolved repeat setting. Native start arguments are
**frames**, independently of FPS. The common entry begins at frame 1, not one
second; separate entries below begin at frame 0.

| Installed resource | Native slot | Proven consumer and lifecycle |
| --- | --- | --- |
| `magic_ancient_wall` | 788 | StateAncientWall, common frame-1 entry; current-motion completion exits. Saved camera/target/effect category 1 are external scope. |
| `magic_crow` | 783 | StateRavenPanic, common frame-1 entry; completion exits. Retained raven/cinematic owner and category 5 must exist first. |
| `magic_grenade` | 771 | StateRoseBomb, separate frame-0 entry; common update/completion, separate live aim and frame markers. |
| `magic_will_power` | 789 | StateWillPower, common frame-1 entry; completion exits, retained target updated after common actor update, category 3. |
| `magic_sacrifice` | 787 | StateSacrifice, common frame-1 entry, category 4. Target invalidation exits immediately; otherwise its explicit update checks completion after root/contact. |
| `magic_tornado` | 779 (also aliases 780/781) | StateTornade, common frame-1 entry, category 14, weapon attachment. Camera-owner return enters StateTornadeEnd; that state waits for the existing current motion to complete. |
| `magic_meteorite` | 782 | StateMeteor, common frame-1 entry/category 0, then cinematic categories 15/16. Two external cinematic boundaries, first selecting standing slot 1 at frame 0; terminal world/camera ownership controls release. |
| `magic_thunder_s` | 784 | StateThunder aiming entry at frame 0. Completion selects the middle clip; the owned-camera timer has strict negative ten-second expiry. |
| `magic_thunder_m_l` | 785 | Thunder aiming loop at frame 0; accepted target enters strike, release/timeout enters the terminal owner without inventing another clip. |
| `magic_thunder_e` | 786 | Accepted Thunder strike at frame 0. Five cinematic-owner boundaries precede the terminal current-motion completion check. |
| `magic_teleportation_s` | 768 (also alias 773) | Proven generic teleport start selects 768; its movement/ending chain selects missing PW0 slot 769 (`magic_teleportation_e`). The chain is unavailable for Alicia with installed resources; no substitution. |
| `magic_teleportation` | 774 | Installed and table-bound, but a direct state consumer remains unresolved. Full-text immediate-operand search did not establish one; this is not proof the resource is unused. It is not substituted for missing slot 769. |

The table describes resource consumers, not twelve independently completed
spell implementations. Slots 772 and 775–778 name other global spell resources
absent from PW0 and are not synthesized.

## Source ownership and transitions

Common `613570` optionally attaches the weapon, faces the camera, sets the model
action flag, selects the motion at frame 1 with a 0.3-second blend, and acquires
the configured effect. `613880` checks completion only when its state flag
permits it, then performs the special actor update and root/contact dispatch.
`6137c0` releases its flags, effect, attachment and input state. There is no
blanket CAN0 exit rule for these spells.

Ancient Wall (`69cb80/69cf60`) saves/restores its camera anchor around that
common scope. Raven (`69e1b0/69e420/69e4b0`) distinguishes ordinary completion
from damage interruption when consuming its world request. Will Power
(`69d690/69d760/69d7c0`) owns a retained target outside the common scope.

Rose Bomb (`69cfd0/69d200/69d250`) bypasses common entry: frame 12 attaches the
grenade, frame 36 can launch it, and the later frame-50 crossing branch takes
precedence in the original update. Its aim direction and projectile admission
remain external. The policy does not fabricate those effects from pose changes.
Sacrifice (`69d810/69de50/69df00`) validates the retained target generation;
frame-75/frame-145 effect/heal work is likewise an external gameplay obligation.

Tornado (`6a0780/6a0af0/6a0b20`) disables common completion. When its camera
returns, `6a05a0` constructs StateTornadeEnd. Registry `613fa0` binds that state's
entry `6a0b80`, exit `6a0be0`, and update **`6a0530`**. The phase change therefore
restores common camera/effect state and changes weapon attachment; the end
owner waits for current-motion completion before restoring the primary weapon
and consuming its world request. It must not finish immediately at camera
handoff. `finishing` reports that externally owned handoff explicitly.

Meteor (`6a0c40/6a0e90/6a0fe0`) disables clip-completion exit. Its first external
cinematic boundary selects standing, its second transfers ownership to the
retained meteor, and its terminal predicate checks player/camera ownership.
The observation named `meteor_world_finished` represents that predicate, not
an estimate from the 480-frame clip. Phase-dependent camera/time/input and world
cleanup still belong to the future spell owner.

Thunder (`69ea30/69ef80/69f050/69f150`, strike
`69f5c0/69faf0/69fd10`, terminal `6a03f0/6a0460/6a0530`) has separate ownership.
Its aim timer advances only while the source camera is owned; strict negative
expiry precedes the clip-completion branch. An accepted target is validated
before selecting strike. Strike advances through five external cinematic
segments; terminal entry preserves the current motion. Ownership transfer keeps
retained targets alive across these states, while interruption releases only
what the interrupted state still owns. The policy reports phase changes and
final release; it does not claim to have carried out these transfers itself.

## Numerical and implementation boundary

The frame-unit proof follows `5e47a0` into `41f2f0/41f200`: the incoming start
value is clamped against raw start/end frame fields and stored directly as the
controller frame clock. Seconds are converted only with the selected prepared
clip's FPS at the actor boundary. The separate game timestep drives Thunder's
aim timeout. See [scripted jump](scripted-jump-policy.md) for the same recovered
units in start/landing entries.

The policy uses fixed value state and borrowed observations, with no per-frame
allocation or independent animation clock. Applicable full Core E.4 and
performance MEM.9 rules were consulted for design and implementation. This is
original game-state reconstruction; no generic engine or modern spell system
was substituted. Existing engine timelines and pose layers remain shared.

Bounded exports are `pass133-magic-phase-boundaries` and
`pass134-magic-admission-states`, alongside `pass124-reaction-selection` and the
older callbacks referenced above. Tests witness source frame units, missing
observation preservation, strict timeout order, five strike segments, Meteor
world gating, and Tornado's distinct ending phase. Native spell/world parity
is not established by these in-process tests.

`SpellPresentationUpdate` also exposes original root/contact dispatch. Common
completion, invalid Sacrifice target, Tornado's initial handoff, Thunder timeout
and terminal completion return before that dispatch. Valid Sacrifice completion
and Meteor world transitions occur after it. Meteor's first transition marks
its standing request for publication after the prior interval's root/contact;
the adapter must not consume newly selected standing root for the old spell.
