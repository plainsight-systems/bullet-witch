# Animation audio presentation

October 23, 2026. Shared WMAv2 preparation and the native cue backend are implemented and
silently verified; typed game SE/VO receivers and mission ownership are implemented.
Production actor wiring and a bounded CoreAudio run are verified; subjective
audibility and mix quality still await user listening. See
[motion events](motion-event-timeline.md), [audio services](audio-and-movies.md)
and [bank construction](audio-bank-reference.md). Unimplemented side effects remain explicit in the actor presentation status.

## Original game requests

The motion receiver binds SE_0 at owner+0xc4 and VO_0 at owner+0x1b0. There are
411 sound and 36 voice records in the prepared Alicia corpus. They are action
requests, not a list of wave indices.

| Consumer | Recovered contract |
|---|---|
| SE entry `0x5f8ac0` | Reads action ID at record+0x20 and bone at+0x24. It preserves a first zero-duration exception and previous-frame guard. `0x5f8150` resolves a positive valid bone to its evaluated world pose, otherwise model world. A support segment and collision surface classification can select the sound and emitter location. Distance/global-manager/descriptor predicates alter that query; they must not be replaced by an unconditional default material. |
| SE resolution `0x46e710` | First checks published direct-ID ranges (`0x4ad400`), then action IDs0..22 use actor class and surface tables. Alicia selects actor class0. Unknown IDs remain unavailable. |
| Cue submission `0x44bbd0` / `0x44d810` | Resolves the action to a packed cue, then bank=(value>>16)&255 and cue=value&0x7fff. Checks loaded bank/count, reserves an instance and applies emitter parameters. Reproducing the event does not mean bypassing cue metadata. |
| SE retained lifetime | Looping/sustained requests retain an instance associated with action ID and bone. Receiver update `0x5f9b90`/`0x5f9bc0` updates matching instances; exit `0x5f83f0`/`0x5f8410` removes matching requests. Reset `0x5f7ff0` resets the first-event flag and destroys retained instances. One-shot lifetimes remain owned by audio. |
| VO entry `0x5f90c0` | Uses its previous-frame guard, reads record+0x20 and calls actor voice manager `0x4704b0`→`0x470580`. It does not submit that integer directly to XACT. |
| VO selection | IDs below62 use actor voice-group lookup and a table of priority/selection metadata (`0x46f8b0`, `0x46f770`). Larger IDs use named runtime events. `0x46fd50` constructs group/action/variant names or `rtevent_%04d`; `0x46f130` arbitrates actor handles, priority and finite voice slots, and resolves talk definitions. Prepared GVTD priority, weighted choice and finite history rules are implemented; global cross-actor limits and repeat-override behavior remain separate. |

The actual executable tables and BW_COMMON wrapper agree: action0's player
fallback is packed cue0x16c, `SE_CHA_ALI_WALK_M01`; surface2 selects cue0x18,
`SE_CHA_ALI_WALK_M02`. Action2's fallback is `SE_CHA_CMN_TMBL_M01`.
These are concrete mapping witnesses, not permission to use the fallback for
all surfaces. The ignored table snapshot records all23 player mappings.

Required publications include BW_COMMON bank0 and VOICE bank4. GAME_VOICE bank50
and SE_STREAM bank2 use external streaming wave banks. The startup audio owner
also requests `audio.xgs.BulletWitch`; its categories, RPC and DSP settings must
be initialized before faithful cue execution. MissionAudio now loads the settings/common/voice publications and the selected
GAME_VOICE closure independently of stage-bank metadata.

## Actual codec gate

Read-only inspection of original version46/header44 wave banks found:

| Bank | Waves | Encoding |
|---|---:|---|
| BW_COMMON | 584 | All WMA tag3, subtype bit0 (WMAv2) |
| VOICE | 284 | All WMA tag3, subtype bit0 (WMAv2) |
| STAGE4 | 6 | All WMA tag3 |
| Streaming GAME_VOICE | 2288 | All WMA tag3; includes five six-channel entries |
| Streaming SE_STREAM | 28 | 23 PCM, five WMA |

The prepared format reader retains encoded data, loop sample ranges and seek
entries. Metadata construction never established decoding support. The pinned FFmpeg7.1.1 build now also enables WMAv2. The shared game-wave
adapter submits original packets directly; it does not require an xWMA demuxer. Its movie decoder additionally constrains its stream
contract, so the game sound path cannot reuse that object as-is.

A diagnostic xWMA wrapper preserves the original encoded payload and cumulative
decoded-byte seek table. Three selected original waves decoded with installed
FFmpeg (exact version in ignored evidence), with no errors: BW_COMMON mono wave0
81920 frames, stereo wave180: 221184 frames, and VOICE mono wave1: 69632 frames.
Each decoded frame count exactly equals both authored duration and final seek
entry divided by channels×2. This verifies feasibility for those files, not the
pinned production build, looping, streaming or audible native acceptance. The
probe wrote only ignored evidence and did not play audio or alter the game.

## Smallest faithful implementation seam

Pinned FAudio/FACT26.10 revision
[`6839b88e304a046ae1a609ff14a085371e02a4e0`](https://github.com/FNA-XNA/FAudio/tree/6839b88e304a046ae1a609ff14a085371e02a4e0)
provides the closest existing XACT cue implementation. Source inspected here:
`FACT.c::FACTWaveBank_Prepare` and the bank/cue creation APIs,
`FACT_internal.c::FACT_INTERNAL_OnBufferEnd`,
`FAudio_internal.c::buffer_get_end` and its decode loop, and
`FAudio_platform_win32_wmadec.c::decode_wma`/init/free/end_buffer.

Its macOS build disables WMA decoding. Enabling the option alone selects a
Win32 Media Foundation implementation; its disabled path emits silence. There
is **no public decoder callback injection API** in this revision. A usable
adapter therefore needs an explicit, pinned backend extension, not merely a
link flag. The useful internal replacement seam is the WMA init/free/end-buffer
and `decode_wma` implementation: retain FACT cue/RPC/variation/loop ownership,
replace only compressed-sample delivery with a shared CPU decoder.

The shared decoder accepts mini-format metadata, immutable encoded
packets, cumulative decoded-byte offsets and requested sample ranges, returning
interleaved floats. FFmpeg7.1.1 `libavformat/xwma.c::xwma_read_header` establishes
WMAv2 bitrate normalization and six-byte codec extra data (byte4=31), plus the
WMAPRO-specific extra data. `libavcodec` send/receive owns packet decoding.
The current source FAudio block-align/average-byte lookup tables establish the
XACT mini-format conversion. Decode prerequisites include enabling WMAv2 and
validating actual six-channel WMAV3/WMAPRO entries independently.

This belongs behind shared engine/audio contracts and target device/backend
implementation. It is independent of the video player's one-decoder AudioQueue
transport. Preserve the existing FAudio mix/resample/spatial/cue semantics;
do not build a second elementary mixer or rewrite sound-bank cue metadata.
A backend extension must retain license/provenance and remain separate from
recovered game policy.

The decoder contract must cover sample-accurate PlayBegin/PlayLength and
LoopBegin/LoopLength, cumulative byte-to-sample accounting, seek by decode/preroll
rather than treating WMA packets as independently seekable, EOS drain, and
buffer transitions. FAudio may request resampler lookahead twice at the same
sample offset; consuming a sequential decoder twice would be incorrect. It
retains decoded output for this reason. Capacity and decode/cache work must be
prepared outside the native callback; the existing Win32 implementation's
callback reallocations are not a model for this engine's realtime allocation
policy. Define finite residency/queue limits and report exhaustion.

FAudio's custom allocator extension supports exactly one callback set across
all objects, without an instance context parameter. Integration needs one
explicit engine-lifetime allocation capability, consistent realloc accounting,
and callback shutdown before domain release. Native device allocations remain
external. FACT itself starts runtime work, so ownership/thread scheduling must
be documented rather than assuming it is a passive parser.

The shared decoder, pinned cue backend, prepared action resolver, retained
SE ownership and actor sinks are now implemented. The bounded native motion-event run below verifies device scheduling and
lifetime. Subjective listening, every action sound and all game audio remain
separate acceptance boundaries.

## Evidence and limits

Source passes129/130 are retained under ignored engine evidence. Ignored
`research/evidence/reconstruction/animation-audio/` holds hash-bound bank codec
census, resource CALL metadata, the player sound-map table with wrapper cue names,
original bank payloads, decoder probe script/results and exact installed FFmpeg
version. The source census reads little-endian XACT mini-format bits and validates
original archive ranges through the existing resource inspector. The production decoder/native tests below supersede the initial CLI feasibility
probe. No game application was launched or controlled by this work.


## Implemented backend verification

`include/bw/audio/{wave,output}.hpp` separates immutable shared CPU decoding from
native cue/device ownership. `src/audio/fact/` links the exact FACT and SDL source
pins documented in [game audio](../../../docs/game-audio.md). The native bank
bytes remain original, and the internal WMA hook serves prepared ranges rather
than rewriting bank metadata or decoding in the device callback.

All 868 common/voice waves decoded through the pinned shared adapter: 47,105,024
float samples (179.691 MiB), in 0.79 seconds in the diagnostic Debug run. The
original seek-table duration bounds every read. Some valid waves end with fewer
coded samples: common wave7 is 512 frames short. Native FAudio's WMA callback
zero-fills unavailable EOF data; the adapter preserves that behavior with an
explicit maximum of one WMAv2 frame (FFmpeg `wma_common.c` frame-length rule).
It rejects larger shortages and decoded overruns. Repeatable beginning, loop
and end reads are checked in the existing provisioned startup corpus.

The initial silent SDL dummy-device native probe registered both original banks and XGS,
played a common footstep and generic bank voice concurrently, paused/resumed, stopped them,
rejected stale handles and returned charged live bytes to zero. Retained bytes
while prepared were 196,258,387 including bank and mixer ownership. These are
preparation/lifetime witnesses, not audible CoreAudio acceptance or a claim that
all event mappings are implemented. Native actual-content audible acceptance
is deferred until the event receiver is connected.

Two pinned FACT lifetime defects were demonstrated during that probe and are
handled explicitly in the adapter: `FACTSoundBank_Destroy` omits track-level RPC
code arrays parsed by `parse_rpc_codes`; these are released under the same API
lock after cue consumers retire. Also `FACTAudioEngine_Release` holds its API
lock while joining a worker that can need that lock. The adapter first invokes
shutdown outside the lock and closes the bank locks cleared by shutdown, then
releases the engine. This ordering was verified by native thread sampling and
repeat teardown. No global catch-all allocation reclamation hides leaks.

The original game voice registry is now located: stable `audio.talkdata` owns
`alicia.bin` (STMANAGE) and `voice_weight.bin` (GVTD). Alicia's 36 VO records use
IDs0,2,3,7,8,19,56,58,59,60. Prepared GVTD intentionally leaves actions2/19
unavailable. The full Alicia table resolves ten actions and26 weighted
variants, including idle/action families beyond that event subset.

### Alicia streaming-bank correction (preparation acceptance)

The actual talk lookup rejects the earlier assumption that Alicia uses VOICE
bank4: death/spell `VO_PW0_*` names live in GAME_VOICE bank50, while
attack/dodge/damage variants remain in VOICE bank4. `0x46f130` probes bank4
to choose memory-versus-stream voice slots; absence does not mean absent speech.
The source talk `wav` property must resolve against both original publications.

The pinned FACT `FACTWaveBank_Prepare` streaming WMA branch already reads a
whole compressed wave per voice, while `FACT_INTERNAL_ParseWaveBank` requires
the original streaming flag to match its constructor. The bounded adaptation
uses FACT's streaming constructor over retained memory I/O, preserves bank
bytes/cue numbers, and decodes only the prepared Alicia cue dependency closure.
The codec hook recovers the canonical bank entry through FACT's wave callback,
so temporary streaming caches do not become PCM identities. Other cues reject
before native scheduling. This is resident preparation of selected original
streaming content, not a claim of general background disk streaming.

R.20 and MEM.9 were consulted again for this contract: owned bank/PCM lifetime
extends through joined native shutdown; cue closure and decoding occur during
loading, and sample callbacks only identify/copy immutable PCM.

### Prepared game receiver and verification

`5f8210` binds separate contexts for body mode1 and upper mode2. Only mode1
suppresses SE22 and2039..2053. `5f7910` initializes context default-surface0.
The support segment uses half of group0 header float+0x40 above/below the
evaluated emitter; costume0..5 extents are approximately1.184..1.192. Actions
0..5 and17..19 query within squared distance225. The other admission terms are
global manager byte+a2 (`489dc0`) or descriptor flags0x2100. The former is
initialized false by489cb0; Alicia descriptor flags start1 in5fe970. This is
**not** an actor visibility/frustum predicate. Outside admission source surface1
is used; an admitted query without authored contact surface retains default0.

The retained cue bit is resolved against default surface, while the actual cue
uses contact surface. Retention requires strictly more than6 frames remaining
(raw82dbd8). Updates/removal match action ID, retain the original bone and are
scoped to their motion bank. One-shot lifetimes stay with FACT. Support-particle
and actor-stimulus side effects are not fabricated by the sound consumer.

`470580` emits VO from actor physical world position+0x80/+0x84/+0x88, not
model translation or a named bone. Constructor5fe970 selects voice group22.
`46f130` rejects an incoming voice when the actor's active numerical priority
is already lower/equal. `46f330` clears same-action history when fewer than3
weighted candidates remain, and records a chosen variant only when at least2
remained. Shared session RNG5df8c0/5df8e0 starts seed1, advances uint32 by
state*69069+1 and returns (state&0x7fffffff)%bound; the draw counter advances once.
The actor adapter borrows this game-owned stream rather than reseeding audio.

The canonical startup corpus now verifies real talk/weight/cue preparation,
all26 variant resolutions, priority/history, surface lookup, mode suppression,
retained-position updates, per-bank reset and the strict6-frame boundary. The
policy dispatch itself does not change charged allocations; native FACT cue
allocations remain an explicit backend boundary. An SDL dummy-device probe
loads actual MissionAudio, emits footstep+dodge voice, then preempts with a
streaming death voice, pauses/resumes/stops, rejects an unprepared bank50 cue
and releases all charged bytes. That probe retained266,765,062 charged bytes
while prepared, including its catalog and native owners, then returned to0.
This is not native process RSS or device-driver accounting. No audible
acceptance is claimed.

The selected stream closure has14 waves,2,009,088 float samples and330,040
compressed wave bytes. Together with the complete868 common/voice-wave
reference, this is49,114,112 floats. General NPC/dialogue ownership, native
two-stream/eight-memory global voice-slot priority, the global repeat override
inside46f960, runtime-event IDs>=62, subtitle/lip effects, and other sound-bank
publications remain outside the implemented Alicia animation receiver.

### Production motion events through CoreAudio

The ignored `motion_audio_probe` uses the canonical controlled geometry output
with real MissionDriver/MissionLoading, StageInspection, actor/controller motion
clocks, typed event receivers, MissionAudio and FACT/FAudio. It creates no window
and sends no input to the user's game. The bounded 10.5-second sequence after
loading walks, pauses, performs an evade, then holds Thunder's start phase long
enough to cross its authored frame115 voice event, with another pause/resume.
It does not call CharacterAudio directly to manufacture event delivery.

Both SDL dummy and explicitly selected SDL CoreAudio runs completed successfully:
40 common-bank cues, one memory-bank voice and one streaming-bank voice were
accepted; the shared RNG consumed two draws. Paused animation produced no new
cues, native updates reported no decoder-bridge/device failure, and all charged
bytes returned to zero after teardown. Logs are retained as
`animation-audio/motion-audio-dummy.txt` and `motion-audio-coreaudio.txt` under
ignored reconstruction evidence. This proves production event routing, native
device scheduling and ownership. No loopback capture was available; subjective
audibility, balance and mix quality await the user's listening review.

The canonical Stage fixture holds Thunder's camera-owned start for120 frames
before accepting the target. Its earlier20-frame observation correctly replaced
the start before VO58 at frame115; Thunder's middle/end parameters contain no
voice records. Ancient Wall's VO56 at frame20 resolves bank4, so it is not a
streaming-bank witness. The controlled audio fixture explicitly acknowledges
completion of its previous voice before testing a same-priority replacement;
it does not invent a universal mock cue duration or alter production priority.

The pinned streaming-WMA branch supports only absent or whole-wave loop
regions. Admission now rejects a selected partial loop before FACT's assertion.
A diagnostic copy of original GAME_VOICE wave1133 was changed to an in-range
partial loop; registration rejected it and restored its charged-byte baseline.
Registering the unchanged original bank into that same owner then succeeded,
and teardown returned to zero. The game installation remained read-only.
`stream-loop-admission.txt` records this targeted failure/rollback witness.
