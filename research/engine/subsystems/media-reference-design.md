# Media playback: established implementation comparison

October 22, 2026. Source inspection and native failure diagnosis. This is design
evidence, not a claim that the replacement is implemented or playback accepted.

## Why the current path is rejected

The native failure snapshot recorded 20,480 stereo sample frames submitted
(0.426667 seconds), 53 decoded video frames, and a later audio arrival after
exhaustion. Maximum draw time was 1.63 milliseconds. The mixed decoded-sample
path couples audio availability to future video retention. Moving that same
sequence to a worker did not remove the dependency. The inferred design defect
is head-of-line blocking; the replacement must remove it before further pacing
tuning. See ignored evidence
`research/evidence/reconstruction/native-startup/native-failure-diagnostic.log`.

## References actually inspected

### FFmpeg n7.1.1

[ffplay.c](https://github.com/FFmpeg/FFmpeg/blob/n7.1.1/fftools/ffplay.c):
`read_thread`, `stream_has_enough_packets`, `audio_thread`, `video_thread`,
`sdl_audio_callback`, `compute_target_delay`, `video_refresh`.

Demux routes compressed packets into separate stream queues. Audio and video
have independent decode/output progress. Its audio clock relates sample
presentation timestamps to outstanding software and hardware audio, and video
scheduling follows the selected master. Stream drain is distinct from demux EOF.
Its configurable queue and realtime policies are not universal memory guarantees.
Adopt those separations; specify our own bounded local-file admission and native
output contract. Do not copy SDL latency assumptions into AudioQueue.

### Godot 4.5-stable

[video_stream_player.cpp](https://github.com/godotengine/godot/blob/4.5-stable/scene/gui/video_stream_player.cpp):
`_audio_mix_callback`, `_mix_audio`, `mix`, process notification.
[video_stream_theora.cpp](https://github.com/godotengine/godot/blob/4.5-stable/modules/theora/video_stream_theora.cpp):
`update`, `get_time`.

A bounded resampler mediates main-thread production and audio-thread consumption.
The Theora update distinguishes audio/video readiness and ends, retaining partial
audio writes. Its time-compensation code includes an unresolved latency comment.
Use its producer/consumer separation as evidence; do not adopt this timing path
as proof of correct synchronization for ASF/WMA Pro.

### Unreal Engine release checkout

Repository: [EpicGames/UnrealEngine](https://github.com/EpicGames/UnrealEngine),
revision `f45729c0b6f097038e72b52d366a62baeca3275b`; checked-out Build.version
reports 5.8.4. Local reference: ignored `research/references/UnrealEngine`.
This is a shallow, filtered, sparse source checkout, not an engine build.

Inspected source paths:

- `Engine/Source/Runtime/Media/Public/IMediaSamples.h`: separate audio/video
  fetch, capacity and availability observations.
- `Engine/Source/Runtime/MediaUtils/Public/MediaSampleQueue.h`: sample ownership,
  producer/consumer operations, flush markers and diagnostic count limits.
- `Engine/Source/Runtime/MediaAssets/Private/Assets/MediaSoundComponent.cpp`:
  `FMediaSoundGenerator::OnGenerateAudio`, `UMediaSoundComponent::UpdatePlayer`.
- `Engine/Source/Runtime/MediaUtils/Private/MediaAudioResampler.cpp`:
  `FMediaAudioResampler::Generate`.
- `Engine/Source/Runtime/MediaUtils/Private/MediaPlayerFacade.cpp`:
  `GetTimeStampInternal`.
- `Engine/Source/Runtime/Media/Private/MediaClock.cpp`: input/fetch/output/render
  sink phases.

The sound generator consumes a dedicated audio sample source and publishes a
sample timestamp paired with host time. Flush generations protect observations
across stream changes. Missing PCM can produce device silence without publishing
a fabricated new media timestamp. The facade distinguishes sample availability
from a known stream end and separates presentation-time queries. Engine phase
notifications are not themselves a hardware playback clock.

Adopt separate stream capabilities, explicit consumption observations and stream
generations. We do not need Unreal's object system, editor integration or entire
media framework. No Unreal source is copied into project implementation or
tracked research; the local checkout is reference material.

## Required design outcome

The replacement design must specify compressed and decoded buffering separately;
audio progress independent of render/main-thread stalls; device time versus media
time and latency; per-stream EOF; priming and recovery; retained final-frame
presentation; bounded failure; and cancellation through worker/callback/GPU
quiescence. Arbitrary buffer growth, duplicate file readers and per-logo timing
exceptions are not a substitute for those contracts.

Full reference snapshots and hashes are ignored under
`research/evidence/reconstruction/native-startup/reference-players/`; Unreal
checkout provenance is in `research/references/unreal-provenance.json`.

## Engine and game boundary

The approved implementation follows Unreal's engine-level separation while
preserving Bullet Witch's recovered game behavior. The shared player owns media
control and completion, independent stream queues supply samples, the native
audio consumer supplies consumption timing, and native video presentation owns
its texture/submission lifetime. This does not require an engine-wide mixer or
Unreal's object system for the current movie component.

Unreal's `MoviePlayer/Public/MoviePlayer.h` and
`MoviePlayer/Private/DefaultGameMoviePlayer.cpp` also define generic startup/loading
movie control through registered `IMovieStreamer` implementations, including
Media Framework integration hooks. It is a separate module; not every registered
streamer must use the same Media Framework backend.

Here the original Lua chooses/waits for screens; reconstructed C++ screen logic
chooses media, skip rules and fades. Native macOS implementation belongs in
`src/platform/macos/` and is selected only by the Apple build. Shared media behavior
and contracts must remain usable by another target's output implementation.

## Media timestamps and device time

The first integrated run of the replacement exposed a contract mismatch, not
lost PCM. At stereo source frame 36,864, Marvelous supplied a decoded timestamp
of 0.853 seconds after the preceding segment ended at 0.853333333 seconds.
The native output rejected that correction because it independently required
media PTS to be monotonic to within one sample. The decoder had preserved the
actual source timestamp, and its PCM still matched the independent conversion.

FFplay's `audio_decode_frame` (the pinned n7.1.1 source, lines 2456–2462) assigns
its audio clock from each decoded frame's timestamp plus its sample duration.
It does not impose that monotonic-source rejection. The relevant distinction is
monotonic device sample positions versus source media timestamps, which can
correct in either direction. Native ranges must preserve decoder-approved PTS
without trimming PCM, inventing silence or fitting a tolerance to this clip.
The player follows the corrected consumed timestamp and holds future video until
it becomes due. Unsupported large discontinuities remain a decoder outcome;
native timeline discontinuities remain a device outcome.

The ordered segment traces and the rejected run are retained under ignored
`research/evidence/reconstruction/native-startup/timestamp-segments/` and
`native-redesign-final.log`. The latter records the failed run, not acceptance.
