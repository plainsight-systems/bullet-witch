# First encounter AI reconstruction

Work in progress. Original Lua and native controllers jointly own soldier
behavior. No distance-only chase/fire substitute is admitted. The prepared
`no_ea_m_000` tree is the starting resource, not the complete behavior closure:
its low-knowledge branch changes to another authored AI resource. Reachable
`changeai` targets must be prepared during mission loading.

## Ownership boundary

The world publishes its one admitted frame and immutable actor observations.
NPC perception owns target knowledge and local retention state; it borrows
query-only sight access. Behavior execution owns tree selection, labels and
subroutine/action lifetime. Concrete native actions emit typed requests to the
actor's movement, weapon and animation owners. Neither perception nor script
callbacks can advance the world clock or mutate another actor directly.

## Native facts recovered so far

- Ordinary EA0 creation uses `4563e0 -> 5ff170`, with Shooter table `855ad4`.
  The alternate table `8523d8` belongs to the different EB0 setup and is not a
  universal enemy Shooter implementation.
- `5a7570` refreshes the script environment every admitted actor update. Its
  separate action dispatch alternates through a two-update countdown unless
  an action flag requests every update. Tree reconsideration additionally
  depends on action completion/state changes and a load-balanced countdown.
  These are distinct original gates, not one arbitrary global AI rate.
- `5dad90` advances behavior-local elapsed time from the admitted message delta
  and updates two knowledge observations. `5aad90` owns retained knowledge,
  target identity, lost-target delay and query scheduling. Internal states
  0/1/2 map to Lua knowledge levels 0/1/3 through table `8485e8`.
- Candidate ranking `5aa490` combines reciprocal distance and facing, weighted
  by source affiliation. Current targets bypass the ordinary view-cone gate.
  `5aa1d0` retains the best three candidates, preserving existing order on ties.
  `5aa3b0` takes the first visible candidate, otherwise the highest-ranked
  candidate with its visibility flag cleared. This distinction matters to
  knowledge retention.
- `isEnemyCanLook` (`5d8990 -> 5d84c0`) and
  `isEnemyCanShootWithReadyGun` (`5d8c80 -> 5d85f0`) perform sight tests from
  distinct actor/ready-gun origins. The latter is not an ammunition check.
  Both use target position from retained knowledge and `5abf60`, with source
  query flags combined with `0x3200`. Knowledge candidate visibility adds
  `0xc200`. Query filtering must preserve those source flags.
- `getEnemySquareDistance` measures three-dimensional squared distance to the
  retained target's current position; absent target position resolves to zero
  in the original getter. `getNowBehaviorSec` reads the local elapsed counter,
  not an independent wall clock.
- Shooter request receiver `62ea90` checks the current weapon, requests raised
  posture, selects supplied or ready-gun-derived targeting and invokes its
  fire virtual. The native producer of recurring requests is still being
  traced through action dispatch; lack of a `oneshoot` node is not evidence
  that the soldier should remain idle.

## Evidence

Fresh ignored exports `pass-338-ea0-perception` and
`pass-339-ea0-detection-calculations` supplement the original AI maps and
`pass-06u` action methods. Shared tree selection and Lua score adapters already
exist; this report does not claim complete runtime integration or native
encounter acceptance. Concrete action dispatch tracing is separate from the
perception/evaluator implementation so the actor remains the sole movement
and animation owner.

## Prepared settings and scoring ownership

`NpcAiSettings` decodes the original customization tree at mission preparation.
The search record supplies change delay, lost-target duration and nearby range.
The five shoot records supply cone degrees, transition seconds and prediction
flags. Source lookup falls back to record zero for an invalid mode. NPC targets
use record four for cone/transition, while prediction still comes from the
actor's selected mode. Degrees are converted once at this boundary.

The character environment constructor's initial values are group -1, loop 0,
move range 100, attack ranges 10/50/100, view 90 degrees, far radius 60 and close
radius 10. These differ from the Lua accessor's fallback values when a range is
zero. Authored mission/script writes must overlay the environment before use;
the accessor fallback must not replace the environment constructor.

`npc_movement_settings` copies the 13 fixed posture records read by `5d5a00`.
Their second field is the follow-controller movement radius, not collision
radius or speed. Invalid unsigned posture indexes select record zero. The
native routine calls two actor methods but discards their floating results;
those calls do not modify this returned table value.

Original `5025e0` creates a temporary evaluation context over shared scratch,
registers the current environment, loads the selected wrapper and destroys the
context after selection. It does not allocate six persistent VMs per actor.
Our mission-owned `NpcBehaviorLibrary` prepares one evaluator per admitted
wrapper and routes a borrowed actor identity/observation interface only during
a synchronous call. The route clears on success and exception; construction
thread and reentry checks prevent concurrent/reentrant routing. `NpcBrain`
owns perception, native action state, runtime scheduling and a small scorer.
It borrows the mission library. Construction does not select actions; `start`
follows binding of the real world adapter. Prepared definitions and names
outlive the library and every actor.

This retained-function choice has a concrete compatibility limit. All 1,893
score functions in the six admitted original wrappers were inspected at the
bytecode-operation level: none writes globals, upvalues or tables. Their
external reads are registered observation functions; top-level chunks install
the functions. The ignored `lua-wrapper-purity.json` records the census. This
supports reuse for this closure, not an assumption that arbitrary future Lua
wrappers are stateless.

`getLevelRandom` reads a cached float. Environment construction sets it to
zero; `5da810` draws from the shared source RNG before each full child descent.
Sibling score calls share that value. Preliminary positive interrupt probes
occur before that refresh. `BehaviorScoring::begin_level` represents this
explicit mutation boundary; observation getters do not draw random values.

## Live observation producer map

The prepared callbacks do not synthesize missing facts. The actor/world adapter
must supply the following source owners. The brain supplies the four local
facts and stack history; it forwards remaining queries to that adapter.

| Lua observation family | Source owner and semantics |
| --- | --- |
| Knowledge | Perception's retained state; internal 0/1/2 maps to Lua 0/1/3. |
| Sequence end | Current native primary action outcome byte, including 2 and 255; companions do not replace it. |
| Behavior seconds | Admitted action/runtime elapsed time; no separate clock. |
| Level random | Cached source RNG result refreshed before full descent. |
| Previous labels | Current/previous action stack: raw node-byte-offset equality first, then label-subtree membership. |
| Actor life/max | Actor health owner queried by `5d26c0`/`5d2760`. |
| Script data | Copied character environment with authored writes; source zero-range fallbacks remain in the getter. |
| Leader distance/angle | `5daa00` scans original actor order for matching group, leader flag and not dead. Absent leader returns -1 squared distance and 360 degrees. |
| Enemy distance | Current target position; absent target resolves to the zero **position**, so distance is actor-to-origin rather than scalar zero. |
| Player distance/angle | Original optional player-position override, otherwise session player pose, otherwise zero position. |
| Enemy/player angle | Actor position and original facing basis, through `68b060`; callers convert Y Euler radians to degrees. |
| Enemy movement angle | Movement-controller destination relative to actor position; degenerate direction uses actor facing. |
| Enemy/player visibility | Distinct actor/weapon query origins, source filtering and actual collision queries. |
| Can shoot with ready gun | Source ready-gun sight test; not an ammo predicate. |
| Target model/player predicate | Target actor category flags and identity comparison with session player. Missing model is -1. |
| Last message | Environment message ID and position retained by message producer, not current target position. Constructor ID is zero. |
| Last sound | Retained unsigned sound identity; source clears to 65535 when knowledge is not searching. Initial producer remains separately traced. |
| Callback names | Case-sensitive original 16-name mapping to actor callback state. |
| Near path point distance/angle | Authored path owner and original point selection; `5d5a40` returns zero position only when its actor is absent. For an admitted actor, it temporarily selects the authored patrol, finds its nearest point, and delegates to `5d6590`; missing path/group returns the actor's retained initial position. |

Passes 345 and 346 retain the getter/constructor/lookup exports behind this
mapping. Message-position initialization, first-frame sound state, callback
producer details and the post-scan target auxiliary update remain
explicit live-integration boundaries until their owner traces are closed.
They must not become arbitrary zeros in an observation snapshot. The mapping
is evidence for adapter construction, not a claim of a completed playable
encounter.

## NPC locomotion ownership

The source NPC Runner update `666c20` consumes retained destination, requested
posture, requested playback rate, signed turn and a separate positive turn-speed
limit. Destination distance is horizontal. The original epsilon is float epsilon;
within that threshold playback rate is zero. A positive turn-speed limit bounds
heading change by world delta times that limit and publishes a facing message.
`667000` selects only the low motion nibble and applies the rate. `6678a0`
combines that nibble with the explicit posture and requests preserved phase with
a 0.3-second blend. The hardware walk/dash threshold belongs to `6667e0`, not to
this NPC path. Root displacement remains authored animation motion and is applied
by the actor/world movement owner, not synthesized from a guessed meters/second.

The existing pinned Unreal `CharacterMovementComponent::CalcVelocity` comparison
in `player-input-locomotion-policy.md` supports this boundary: authored root motion
bypasses ordinary velocity integration. The source NPC policy therefore publishes
motion/direction; it owns no collision or world clock. For an exactly coincident
target the original computes an unused undefined direction after setting zero
rate. The reconstruction retains the previous finite heading while preserving
that idle decision; it does not invent movement from the degenerate vector.

The isolated original-x86 selector witness covers 60 raised/unraised cases,
cardinal/diagonal movement, idle, positive, negative and excessive requested rates.
Production motion nibble and applied playback rate match all 60 cases. This
witness executes `667000` with only its math-library and animation-rate receiver
calls substituted; it does not prove full animation, collision or rendered
locomotion parity. Scoped contract tests and fatal clang-tidy pass.

### Copied mission admission metadata

`47ef40` overlays the constructor environment from the original `chara` row.
Columns 7–10 become movement/near/middle/far ranges, with an exact-zero fallback
to 10 for **each** field. This differs from both constructor defaults and Lua
getter zero fallbacks. Columns 17–19 supply view angle/far/close radii, with
nonpositive fallbacks 90/60/10. Column 13 is the movement-rate multiplier;
`5da830` preserves the requested rate when that multiplier is zero. It is not
target eligibility. The perception scan reads the actor's virtual life getter
before admitting an NPC candidate.

The source `path` table's exact name match supplies the loop flag. Placement group
ID supplies script group. A behavior name beginning with case-insensitive `l`
sets the leader field. The base affiliation follows `44af80` (player, resistance,
civilian, enemy), then authored override bits 4/8/16 in priority order. This is
placement admission; later source actor-category transitions remain the actor
owner's responsibility. `5d8260` maps copied affiliation values to script model
categories, including the transformed flag and unknown -1 sentinel. Voice group
is column 24 interpreted as float then truncated to integer before shared audio
profile selection. Preparation publishes copied values and never parses table
cells inside a frame.


## Live actor composition checkpoint

The NPC controller now owns the per-actor brain, navigation progress, weapon
magazine/cadence, retained muzzle presentation and audio channels. WorldCombat
owns controller lifetimes and supplies copied population observations. Before
pose update it consumes one authoritative frame, runs authored perception and
behavior, submits source motion requests, and emits accepted weapon receipts.
After pose/physics publication it consumes motion audio and updates attached
muzzle presentation. Body, upper and additive banks retain separate event/audio
reset ownership. Static actors outside the selected first soldier archetype do
not acquire this controller.

The original actor pose virtual at vtable `852334+20` resolves to `600ae0`.
The perception argument `0x36` is a weighted control-point attachment slot,
not a raw skeleton joint. `428640` constructs its frame from three authored
points and inverse-bind skin influences, orthonormalizes locally, then composes
the model frame. The source half turn follows success; absence falls back to
the actor frame. Sight consumes both its completed translation and forward.
The previously documented raw-joint interpretation was incorrect.
The
shared target offset at `1b5b5e0..ec` is zero-filled BSS: pass355's direct-reference
census and the raw image operand census find only reads, with no writes or
address-taking. The adjacent scheduling constructor clears the following bank
starting at `1b5b5f0`, not this vector. The copied zero offset is named for this
source evidence, rather than being an unexplained observation default.

NPC fire messages may carry an explicit target (`62ea90` flag2). That target
survives through `630400` into the shared projectile path; its scenery reference
remains the source weapon attachment origin. Only the no-target branch adds
the returned attachment's positive Z axis to that origin. The machine
instructions at `62eb46`–`62eb92` establish ADD after the attachment half-turn;
the earlier negative-Z claim conflated a separate animation-event producer. Both routes retain the shared
weapon cadence, random stream and presentation owners.

This remains an integration checkpoint, not accepted combat parity. Unclosed actor/group selector variants and contact-action state receivers
remain explicit failures. Target requests, selected player-directed broadcasts,
source voice arbitration, facial/visibility presentation and joint-hit event
preparation now have concrete receivers. Actual authored encounter execution
remains the integration witness; unresolved exercised behavior blocks acceptance. The named locomotion policy has 60
original-machine comparisons without a slot/rate mismatch; that numerical
witness does not establish integrated motion, aim or contact parity.


### World-owned message dispatch

The `5cdcc0` recipient filter and `5cdeb0` distance function preserve original
active-list order and strict distance/radius admission. Ally selection compares
bit 2 as a boolean, rather than intersecting arbitrary affiliation bits. The
source supports all/group/group-leader/opposing/exact-authored-ID selectors,
negative-radius unconditional near strength, linear near/far strength and a
30-recipient limit. There is no additional dead-actor filter. World composition
submits copied target messages to admitted AI owners; it never passes another
mutable actor into a controller. Receiver arbitration retains equal requests
individually and consumes the strongest eligible request before the next brain
step. Unimplemented actor families are not claimed as active AI recipients.

The selected behavior broadcast sends event 8 directly to its current enemy.
Its typed message is registered on `AiBehavior` (`5a5080`, RTTI), with handler
`5a6e10` reached through `588930`. It is not a player action request. The player's
lack of that AI receiving component naturally supplies no recipient; no invented
player evade, melee or animation follows the NPC broadcast. A live NPC receiver
would require event-mask/priority arbitration (`504790`/`5a8690`) and remains an
explicit unsupported path outside the selected player-target exchange. This is
not generalized to other message types.


### Ground sensors and retreat projection

The ground sensor follows `5ab810`: a miss returns the original height sentinel
−1,000,000. It does not return its unchanged input as a successful destination.
Retreat follows `5ce940` through `5abb40`/`5abb80`: raise the segment by .3, clip
it through the original static collision bank, retain the source intermediate
height, probe down 20 units, then restore the prepared body radius. The radius
comes from the actor settings copied by `5da310`; it is not a visual bound.
`navigation_projection` contains these immutable query operations separately
from actor movement. Focused floor, wall and miss cases pass; those cases are
contract verification rather than proof of the full native navigation path.

### Joint-owned melee volumes

Motion channel 7 (`KAK0`) is the original joint-hit receiver. Preparation decodes
its packed effect, near-damage and far-damage values, radius and joint once.
The six selected EA0 records carry effect 7, radius 1 and equal near/far damage
300. The second damage value is a radial interpolation endpoint, not force.

Entry (`5f88a0`) retains one of four joint-keyed slots and obtains an attack
serial from the world. Update (`5f9640`) publishes the completed joint center
and prior point; exit clears all matching joint slots. The first prior point
uses actor X/Z and joint Y. A missing joint uses the original model-origin
fallback. No local clock, physics owner or per-frame allocation is introduced.
The world owns sphere queries, the 32-face result budget, receiver serial
deduplication and damage dispatch. The previous point is also required by the
separate scenery-segment consumer `4e8770`; it is not an actor sweep.

`555fe0`/`550810` establish the source sphere contact score as signed separation
(distance to the closest triangle point minus radius), with each overlapping
face retained before receiver deduplication. The provider uses the pinned Jolt
triangle narrowphase on world-transformed triangles for affine animated
receivers. This is the explicit provider adaptation; it does not claim that
the old scaled-space approximation or overflow replacement is reproduced.
The world-facing result retains copied identity, contact and separation.

The focused melee tests verify four-slot lifetime, repeated-joint retirement,
serial ownership and channel-7 preparation/update dispatch. Scoped fatal
clang-tidy passes. These tests do not establish complete melee damage, scenery
response or animation acceptance; those require the integrated encounter.

### Ordinary soldier motion publication

The common table alone is insufficient for EA0. Constructor `601250` overlays
83 entries from `852478`, including twelve newly populated reload slots and
family-specific death clips. Shared preparation now composes that table once
with the common table into 739 immutable aliases; it retains no mutable
per-instance lookup table. The chosen WG1 does not select the additional
WG4/5 or WG6/7 overlays in that constructor. NPC pose semantics consume the same
composed aliases, so a family-specific name does not lose its source slot
semantics. In particular, slot 730 selects `die_va_ba`, not the unavailable
common `die_ba`.

The purchased-content encounter now verifies enemy shots damaging Alicia and
Alicia's ordinary shots killing an authored EA0, followed by the original
animation-to-physical-body handoff. This is a headless production-world check
with controlled device outputs. It does not establish native audiovisual
synchronization, player defeat/retry, or complete selected AI action coverage.
The observation uses copied admitted-actor state: checkpoint serialization
correctly excludes dead actors and therefore cannot validate corpse state.


### AI contact-state receivers

The common actor installs `AiGrappleCapacity` (`5fd2d0` → `66c7e0`), distinct
from the player hardware `StateGrapple` receiver and the other specialized
capacity at `691480`. Its attack selectors 6, 7, 10 and 11 select slots 722,
723, 830 and 808 respectively (`66c970`/`66cca0`), fresh with blend .3. Other
selectors leave that capacity in state zero. The selected ordinary EA0 tree
requests selector 6. Completion retires exclusive body ownership; ordinary
locomotion cannot overwrite it while active. Damage/death interrupts it.

`66cde0` compares the unnormalized target displacement against the cosine of
the admitted angular step before turning. A six-case original x86 witness
confirms this can snap a distant diagonal target earlier than a conventional
normalized-direction implementation. The finite source cases are retained.
The exact antipodal cross product produces NaNs in the original witness. That
undefined case uses the signed half-turn convention in pinned Unreal
`FMath::FixedTurn` (`Core/Private/Math/UnrealMath.cpp`, commit
`f45729c0b6f097038e72b52d366a62baeca3275b`) through the shared direction math.
This is a documented robustness adaptation, not original-output parity for
the singular case.

Dodge message `6891a0` chooses grounded dodge slots 725/724/727/726 using
`68ae10`'s angle-dependent ordered three-direction probes. It falls back to
slot 724 if none qualify. Candidate distance is 4.6; `68b260` grounds each
candidate (+2/−20), applies .01 segment clearance and requires at least three
units of travel. Its returned classification is a normal classification, not
a hit count: only no contact or the original up-facing class is admitted.
`SegmentClearanceResult` now publishes its already-computed normalized contact
normal so this game sensor does not reimplement geometry.

The action selector −3 in `5ca830` reads the AI environment's retained message
position (+20..28), not a group leader. Native action dispatch now uses the
explicit optional message-position observation and rejects selection before
that source value exists. Real incoming AI message delivery remains required
before this branch can be claimed as integrated. The bounded producer trace
(`5a72c0` → `5da1c0`, pass 371) identifies message mode 1: the payload installs
position, message ID, optional authored actor identity, current behavior and
return behavior together. It is not the ordinary projectile-hit receipt.
No synthetic hit-position substitution is installed. Dodge entry also samples the
shared source random stream against body+4c and sets/clears EHIT query mask
0xA through the actor's scoped query publication. Completion releases that
mask, except when a newly admitted melee-blast state has acquired the same
source bits; that state retains its own release authority.

Grounded contact support loss now requests the existing shared Runner
`ScriptedJumpPolicy`: `68a4d0` → `6030d0` → `5fdb30`/`5fec70` admits a fresh
family-0 flight with zero velocity. The policy has moved from Alicia's action
directory to `actors/animation`; its verified equations and transition rules
are unchanged. NPC ownership keeps its own velocity and phase, replaces pose
root displacement with trapezoidal ballistic displacement during flight, and
uses the same collision resolver in airborne mode. Ground contact feeds the
original landing thresholds; landing retains root/contact and completion/CAN0
ordering. Incoming damage/blast interrupts generic flight. This shares source
state policy without borrowing player input, camera or controller ownership.
Actual NPC support-loss rendering remains a separate integration check; the
previous ordinary shoot/death witness does not exercise this transition.
Genuine mode-1 AI payload delivery remains an explicit integration boundary.

Focused synthetic checks cover Dodge direction priorities, blocked fallback,
supported versus missing/blocked sensor destinations, exact grapple slot
selection, original finite facing samples and the documented antipodal
adaptation. Native-action checks prove that selector −3 reads a distinct
message position and refuses a missing one; they do not prove that production
message delivery exists.

Wait posture producer correction: original entries `5bb040` and `5bb320`
(pass 341 action-dispatch exports) resolve the authored integer through
`5d5a00` before both the movement-speed and stopped-posture messages. The
shared table maps authored 0 → 0x10, 2 → 0x90, and 3 → 0x20; this is not a
bit shift. Original `sub_ea_lv01_no.amb` wait nodes at 600/648 contain posture
3, and `sub_ea_escape_m.amb` waitrandom node at 420 contains posture 2. Passing
these raw values to Runner violates its motion-family contract; raw 0 instead
silently chooses the wrong family. Wait preparation now uses the same source
lookup as movement actions and retains its encoded family in the shared
immutable action library. Entry keeps speed 1, stop true, and the original
timeout and random-stream behavior. Locomotion validation is unchanged.

Synthetic public NativeActions checks execute both wait types for all three
authored postures, feed their emitted command to locomotion, and check the
source stopped slot, speed, strict timeout boundary, outcome 255, and random
draw consumption. They establish the producer/consumer regression, not the
ordinary original-start route or human rendering acceptance. The source
alternate-context branch (`+0x44 != 0`) omits the speed message unless raw
posture is 2 but always sends the mapped stopped posture; the existing combined
receiver still models the ordinary branch. This correction does not claim
that alternate context is implemented.

Ordinary-start traversal also reaches a valid action-zero terminal: the captured
`no_ea_m_000_3288` node (prepared index 274, parent 273) has no children,
eligibility/evaluation 1, priority 0, and a display label rather than a native
action name. The prior action at 268 completed with outcome 255. Source
`502e30` returns the input branch with its terminal byte set when no child
exists; `5025e0` stops descent on that byte and proceeds through ordinary
priority/lifecycle dispatch. Registry `846fb8` registers action 0 as `noname`
(factory `5b98e0`, vtable `849490`), whose outcome starts at zero, entry/update
are no-ops, and reconsideration is allowed. Thus terminal does not imply
completed, malformed, or absent native ownership.

BehaviorRuntime now preserves the selector's terminal result instead of
redescending until rejection. NativeActionLibrary prepares code 0 through its
registered no-op semantics independently of the display label; negative label
controls remain excluded. This applies to selectors too, because source
selection may return the first excluded child as terminal even if it has
descendants. Synthetic runtime coverage checks completed-action replacement,
retained terminal ownership, pause/retirement, and terminal selection with
descendants. Native-action coverage checks its inert reconsiderable lifecycle.
Bounds and control-cycle checks remain in place. Captured route evidence is
under `research/evidence/reconstruction/first-combat-normal-route/`; final
ordinary-route acceptance is a separate integration result.

### Collision-driven route ownership recovered during the full encounter audit

The original no-archive path is not unconditional direct steering. The routed
receiver (`5d1b90`) consumes `5cfcb0` and the outer `5d0aa0` owner. It debounces
movement-sensor failure, preserves the constructor's alternating admission bit,
and enters a pair of collision-search jobs when the direct capsule is blocked.
The separate direct receiver (`5d1dc0`) retains its body-radius offset and
sensor-failure completion policy. Follow, angular movement, side movement and
the routed variants of flock/patrol use the former; side movement is not the
unrelated forced-direct action.

`CollisionRouteSearch` implements the bounded worker (`5ae960`, `5b0330`,
`5db7e0`/`5db860`/`5db9b0`). `CollisionRoutePair` implements `5afed0`: both jobs
have the same selected side, independent retained progress and explicit
requeue. Scheduler status 2 means one update has run. Search phase 3 means
complete. Pair phase 4 remains movement-ready while its jobs are requeued;
these three facts must not be conflated. The pair retains the delayed admission,
periodic refresh, secondary best-point handoff and near-boundary publication
rules. The original does not initialize the handoff flag before its first
state-dependent read; reconstruction initializes it false, then preserves the
recovered writes and reset behavior rather than reproducing undefined storage.

The scene route owner composes the pair and immutable query capabilities. The
world service phase advances pending jobs once before actor action dispatch;
controllers submit requests and consume copied results. No worker owns a world
clock, actor transform or physics provider. The geometry adapter reuses the
shared segment/capsule and four-point ground-support queries. The worker's
support callback uses its own step radius and configured lateral sample radius;
it does not call the movement sensor's wrapper and accidentally add its
look-ahead twice. Ground adjustment clamps the original downward result to the
allowed step interval and separately reports whether clamping was required.

Focused tests distinguish world processing from actor consumption, pending from
completed search, pair-ready from worker-ready, cancellation, fixed side
retention and source boundary-walk output delay. These are contract checks;
original-content movement and continuous native pause/restart evidence remain
separate verification requirements.

The shared capsule face observation uses the recovered signed support-separation
score and returns authored triangle winding, separately from the physical
solver's closest-distance penetration depth. One geometry-provider adaptation
remains explicit: the original first selects the closest face inside each mesh
object, then ranks those object contacts by support separation. The prepared
static provider flattens triangles into its BVH and ranks admitted faces there,
with source ordinal resolving ties. This can select a different side at a
multi-face intersection; it is not claimed bit-identical contact ordering.
The generic capsule helper raises each endpoint by radius plus caller height;
the body wrapper passes height minus radius, while collision jobs pass their
independent step/body radius and maximum-step height.

MoveTo publication (`5d18d0` → `5b7c20` → `666290`) compares a candidate with the
retained prior destination at the original squared change threshold. Only an
admitted changed destination clears Runner's explicit stopped state and copies
the horizontal aim anchor. No new packet preserves both prior destination and
Stop state. Navigation selector zero and navigation-owner reset do not fabricate
Stop commands; locomotion independently selects zero rate at the destination.
