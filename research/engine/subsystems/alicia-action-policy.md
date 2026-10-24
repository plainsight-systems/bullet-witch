# Alicia grounded and weapon animation policy

This is recovered Steam control policy, separate from the engine's prepared
clips, playback rings and pose layers. The current implementation boundary is
`src/game/alicia_action_policy.hpp`: original-slot requests with independent
body, upper-body, additive and weapon destinations. It does not authorize a
shot, consume ammunition, resolve damage or pretend that unsupported game
actions have succeeded.

## Evidence and selection

The bounded exports `pass-84-runner-movement-policy`,
`pass-90-runner-hardware-jump`, `pass-108-layer-weapon-receivers`,
`pass-117-alicia-shooter-policy`, `pass-118-alicia-visibility-stance` and
`pass-121-alicia-stance-weapon` retain pseudocode plus machine instructions.
Pseudocode local names and inferred signatures are not original source. The
native alias table remains authoritative for which requested slots have clips.

| Consumer | Observed contract |
| --- | --- |
| Ground direction `0x667000` | Unraised movement selects low nibble 0, idle 1. Raised movement classifies against horizontal camera forward rotated +15 degrees: forward 9, backward 8, left 10, right 11. The side interval includes both ±cos(45°) boundaries. Raised idle uses 12, or signed-turn 13/14 with absolute turn rate. |
| Standing hardware `0x6667e0` | Multiply processed magnitude by active weapon parameter +0xb0 while raised. Above 0.9 select dash family 0x10 with that magnitude as rate; otherwise family0 with magnitude/0.9. Idle rate is1. Source input packet already owns deadzone processing. |
| Crouched hardware `0x665bd0` | Same direction selector, family0x90; uses scaled magnitude directly, with no standing /0.9 adjustment. Input+0x20 mask2 exits crouch; standing enters via0x65bde0 on the same edge. Entry/exit0x665bb0/0x665bc0 set/clear actor stance bit through0x604590. |
| Idle with active upper motion | Standing/crouched handlers substitute the current upper family with idle low nibble and sample its current frame for the body request. This is explicit clock synchronization at selection, not permanently shared clocks. |
| Raised transition `0x6313b0` / `0x62e7d0` | Standing raise/lower families0x250/0x270; crouched0x260/0x280, retaining the current body low nibble. Raise clears upper before selecting. Upper channel1, start0, fresh=false, blend 0.3. Weapon raise selects2 and desired rest2; lower selects5 and desired rest1; weapon fresh=true. |
| Accepted reload `0x62e570` / `0x62fa50` / `0x62e6d0` | Standing bases0x150,0x170,0x190,0x1b0 by weapon class0..3; crouched adds0x10. Retain body low nibble; upper channel1 fresh=false. Weapon selects3 or4 based on body slot<0x2c0 with low bit8 set. The original weapon receiver also mutates ammo; presentation policy requires caller acceptance. Custom non-Alicia wg prefix overrides are outside this Alicia helper. |
| Accepted shot `0x630400` | Only after weapon0x616530 returns accepted result0 and upper owner is available: upper family `(class*2+crouched+0x1d)*0x10`, current body nibble; weapon slot6; additive recoil families0xd0/e0/f0 for walk/dash/other, or0x100/110/120 for class2. Air additive uses0x2c1. All fresh, blend 0.3. Missing weapon slot6 stays an explicit catalog gap. |
| Accepted switch `0x62fb90` / `0x62e8e0` | Ground standing family0x130 or crouched0x140 with body low nibble, upper channel1, fresh=true, blend 0.3. These hardware-grounded receivers consume message selector0. The separate generic0x62e670 receiver uses fresh=false and is not silently conflated. |
| Upper stance migration `0x62e400` | Existing slots0x130..0x2bf alternate standing/crouched every0x10. Move ±0x10 only when parity disagrees with stance, fresh=true, negative start sentinel, blend 0.3. Preserve current phase through the actor adapter. Other actor states have their own clear/retain policy. |
| Weapon completion `0x617a30` | If the current weapon clip is nonrepeating and completed, select the stored desired rest slot with start0, fresh=true, blend 0.3. Body completion does not imply a universal return to idle. |

The default hardware reader `0x5fc3f0` maps left-stick click to crouch mask2,
right-stick click to aim-mode cycling, right trigger to shooting (Shooter
threshold0.8), X to the reload request, B to weapon cycling, A to the attack
request, and left trigger to evade. The original option swaps trigger and stick
click roles. A raised weapon is a retained timer/state, not an invented held aim
button: `0x62ed80` refreshes it to6 seconds, or0.75 for weapon class3; the owner
decrements it while no upper action is retained. Aim modes and combat admission
must be connected by their owners.

The standing idle handler also retains explicit special-motion state and a
60-second transition to slot0x2ff. Those state lifetime rules remain distinct
from the stateless family selector. This file does not claim complete Runner,
Shooter, magic, damage or death execution. See
[coverage](alicia-animation-coverage.md) and
[action transitions](alicia-action-transitions.md).

Crouch lifecycle is verified through the typed dispatch, not inferred from
animation names. `0x667910` registers StateSquat entry message `0x957400`
through `0x661040` → thunk `0x65c420` → Runner virtual+0x90 → `0x665bb0`,
which sets stance through `0x604590(1)`. Exit message `0x957404` uses
`0x6610c0` → `0x65c440` → virtual+0x8c → `0x665bc0` → `0x604590(0)`.
State-machine update `0x568960` sends exit to the old state before replacing it
and sends entry to the new state afterward. Entering the evasive state therefore
clears crouch; its collision height must follow the cleared stance. The thunks
were checked with bounded LLVM21 disassembly under ignored action-policy
evidence, alongside the existing pass74/pass05d exports.

The negative start sentinel preserves **normalized phase**: `0x5e25c0` returns
current frame/end frame minus3; `0x5e47a0` cancels that sentinel encoding and
multiplies by the new resource's end frame. Copying current elapsed seconds
would be wrong for unequal clip durations. The separately authored positive
idle mirror is a current-frame value; Alicia's verified common60Hz clips allow
the game adapter to represent that value as seconds.

Upper completion is an owner lifecycle rule. `0x5e0830` registers upper pool
`+0x5360` after the base pool in the model's owner list. `0x5e5280` preserves
the first owner's terminal pose but calls `0x5e1a00` with keep=false for the
upper owner, clearing that whole pool before pose consumption when the selected
clip is nonrepeating and completed. The additive pool `+0x5398` is constructed
but not registered in this list. It must not inherit this upper clearing rule.
`0x5e47a0` passes false for controller auto-removal (`0x41f200` byte+0x11), so
post-pose per-entry auto-removal is not an equivalent replacement.

## Visibility listeners

Actor construction registers ID1 with no-op callbacks and ID1000 with the
selected weapon listener. Registered IDs do not fall through to body groups.
The group accessors0x42a7b0/0x42bd40 operate an eight-word enabled bitmask.
ID1000 activation0x5ff910 and reset0x5ff560 OR word0 with2, enabling weapon
group1. Release0x5ff660 clears bits1+2 (`& ~6`), disabling groups1 and2.
WA0 has actual draws in both groups. Release may also request effect IDs
0x2e/0x2f/0x30 at attachment bone25 for eligible weapon classes; those effects
are separate from visibility and remain unsupported until their owners exist.

`0x5f7f00` invokes reset on **all registered listeners** before destroying the
fallback-owned group entries. `0x5f7e40` and `0x5f7de0` scan the registered list
and invoke matching activation/release callbacks without acquiring/removing
registration. Therefore resetting only a remembered positive1000 marker would
be a reconstruction error.

## Implementation boundary

Source requests are small fixed values, with at most three submissions for an
accepted shot. The actor adapter resolves aliases and resource repeat mutation,
sets current rates even when alias admission retains a controller, and binds
events only for proven body/upper owners. Additive requests do not acquire an
unproven event timeline. Masks, release snapshots and independent clocks stay
in the reusable engine mechanisms described in
[animation layers](alicia-animation-layers.md).

Both C++ corpora were queried and full I.1, E.4 and MEM.9 read for design and
implementation. The policy validates input invariants and uses fixed value
storage; no per-frame parsing/allocation or new generic framework is required.
The pinned Unreal layered blend comparison remains in the layer research; this
game-specific policy uses the recovered original consumers rather than applying
Unreal action conventions to Alicia.

Verification uses the existing runtime unit target: analog walk/dash/crouch,
camera-biased shooting directions, idle upper-phase mirroring, independent
paired requests, stance migration, weapon completion and malformed input.
Pinned formatting, scoped fatal clang-tidy for the policy/fixture, resource and
runtime contract targets pass. Actual native controller/presentation acceptance
belongs to the actor integration; these policy tests do not establish it.

## Deterministic standing idle variation

Standing update `0x6667e0` accumulates float seconds only for the unraised idle
low nibble 1 with no upper action. At strictly more than 60 seconds it stores
slot 767 (`@stay`) and resets the timer; that update still submits ordinary idle.
The next update selects stay at frame 0, fresh=false, blend 0.3. There is no random
choice here. Standing entry `0x6667c0` zeros the timer; constructor `0x4500b0`
initializes retained special slot to−1. `GroundIdlePolicy` preserves this phase
and resets when its standing owner exits.

Stay continues while the current selection is idle (low 1/12, slot<0x2c0), no
upper action overrides it, and the selected stay has not completed. Completion
belongs to the current body slot; an outgoing clip's completion cannot end it.
Movement, raised turning, an upper action or state exit interrupts the variation.
The direction refresh condition uses the retained slot from the beginning of
the update, including the tick that interrupts it. Crouched state has no such
60-second selection. Normal stand/squat blinking already comes from authored
FAC0 intervals 10/13; stay contains eight FAC0 intervals. No independent blink
timer is reconstructed.

## Authored transition blend override

`0x5f9290` applies INT0 only when fresh is requested or the native slot changes,
the destination resource exists, and the requested start frame differs from−1.
It first reads outgoing parameter INT0 record 0 signed exit frames at +0x24;
a strictly positive value overrides requested blend. After switching to the
incoming parameter reader through `0x6ad480`, if no outgoing override was chosen,
incoming record 0 signed entry frames at +0x20 overrides when nonnegative. Chosen
frames divide by 60; absent/negative values retain the request. Outgoing zero
does not suppress an incoming override. Preserve-phase start−1 bypasses this
parameter transition path. `0x4201e0` tests the FPA0+0x18 pointer, not an event
clock predicate. Accessors `0x41fd60`/`0x420030` use a 40-byte stride, distinct from the 36-byte marker records in other categories.

The actual corpus contains only `at_stagger` INT0, entry 10/exit 0: entering uses
10/60 seconds, leaving retains the next incoming/default blend. This policy is
an adapter contract, independent from the grounded idle implementation.


## Interactive input capture and admission boundary

The physical edge proof is `0x41cfd0`: snapshot +0x10 is
`~previousHeld & newHeld`, while +0xc retains held buttons. The gameplay reader
`0x5fc3f0` consumes +0x10 for face buttons and stick clicks. Its face-index
helper `0x4bb7d0` returns the supplied index unchanged in this Steam executable.
These actions therefore do not inherit menu repeat timing.

| Default Xbox control | Captured intent | Desktop review alias |
| --- | --- | --- |
| RT | Held normalized fire value; Shooter tests >0.8 | F held |
| X | Reload press; processed +0x20 mask0x10 | R |
| A | Attack press; processed +0x28 mask2 | E |
| B | Next weapon press; processed +0x24 value1 | Q |
| RS click | Aim-mode press; processed +0x20 mask8 | T |
| LS click | Crouch press; processed +0x20 mask2 | C |
| LT | Held evade threshold >0.1 | Space |

The desktop letters are explicit host aliases, not reconstructed Windows
configuration defaults. Native capture preserves keyboard down/up taps drained
in the same pump, filters OS repeat for press actions, and clears held/edge
state on loss of focus. Controller input remains sampled at the host pump.
Menu confirmation/cancellation stay separate consumers of A/B. No mouse aiming
or trigger/stick swap configuration is added by this capture change.

Y produces processed +0x20 mask4, but its action consumer remains unresolved in
this bounded investigation; it is not assigned an invented action. Shoulders
also participate in the original magic selection packet, whose gameplay
admission is separate from these captured weapon controls.

Aim mode is not a two-state held aim button. `0x62ed80` increments the signed
low nibble of Shooter +0x28 modulo `0x617500(active_weapon)`. That function
returns two ordinarily, but for the flagged alternate weapon parameter kind3
it returns the selected table count +2. A positive mode keeps the raised
condition active without RT, and the owner subsequently requests the
corresponding camera/aim context through `0x604600`. Capture alone does not
implement that camera owner or all weapon-specific modes.

Shot animation follows successful `0x616530` admission, not every frame with
RT held. Reload `0x6309e0` computes missing ammunition fraction, consults the
original resource-cost acceptance, and publishes the accepted reload message.
Weapon cycling resolves available weapons before publishing its animation
message. Attack enters its own accepted action state. Native input cannot
invent ammo, availability, damage, target interactions or those outcomes.
A separately labeled animation review may supply external observations to
exercise these presentation consumers; it must not be reported as completed
combat gameplay.

## Held-fire cadence and presentation admission

Hardware update `0x62ed80` invokes the fire receiver only when trigger exceeds
0.8 and actor+0x126c is already positive. This check precedes its queued raised
lifetime refresh (6 seconds, or 0.75 for class 3). The first held frame therefore
arms raising; refreshing the timer must not manufacture same-frame eligibility.
`0x630400` attempts fire only when the upper controller owner is absent. Its
weapon call `0x616530` returns 1 while the weapon's cooldown is positive, 2
when the required ammunition is absent, and 0 after acceptance. The original
held trigger therefore does not authorize a new animation every rendered frame.
Each accepted projectile adds `1 / specification[0x90]` seconds to the cooldown;
the source repeats this addition when a preceding update left negative debt,
then submits one Shooter animation plan. `0x617a30` subtracts consumed game delta
only if the old cooldown is positive, otherwise writes zero. Keep the negative
overshoot for one interval; neither clamp every subtraction nor accumulate idle
debt indefinitely. Source weapon update follows its parent's request.

The base specification comes from `0x617260`'s
`witch/weapon/waN/00_spec` lookup. The installed resource gives:

| Class | Shots/second (+0x90) | Raised movement scale (+0xb0) | Capacity (+0x94) | Full recharge cost (+0x60) |
| --- | ---: | ---: | ---: | ---: |
| 0 | 10 | 1 | 80 | 20 |
| 1 | 1 | 1 | 16 | 40 |
| 2 | 1.8 | 1 | 24 | 40 |
| 3 | 60 | 0.9 | 400 | 40 |

These values are read from the retained customization tree, not invented class
constants. Positive capacity consumes one packed ammunition count (weapon+0xa4,
bits 16–25); exhaustion returns 2. `0x6309e0` calculates missing fraction
`1-current/capacity`, multiplies by the recharge cost, and requires successful
actor-resource admission through `0x600200` before queuing recharge. Reload
receiver `0x618150` refills the packed count, clears cooldown and selects weapon
motion 3 or 4. Those combat mutations are not implemented by an animation preview.
The source also samples a spread random value before its cooldown test; the
presentation gate does not claim to reproduce combat RNG consumption.

`WeaponPresentationSettings` prepares rate and movement scale once.
`WeaponPresentationGate` preserves the cooldown arithmetic and upper-owner gate,
with an explicit caller-supplied accepted observation. The labeled interactive
animation review supplies that observation; it does not report ammunition,
projectile, hit or recharge-resource outcomes. Trigger thresholding stays at the
input compatibility boundary. Review reload is one accepted edge, with the upper
owner gate preventing a held input from repeatedly resetting the action. Pause
consumes no gate time; accepted review reload/reset clears its timer. The consumed
engine delta bound of 0.25 seconds and rate sanity bound 1000 bound catch-up work;
the installed maximum is 60. No per-frame allocation occurs.

`0x617500` normally returns two aim modes. Its additional modes depend on an
active enchantment index, effective specification +0x58 equal to 3 and that
index's separate table +0x1c. The basic review does not fabricate those spell-owned
extra modes. Original exports: pass117 Shooter/weapon consumers, pass05b weapon
update, pass32 specification lookup; the source algorithm is explicit and needs
no replacement-engine timer heuristic. Core E.4 and performance MEM.9 were read
for both design and implementation. Tests distinguish held input, upper/accepted
admission, residual catch-up, debt expiry and reset; the original-root corpus
checks all four authored rates and scales.


### Weapon review labels

The UI maps resource classes 0–3 to **Gunrod (default)**, **Shotgun**, **Cannon**
and **Gatling**. Original reload bindings 337, 369, 401 and 433 resolve to the
magazine, shot_gun, cannon and chaingun families respectively; the accepted
reload receiver selects their class-specific base plus the retained body stance
nibble. The ignored `animation-coverage-bindings.json` census retains the exact
slot/publication evidence. The publisher's May 1, 2018
[PC changes article](https://www.xseedgames.com/2018/05/01/whats-new-with-bullet-witch-on-pc/)
uses the names Shotgun and Gatling and identifies Gatling as the slower raised
weapon, matching the class-3 movement-scale setting. Class 0 deliberately uses
the descriptive default Gunrod label rather than asserting an unverified
localized machine-gun name. These are display labels; source class IDs and
animation bindings remain unchanged.

## Normal weapon cycling presentation

The live B/Q path previously reused the neutral Weapon Class preview and omitted
the already implemented switch animation. The normal route now follows the
recovered source order. Hardware `62ed80` reads processed input+24, calls next
`604370` (or previous `604490`), compares the resulting selected entity and only
queues `62e290(0)` when selection changed. `603900` retires the old model through
`616440`, changes the selected nibble, and creates the new model through `615900`
with desired rest1 or2 according to the existing raised timer. That constructor
selects default0 then the requested rest, fresh with 0.3-second blend. The
standing/crouched receivers `62fb90/62e8e0` then submit the upper switch family,
retaining the previous body direction nibble. There is no source timer delaying
the model swap until the hand animation completes.

The port's normal switch retains the shared body/upper/additive events, body
clock, raised state, physical/controller/camera ownership and body deformation
state while preparing a separate selected-weapon clock/catalog. Commit publishes
new weapon draws before adopting, then submits the source upper request. It does
not invoke the menu's neutral action reset. Idle body substitution of the upper
switch family on the following update is the existing source policy; body clock
retention at the transaction boundary does not prohibit that legitimate selection.
All four immutable model/support/device owners now remain mission-resident;
selection borrows an admitted entry and recreates the selected instance without
archive reads/uploads or global world/audio pause. Host elapsed time is preserved.
See [weapon residency](weapon-selection-residency.md) for source/reference evidence,
rollback, native progression and activation measurements.
The class-specific cooldown survives switching; `617a30` updates +d0 outside the
model-present branch. Inventory availability and ammo are still explicitly
external; the current scene admits all four classes for presentation.

Both C++ MCP corpora were read for design and implementation (E.4, MEM.9).
The existing ownership/ring reference comparison remains applicable; this change
introduces no new animation algorithm or per-frame allocation. Source exports
are `pass-117-alicia-shooter-policy`, `pass-70-player-weapon-bindings` and
`pass-05b/00617a30`. Actual-resource corpus cases cover publication rollback,
four raised switches, lowered/crouched variants, frozen commit pose, stable body
clock identity, resumed source idle synchronization and audio ownership.
