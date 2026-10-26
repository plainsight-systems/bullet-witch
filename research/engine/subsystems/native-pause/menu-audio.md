# Native pause audio: recovered behavior and ownership

Status: source investigation and native audio implementation, 2026-10-25.
In-process scheduling contracts and original-content FACT dummy-device mixed
PCM checks pass. Native user listening acceptance remains separate. Original executable reads used
the hash-bound `research/tools/engine_image.py`; sound/category reads used the
existing extracted original `BW_COMMON.csb` and `BulletWitch.xgs`. Original data
and disassembly remain ignored evidence.

## Original menu events

The common dispatcher at `0x4c5e20` indexes the native event table at `0x8367b0`
and calls the shared audio request owner at `0x44c930`. Its cue value packs bank
in bits 16–23 and cue in bits 0–14 (`0x44d810`). Every event below uses bank 0,
`BW_COMMON`, already prepared by the reconstructed mission audio owner.

| Semantic event | Logical ID | Cue ID | Authored cue |
| --- | ---: | ---: | --- |
| Confirm | 1 | 362 | `SE_SYS_DCS` |
| Cancel / back | 2 | 0 | `SE_SYS_CCL` |
| Selection changed | 3 | 363 | `SE_SYS_CSR` |
| Panel change | 4 | 1 | `SE_SYS_WCG` |
| Error / rejected action | 8 | 5 | `SE_SYS_ERR` |
| Saved notice | 14 | 11 | `SE_SYS_SAV` |
| Pause entered | 15 | 12 | `SE_SYS_PSE` |
| Dialog opened | 5 | 2 | `SE_SYS_WOP` |
| Dialog closed | 7 | 4 | `SE_SYS_WCL` |

Root pause update `0x4cd020` requests pause-enter immediately and panel-change
with a 0.4-second delay. Confirm requests event 1, cancellation/Start resume
requests event 2, and an actual selected-row change requests event 3. These are
state-transition events, not sounds emitted every rendered frame.

Help update `0x4ddea0` confirms a topic with event 1, changes the selected topic
with event 3, and leaves either reading or the topic list with event 2. Both
confirm and cancel while reading return to the list with the cancel cue. The
inspected reading scroll path has no repeated sound request.

Options update `0x4d4c10` enters with panel-change delayed 0.2 seconds, exits with
cancel, and uses the same raw confirm/cursor/error cue IDs directly. Row changes
and ordinary volume adjustments use cursor; bounds/rejected operations use
error; accepted actions/default restoration use confirm. Its original save
completion dialog also emits event 14. This is evidence for that source state,
not permission to play a save cue when no save was performed. Descendant
settings without an implemented consumer must not claim success via sound.

`0x44c930` negates the delay into the request's elapsed field. The shared audio
update `0x44ccb0` adds elapsed time until zero before starting the request.
Constants `0x960ae8` and `0x9618ec` decode to 0.4 and 0.2 respectively. The final
request parameter is gain 1.0. Do not interpret the delay as volume.

## Pause is applied to admitted cues

At original menu-pause entry, calls at `0x478acf` and `0x478ad9` invoke
`0x44cb80` and `0x44cb90`. These queue one-frame pause requests in the shared
audio owner at offsets `+0x1a91` and `+0x1a92`. The shared update `0x44ccb0`:

1. Copies those requests into role masks and clears both request bytes.
2. Marks currently admitted role-0 effects and role-2 voice cue slots paused.
3. Calls the native cue pause adapter `0x506180` for playing/stopping slots.
4. Stops advancing marked cue elapsed time and delayed starts.

Resume producers `0x44cb00` and `0x44cb10` queue the inverse operation; the
update clears individual cue pause marks and resumes via `0x5063b0`. New menu
requests admitted after the pause pulse do not inherit a persistent global
paused flag. This is why UI audio can play while gameplay is stopped.

All inspected menu cues are XGS category 14, `SE_MO_F`, whose parent is category
5, `SE`. There is **no separate UI category** for these cues. Simply pausing the
entire effects category and expecting UI to bypass it is incorrect. The XGS
roots are `Music` (2), `SE` (5), and `VO` (21), under `Global` (0).

Original settings gain setters `0x44ca40`, `0x44cba0`, `0x44cc10` route to the
music/effects/voice output groups returned by `0x507380`, `0x5073a0`,
`0x5073b0`. The native implementation changes group output matrices through
`0x505d80`; it is not a demonstrated call to XACT category SetVolume. Options
normalizes stored volume by 1/256; its effects setting updates effects and
voice groups together. The XGS category tree supplies a plausible portable
implementation boundary, but equivalent category-gain behavior needs its own
verification before claiming those live controls are complete.

## Reconstructed boundary

`GameAudio` already owns the one FACT-backed `CueOutput`, prepares the
common/voice banks and selected game-voice closure, then seals registration.
There is no need to load another menu bank. The FACT bridge explicitly admits
only one runtime because allocator/decoder callback ownership is process-wide.
Do not create a FACT instance inside each menu or surface.

Before this slice, `CueOutput::pause(bool)` paused every active and future cue,
and `StageInspection` only pumped it after simulation admission. Both behaviors
prevented menu audio during pause. The implementation replaces that policy.

The implemented ownership is:

- Add an explicit world/presentation pause domain to cue requests and retained
  slots. Gameplay requests default to world; menu requests explicitly use
  presentation. World pause affects world slots and newly admitted world
  requests. Presentation cues remain audible. This is an architectural
  translation of the original admission/pause lifecycle, not a claim that the
  original had separate UI sound categories.
- Keep the existing prepared audio resource owner and runtime. Introduce a
  game-owned menu-audio facade borrowing cue playback only; it owns the logical
  event mapping and a bounded delayed-request queue. Pause, Help, Options and
  later front-end menus use the same facade. Flows publish transition events;
  presentation/resource surfaces do not infer sounds from redraws.
- Service the output once per presentation/host frame, including world-paused
  frames. Move that responsibility out of actor simulation. World control
  continues to own world pause; menu code gets no authority to unpause world
  audio or mutate the world clock.
- Advance delayed UI requests with supplied presentation elapsed time. Reuse
  the existing shared timing ownership rather than adding a menu wall clock.
  Teardown must retire pending requests and borrowed references before the
  runtime owner is destroyed.
- Keep mix-level observation separate from profile mutation and persistence.
  The subsequent authorized settings work adds live gain consumers below; cue
  playback alone does not establish unrelated settings behavior.

The original audio task is persistent (`0x4acb30`, recurring `0x4acce0`, service
`0x507ff0`), so future front-end audio should extend the shared session owner's
lifetime rather than instantiate a second device. That session-wide lifetime
migration is not necessary just to reuse the existing mission runtime for the
current pause slice.

## Reference and verification scope

Read FAudio revision `6839b88e304a046ae1a609ff14a085371e02a4e0`:
`src/FACT.c` `FACTAudioEngine_Pause` visits currently playing cues in a category;
`FACTAudioEngine_SetVolume` propagates category gains through children.
`src/FACT_internal.c` `FACT_INTERNAL_ParseAudioEngine` defines the original
XGS category records and names. These were used to verify the actual category
tree, not to invent a new sound format or mixer.

Both C++ corpora were queried for this design; full I.1 (`ri-explicit`) and
MEM.9 (`guidelines/memory/MEM.9-embedded-allocate-at-init.md`) were read.
Explicit pause-domain/capability interfaces expose authority; bank preparation,
fixed cue slots and a bounded scheduling queue preserve the existing bounded
allocation contract. Vendor cue allocations remain domain-accounted rather
than being misrepresented as allocation-free.

Required implementation verification: existing world cue freezes/resumes; a
presentation cue starts and finishes during pause; future world cues remain
paused; parent/child open sounds occur once with recovered delay; back/confirm
and actual selection changes select the correct authored cue; no post-teardown
pending cue survives; native shared-runtime audio is audible during pause.
A fake output can check routing but cannot establish native audibility.


## Settings gain implementation

The game-facing `AudioMixLevels` accepts stored values 0–256. `GameAudio`
observes that value and applies `MixGains`, with effects and voice both using
`effects / 256`. This path has no authority to mutate or save a Profile.
`CueRequest::mix_bus` identifies effects, music or voice; original voice
requests explicitly select voice. Pause domain and mix group are independent.

Category-only substitution was rejected with an actual counterexample:
common-bank cue 271 (`SE_BGM_WAL01`) has Music category 3 but an effects request
still uses original effects output routing. The original applies a scalar to
output groups (`0x505d80`); pinned FACT's `FACTCue_SetOutputVoices` and
`FACTCue_SetOutputVoiceMatrix` are TODOs that return success. Neither is used.

`cmake/PatchFact.cmake` validates exact pinned fragments and is idempotent. It
adds a cue-owned mix scalar initialized to one. `FACTWave_SetVolume` retains its
authored volume separately and multiplies the cue scalar at the existing native
voice-gain application. New waves receive this gain before native start. The
project C bridge publishes changes under FACT's existing API lock and refreshes
active and upcoming waves from their retained authored values. Mute/unmute never
divides by an old gain, and later RPC/fade/category updates cannot overwrite the
user setting. Both existing and future waves follow the same scalar path.

This establishes original linear output-gain equivalence for the admitted
common/voice corpus: inspecting every sound record in `BW_COMMON`, `VOICE`, and
`GAME_VOICE` found no DSP flag. It does not establish future nonlinear DSP or
reverb-tail routing equivalence to the original multiple submix groups. That
future ownership belongs to the actual effects reconstruction, not a silent
claim that arbitrary mixing graphs commute with gain.

Logos (`0x49e320`), title (`0x499ae0`) and opening advertisement (`0x49e610`)
read the BGM level through `0x4bb7c0` and pass it into `0x562f00`/`0x562780`.
The original DirectShow conversion is `2000 * log10(level / 256)`, with a
−10000 hundredth-dB floor. The reconstructed media output accepts the equivalent
linear 0–1 gain, with zero muted. `MediaAudio` retains it across prepare/stop and
publishes a scalar atomically to the sole AudioQueue service owner. That owner
applies `kAudioQueueParam_Volume` before starting and when the value changes;
the control thread never reaches into a live AudioQueue handle. Gain does not
change timestamps, decoded samples, pause, playback speed or media completion.
Core CP.2 (`rconc-races`) was read for this publication boundary, alongside the
existing I.1/MEM.9 design and implementation consultations.

## Verification performed

- Scheduling contracts: exact cue mapping; original delayed opening; due request
  retirement even on exhausted output; fixed batch/queue limits; malformed enum,
  timing and capacity rejection before partial publication; presentation-domain
  routing; no backend pumping or pause mutation from the menu scheduler.
- Original-content FACT dummy-device check: a presentation cue finishes while
  a newly requested world cue stays paused; world cue completes after resume;
  domain-retained bytes return to zero.
- Original-content mixed PCM captured through FAudio's documented
  `EngineProcedureEXT`, in an ignored instrumented research harness: full-scale
  common cue energy 70.3737; half-gain energy 17.5934 (one quarter, as required
  for half amplitude); muted energy zero. The same SE-category cue routed to
  voice remains full-volume when effects is muted; explicitly routing it to a
  muted music group yields zero. Restoring full gain restores original energy.
  An already-playing original ambient cue also remains silent across native
  updates after mute, then produces nonzero PCM after restoring half gain.
  Domain-retained bytes return to zero.
- Media gain rejects negative, greater-than-one and nonfinite input before
  publishing; existing allocation-failure/stop ownership tests remain in place.

Evidence lives under ignored `research/evidence/native-pause/audio/`. These
checks exercise actual native mixing with original data; they do not claim a
human listening review or native visual acceptance of the complete pause flow.
