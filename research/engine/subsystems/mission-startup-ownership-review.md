# Mission startup: independent ownership review

October 26, 2026. Requested after the first native walkthrough reported music
cutting out and identified identity-matrix construction outside shared math.
This reviews the uncommitted mission-startup slice against the accepted physical
architecture, not only those two symptoms. All three findings below are now
resolved in inspected code; the entry descriptions preserve what was found.
No user acceptance or commit is implied.

## Findings at review entry

### P1 — Two music envelopes can control one playing channel

`MissionStartup` retains its own gain/fade/stopping state and writes the shared
`MusicOutput` in `begin`, `play_music`, `stop_music`, and destruction. Meanwhile
`GameSession::advance` always advances `FrontEndAudio`, whose independently
retained menu fade writes that same output and eventually stops it. Starting a
mission track does not revoke the old frontend fade. A menu fade still pending
at handoff can therefore change or stop the mission's replacement track.

Evidence locations at review entry: `mission_startup.cpp:54–75,209–241`,
`ui/audio/frontend_audio.cpp:33–60`, `session/session_services.cpp:108`, and
application composition of both borrowers from `GameAudio`'s single output.
The implementation owner independently confirmed that original frontend
`0x49a690/0x49b320` and Lua `0x47a5e0` reach the same central music owner
`0x44bdf0` through `0x44bd90/0x44c190`. Splitting its retained state between
screen and mission owners was not faithful to that boundary.

Correction: one session-owned command/envelope state, with frontend and mission
requesting play/fade/stop through that owner. Service it once from the existing
control interval; do not solve this with menu-specific delay adjustments or a
second timer. Verify an unfinished menu fade followed by mission PlayBgm,
checkpoint stop/restart, mission restart and frontend return. This is a
demonstrated conflicting-authority defect; no audible device reproduction was
performed by this reviewer, so the precise observed cutoff remains subject to
the integrated verification.

### P2 — Script startup borrows more mutation authority than it needs

`mission_startup.hpp:40,92` accepts/stores unrestricted `MissionControl&`, but
its actual uses are script push/pop and the script-admission observation. That
also grants user-pause mutation, hardware restriction and input-route mutation.
The earlier timing correction deliberately exposed `UserPauseControl` instead
of lending this same full owner to unrelated borrowers.

Similarly, `MissionWorldActivation::world` at line 21 grants the full mutable
`WorldRuntime`, although startup uses only player visibility plus const player
and population-checkpoint observations. It does not need world advancement,
costume/weapon replacement, action-resource binding or mutable action access.

Correction: expose restricted script-pause and script-world capabilities over
the existing stable owners, with no duplicated state. Retain the gameplay
coordinator's actual begin/advance/present operations; a mutable reference is
appropriate where orchestration genuinely needs those operations. Add concrete
compile-time interface restrictions rather than broad lint assertions.

These are demonstrated excessive API capabilities, not evidence that the
current caller actually corrupts clock, actor or pause state. They matter
before further script/combat receivers accumulate around the same interface.

### P2 — Canonical identity construction belongs to shared math

The review-entry `animation/pose.hpp:59` defined its own numeric identity matrix;
new NPC code consumed that animation-owned name. The literal predates this
slice, but the user explicitly requested correction now. Keep identity
construction in `bw/math/matrix.hpp`; a compatibility alias may delegate to it
without maintaining a second implementation. Audit new projection/rotation
code for the same issue. This is an ownership/layout correction, not a claim
that the identity values caused an observable animation error.

The math correction was arriving while this review was in progress. Its final
code and verification are reviewed separately below before closure.

## Full-scope inspection and positive boundaries

The review inspected all changed/new production families: application/mission
composition, original placement decoding and loading, world population,
NPC initial motion/appearance/attachments, shared retargeting and engine pose,
Lua context/serial callbacks, UI modal/system text/goal presentation, audio,
render scene/native instance publication and sprites. It also inspected the
associated build additions, regression tests and authored implementation/source
records. Separate Alicia research and combat-planning documents were excluded.

- **Lifetime and restart:** application destroys script callbacks before the
  message, gameplay, world, resources and load definitions. WorldRuntime destroys
  WorldPopulation before StagePresentation and MissionSimulation. Population
  retires scene membership while const actor borrows still exist. Loading
  cancellation clears shared motion, dependencies, palettes and their residency
  after live consumers retire. No dangling borrow was demonstrated on these
  actual paths.
- **Resources versus instances:** archetypes retain prepared immutable inputs;
  each NPC owns independent playback/local pose/palettes. Native submission
  copies borrowed palette and texture overrides and gives deformable instances
  separate shape storage. Shared GPU geometry and textures are not duplicated
  per actor. Whole-scene copying during transactional membership changes is an
  explicit current cost, not a claimed optimal streaming design.
- **Simulation versus presentation:** the world supplies admitted delta to NPC
  animation and population; renderer/lighting receive const actor observations.
  StagePresentation owns membership and common character-light publication,
  not behavior selection. Exact new material selectors are admitted explicitly,
  rather than accepting all shaders sharing feature bits. No new actor-owned
  world clock or rendering-owned AI was found.
- **Lua:** each retained coroutine owns a registry anchor, pending continuation
  and terminal error state. Reentrant operations reject before mutation;
  retiring a slot invalidates its ID and releases the poll/anchor. MissionDriver
  reserves one callback slot and dispatches serially. Context teardown clears
  captures before closing the VM and its bounded heap. Reviewed tests cover
  independent continuation cancellation, stale IDs, failed start recovery and
  bounded reuse. This inspection is not a new fault-injection run.
- **Clock and pause:** world and control intervals are copied observations of
  the existing WorldFrame; script waits retain sequence IDs to avoid consuming
  one interval repeatedly. Population/animations do not advance on an excluded
  simulation frame. Script timers use simulation time; modal fades and system
  text use their documented control service. The excessive control borrow above
  is distinct from clock arithmetic, which did not show a second authority.
- **UI:** instruction text is resolved from purchased localized pages; page
  acknowledgment is real input, not timed success. Surfaces own art and bounded
  sprite preparation. System text is appended after the independent cockpit/
  pause/modal base. Native append validates the whole suffix before publishing.
  Active input family is copied to prompt resolution, not obtained through a
  mutable hardware owner.
- **Loading and content:** placement strings borrow stable retained bytes;
  decoders expose const records and reject unsupported distributions. Grounding
  calls the simulation's const scenery query. Preparing a model does not create
  an actor; trigger-fired and deferred/live population state remain world-owned.
- **Unsupported producers:** live teleport after first construction, airborne
  StateStart, AI/combat, dynamic background mutation, full checkpoint restore
  and mission-clear outcomes are explicitly bounded. The unsupported goal
  producer retains a suspended script rather than fabricating completion.
  Those are accepted slice boundaries, not fixes claimed by this review.

### Investigated suspicion rejected: retirement flags

The decoded archetype `actor_flags` field is not the flag word tested for
deferred retirement. In original `0x47ef40`, row 6 fills spawn offset `0x10`
(`local_358` relative to `local_368`); offset `0xb4` (`local_2b4`) comes from
family classifier `0x44af80`. Retirement `0x445580` reads actor `0x1398`, which
is its retained spawn record at `0x12e4` plus `0xb4`. For the currently admitted
EA/HM/HW families, values 2/8 correspond to that classifier. Treating ignored
row-6 flags as a demonstrated retirement bug would be incorrect. The source
comment should name family classification accurately; later combat/death flags
remain future mutable actor state.

## Review provenance and verification limits

The implementation owner confirmed pre-handoff ownership checking consisted of
their own borrow/destruction/clock inspection, source contracts and tests. There
was no independent whole-diff ownership review before the user's request. The
passing gate therefore must not be presented as proof that all architecture
boundaries were independently checked.

This review searched both C++ corpora and read full Core C.9 (`rc-private`) and
performance COPY.5 (`resource-owning-types-are-move-only`) rules. Application:
audit the actual mutation operations borrowers receive and preserve stable,
noncopyable authorities. Private shared pointers that never escape do not by
themselves prove shared mutation; they were not reported as bugs just for their
type. The accepted repository architecture and workflow are the primary scope
and interface requirements.

No production edits, game launch, build or test run were made by this reviewer.
The existing implementation report distinguishes fixture audio/presentation from
native offscreen rendering, and those limits still apply. Final integrated
checks remain with the implementation owner.

## Correction review

- **Math ownership resolved in code:** inspected `identity_matrix` and the
  animation compatibility alias, shared column-vector multiply, cached planar
  rotation and their migrated placement/goal/native-validation consumers. These
  preserve the prior arithmetic/order and leave source thresholds/coordinate
  selection with the game. The native fixture now uses the same canonical
  identity. Reviewed the added orientation/w-lane/rotation tests and the math
  agent's reported passing resource/runtime/native groups and scoped fatal lint.
  The integrated full gate remains the implementation owner's responsibility.
- **Music authority resolved in code:** inspected the new session
  `MusicPlayback`/`MusicCommands`, frontend and mission callers, application
  composition/loading/main-loop service and retirement. Only `MusicPlayback`
  retains command/envelope state. The frontend owns cue sequencing and sends
  music requests; `MissionScriptRuntime` sends requests without retaining a
  second music fade or touching the device. The private-constructed command
  handle cannot prepare tracks, advance time, set gain or expose its owner.
  Preparation waits for admission after the predecessor stops. The application
  supplies existing presentation intervals, rebases wall sampling after
  synchronous preparation, and destroys script borrowers before explicitly
  retiring music. Ordinary reverse destruction keeps the music owner alive
  until frontend/session borrowers retire, and the device alive until after
  music. Reviewed regression cases cover pending frontend fade, ignored play
  while active, checkpoint stop/restart and failed preparation. This repairs
  the conflicting authority, rather than changing a menu-specific delay.
- **Script capabilities resolved in code:** inspected `ScriptPauseControl`
  forwarding and `ScriptWorldControl` forwarding plus their application wiring.
  Script pause permits only push/pop and script-admission observation. Script
  world permits only visibility and copied optional player pose/population
  checkpoint observations. Neither exposes its owner or grants clock, actor,
  user-pause, hardware-input or resource mutation. Both alias existing stable
  authorities without duplicate flags. Compile-time checks cover forbidden
  construction/conversion/mutations and the activation field's restricted type.
  Gameplay's legitimate orchestration reference remains. The persistent class
  and service are now `MissionScriptRuntime` and `MissionScriptServices`, with
  corresponding runtime/services/bindings filenames; original Lua `start()`
  remains the source entry point. Historical finding locations above refer to
  the pre-rename code.

No additional demonstrated ownership defect was found during this correction
review. This is closure of the reviewed findings, not a claim that future
gameplay receivers or currently unsupported producers are complete.

Verification evidence inspected under ignored
`research/evidence/reconstruction/mission-startup/music-review/`:
`source-comparison.json` records 780 source-executed frames, zero state
mismatches and maximum gain difference 4.958e-7. The transport and handoff PCM
analysis files report finite samples, nonzero loop/repeat windows and a 2.23s
exact-silence interval before subsequent music resumes. These are native
disk-output checks, not listening on the user's device. The implementation
owner reports the rebuilt host, six CTest suites and focused purchased-Lua
mission/restart route pass. The final gate log has passing fatal tidy entries
and ends with five Python tests passing. The implementation owner subsequently
confirmed gate process 18250 exited zero: six CTest suites, 264 fatal tidy
commands, five policy tests and the 885-file text-only content guard passed.
The reviewer inspected evidence rather than rerunning checks or launching the
game. User walkthrough acceptance remains pending.

## Subsequent acceptance

On October 27, 2026, the user accepted the repaired slice and authorized its
commit. This records disposition after the independent review; it does not
change the review scope, evidence limits or unsupported gameplay boundaries.
