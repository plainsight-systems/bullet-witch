# Game audio

Game cues have an engine audio boundary separate from cinematic media playback.
`CueOutput` accepts a resolved bank/cue identity, optional world emitter, and
finite caller-provided bank, cue and decoded-sample capacities. Game action and
voice policy resolve before this boundary. `PreparedWave` owns immutable
interleaved CPU samples; target output owns cue/mixer/device lifetimes.

Requests also identify their world/presentation pause domain and original
effects/music/voice mix group. World pause leaves presentation cues running;
mix group is independent of the authored asset category. `MissionAudio` services
the shared output once per presentation frame, including paused frames.
`MenuAudio` borrows cue playback and schedules recovered menu events from
supplied presentation time, without another runtime or an independent clock.
Settings observation applies stored 0–256 levels as linear gain; the effects
setting controls effects and voice together. Movie output receives the BGM gain
through its separate media service. See [native pause audio](../research/engine/subsystems/native-pause/menu-audio.md)
for source evidence, scope and actual mixed-PCM verification.

The macOS backend uses pinned FACT/FAudio for original XACT categories, cue
variation, RPC parameters, scheduling and looping, with SDL's CoreAudio output.
Loading copies original banks and decodes their WMAv2 packets once using the
existing shared LGPL FFmpeg dependency. An explicitly altered internal WMA
translation unit supplies immutable sample ranges to FAudio. Original sound and
wave banks are not rewritten. This is an internal pinned extension, not a public
upstream plugin API. Sample callbacks allocate nothing; upstream mixer and cue
allocations are accounted through the injected memory domain.

Registration is closed by `seal()`, before playback. One control thread owns
commands. Generational handles prevent old requests from stopping newly reused
slots. Missing banks, invalid cue indices and exhausted slots return unavailable;
unsupported codecs, malformed metadata and preparation-budget failures reject
registration. Only one FACT owner is admitted because its allocator and WMA
callbacks are process-global. Shutdown destroys cue and bank owners and joins
native audio work before releasing callback storage. A nonempty admission span prepares only the selected cues and all referenced
track-wave variations; other cues reject before native scheduling. Original
streaming banks use FACT's streaming constructor over owned memory I/O, keeping
flags and cue numbering intact. Only selected WMAv2 waves are decoded; general
background disk streaming and WMAPRO remain unsupported. Selected streaming
WMA partial loop regions reject before native construction, matching the
pinned backend's whole-wave cache capability.

BW_COMMON and VOICE together contain 868 WMAv2 waves and require 179.691 MiB of
retained float samples, before encoded banks and mixer overhead. This measured
size is not an implicit global memory allowance: the loading owner supplies the
budget. Alicia's prepared talk table resolves 26 weighted variants across ten actions.
Fourteen distinct GAME_VOICE bank50 waves add 2,009,088 float samples (7.664 MiB).
The combined reference is 49,114,112 samples; the mission supplies a 50,000,000
sample ceiling and three-bank capacity. The original 52 MiB streaming bank
remains resident compressed data; only its selected wave closure owns PCM.

`MissionAudio` loads catalog publications and an explicit external sound
folder, then seals the target-injected output. `CharacterAudio` resolves
weighted voice priority/history; independent body/upper channels retain SE
lifetimes. The stable actor environment supplies evaluated emitters, source
collision-surface context and a borrowed session RNG. `SourceRandom` preserves
the original seed1/69069 multiplier stream, shared with future game consumers.
Event-to-cue source mappings and current presentation limitations are in
[the audio research](../research/engine/subsystems/animation-audio-presentation.md).

The production Stage-to-motion-event path has completed a bounded CoreAudio
run with walking sounds, an evade voice and Thunder's authored streaming voice,
including pause/resume and clean teardown. Native scheduling reported no errors.
Subjective audibility and mix quality await user listening; this is not a claim
that every game sound or dialogue path is complete.

## Pinned dependencies and distribution

- FAudio/FACT26.10: commit `6839b88e304a046ae1a609ff14a085371e02a4e0`,
  archive SHA256 `987f06301b0aec73fb52472ecbbf58fa07a1737900ffccfe3e67f6746641810a`.
  [Upstream source](https://github.com/FNA-XNA/FAudio/tree/6839b88e304a046ae1a609ff14a085371e02a4e0),
  [retained zlib license](licenses/FAudio-26.10-LICENSE).
- SDL3.4.18: commit `829a65d769d935c4852f8159e964312c0957260a`,
  archive SHA256 `48e15cf1a6928453d77afa955d1fdfbbca9169e2bc3d76d111acb9b8bb076d66`.
  [Upstream source](https://github.com/libsdl-org/SDL/tree/829a65d769d935c4852f8159e964312c0957260a),
  [retained zlib license](licenses/SDL-3.4.18-LICENSE).
- FFmpeg7.1.1 uses the existing checksum-pinned shared LGPL-only configuration,
  extended with WMAv2 decoding. See [decoder distribution obligations](video-decoder.md).

The build fetches pinned source into ignored build directories, with no system
package fallback. FAudio's Win32 WMA implementation is excluded and replaced by
our clearly marked `src/audio/fact/wma_bridge.c`. `cmake/PatchFact.cmake` makes
an exact-fragment-checked adaptation to cue/wave volume: a request-owned scalar
multiplies, rather than replaces, authored RPC/category/fade volume. Native
publication uses FACT's API lock; its unimplemented output-routing APIs are
not called. The same pinned adapter closes omitted
track-RPC allocations and orders shutdown before final release to avoid the
verified worker/API-lock deadlock; see the research for exact ownership evidence.
Ship applicable notices and corresponding FFmpeg source/build material with any
binary distribution. No game bank or decoded audio is checked into Git.
