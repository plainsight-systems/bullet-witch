# First combat actor audio

The actor audio policy now lives under `src/game/actors/audio`. Alicia and NPCs
share motion-event cue interpretation and retained-channel lifetime. Actor model
family selects the original surface-sound class; authored placement data selects
the voice group independently. Neither field is inferred from the other.

## Source and ownership

Recovered PC functions `46e710`, `46f8b0`, `46f770`, `470580`, `46f960` and
`470a70` establish these contracts. `47ef40` reads character table column 24 as
a float, converts it to an integer and places it in character environment `+e4`.
The voice identity table recognizes groups 1 through 22. An absent group produces
silence. The transformed flag selects the original wormman variant table.

Sound actions 0 through 22 resolve through actor-class/surface tables; direct
cue-ID ranges remain independent. Motion receipts retain their source event
context, including interval end and current frame, so a retained sound uses the
same greater-than-six-frames admission condition as the original.

`GameAudio::prepare_character_profiles` performs loading-time preparation. It
collects additional stream cue dependencies, extends the existing native bank,
seals its immutable sample publication, then exposes stable prepared policy
references. It never replaces the existing banks. Duplicate profiles do not
repeat admission. Actors borrow those policies and the world voice pool; they own channel handles
and a retained request whose countdown only that pool may advance. Asset bytes stay in the original
installation or ignored evidence, never in these authored tables or reports.

The GVTD record's final float is a maximum randomized pre-play delay, not unused
padding. The original initializes a positive delay on the voice-manager update
using its shared integer random stream and the exact float scaling operation.
It checks expiry before subtracting the current world interval. The shared actor
policy preserves that countdown and requires the caller to supply an admitted
world delta exactly once; paused worlds do not call it. Stopping the actor voice
also retires a pending request. No second world clock exists here.

The established FACT/FAudio ownership and bank preparation decisions remain as
recorded in `animation-audio-presentation.md`; this extension adds original game
policy above that backend. Core I.1/E.4 and performance CDSA.32/MEM.9 were read:
prepared profile identity and publication are explicit, preparation is bounded to
loading, and event execution owns fixed storage.

## Verification and remaining boundaries

A metadata-only purchased-content witness prepares all 23 voice groups against
the actual talk/GVTD and bank definitions, checks stable reference/duplicate
admission, and returns the domain to zero retained bytes. EA0 groups 1 and 2 each
resolve 36 actions and 114 variants; group 4 resolves 37 actions and 119 variants.
The witness also verifies positive delay initialization, one shared random draw,
expiry-before-subtraction behavior and pending-request retirement. Its output is
an observing test double, not audible playback. Scoped fatal clang-tidy passes.

Some original weighted variants have no matching authored talk/cue entry. The
witness records these explicitly; group 3 includes action 22 and 25 variant 4.
These remain unavailable rather than substituted. The broader source manager's
global arbitration is now implemented as described below. Runtime-event voice
IDs remain a separate producer: the audited six-tree closure has maximum action
61 and every explicit variation argument is -1, so no such override is admitted. Voice-driven
facial/subtitle applicability was checked against the actual admitted talk
families, rather than silently omitted. The metadata witness alone is not
complete voice-manager parity or audible acceptance.

## Damage receipts and admission ownership

Original ordinary damage receiver `6867b0` cancels the actor's current voice
before its branch-specific request: normal/sustained reaction 1, additive 19,
grounded ordinary death 0, category-2 death 1. These requests are independent
of motion-event VOICE and impact sounds. Melee retains its separate 1/2 choice.
`HitReception` carries a copied optional action; the world routes it to the
existing actor audio authority.

`NpcAudio` borrows the session voice pool and owns three retained motion sound banks
for every admitted EA0 damage receiver, independently of selected AI admission.
The id100 controller borrows it for source voice-wait identities and ordered
motion-event dispatch. Other admitted EA0 receivers consume their sound/voice
events through the same owner; this does not admit additional AI behaviors or
weapons. World admission prepares immutable profiles and bounded channels;
retirement destroys the controller, then audio, then the population actor.
Only the admitted world frame advances voice delays. The near-player surface
selection is the recovered `5f8ac0` policy extracted from the selected controller,
not a second audio algorithm.

## Global voice and actor-context closure

The source manager has **two stream slots followed by eight memory slots**.
`46f130` first rejects a same-actor request of equal or lower importance (lower
numeric priority wins), chooses a weighted variation through `46f960`, resolves
its bank, then uses the first free slot in that bank. If occupied, `4701f0`
compares candidates: same actor uses priority; different actors with the same
action and voice class use distance to Alicia; other requests use priority.
Equal ranks retain the incumbent. The class byte is copied from the original
voice identity table: groups 5–15 class 0, groups 1–4 and 16–21 class 1, Alicia
class 2. It is unrelated to surface-sound class or physical body classification.

The eight history entries belong to this global manager. They contain actor
identity, voice group, action and selected variation. Variant exclusion crosses
actors sharing a group/action. `46f330` clears those variation exclusions when
fewer than three candidates remain and records the accepted choice when at least
two remain. Before bank classification, a valid nonplayer actor cannot repeat
its latest globally remembered action; Alicia is exempt. Failed pool admission
does not record history. Stable monotonic actor identities avoid pointer reuse
reviving a retired actor's history.

`GameAudio` owns `CharacterVoicePool` for the session; `WorldRuntime` borrows it
only after audio binding. Source singleton `438fa0` constructs the manager once
through `46ee50`; stop-all `46f570` retires ten requests without clearing history.
The reconstructed mission retirement preserves that same cross-stage history. Actor retirement removes the borrowed
request/environment before destruction. Only the admitted world update walks
slots, initializes randomized delays, starts cues, follows actor positions and
observes completion. The stream preparation boundary consumes one update;
memory requests may start on the first update. Backend completion alone does
not change a voice-wait observation in paused gameplay. The existing pinned
FACT `FACTCue_GetState` and `FACTCue_Stop` implementations were inspected again:
completion observation and synchronized cue stop remain backend responsibilities;
game admission, history and delay remain above that boundary. No independent
backend clock drives source actor decisions.

Actor sound context is a second output of the existing sound receiver.
`44bbd0` resolves the cue, then emits actor context **before** backend creation;
a valid cue therefore produces its world stimulus even if the device cannot
admit playback. Missing cues do not emit. Actions 0–21 carry action, emitter
position and original actor descriptor affiliation to `ActorSoundEvents`.
Weapon context is zero and does not use this producer. NPC audio borrows its
world sink; player audio uses an expiring weak capability so combat teardown
cannot leave a dangling sink during appearance or stage transitions. The
integrator owns the world four-slot priority queue and source NPC recipients.

`46fdf0` also reads talk `playtime`, `face` and `lip`. Actual `alicia`, `geist_a`,
`geist_b`, `geist_c` and `geist_capt` tables contain respectively 140, 456, 460,
500 and 504 properties; all three presentation properties are zero throughout.
Their subtitle/facial consumers therefore have no admitted requests in this
encounter. This does not establish that those consumers are unnecessary for
future dialogue families.

The added purchased-content regression uses a recording output to check both
pool capacities, farther rejection/nearer displacement, queued dispatch,
world-update completion, and retirement. The existing real-bank voice variant
check now exercises shared history. A separate sound check exhausts output
capacity after valid cue resolution and verifies the world emission survives;
an unresolved cue emits nothing. These are policy checks, not hardware
listening or native audiovisual acceptance. Integration/full-gate results are
reported by the sole integrator after the shared world wiring is built.
