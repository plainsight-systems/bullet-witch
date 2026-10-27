# Mission music authority and continuity

The live review reopened music validation after the opening track stopped and
checkpoint music resumed much later. The earlier corpus observed commands on a
fixture output; it did not prove audible continuity or exclusive authority.

## Source and demonstrated ownership defect

Original frontend initializer 0x49a690 requests packed track0x30008 with no
fade-in through 0x44bd90. Main exit 0x49b320 requests a one- or three-second
stop through 0x44c190. Lua `_PlayBgm` 0x47a5e0 resolves its script index and
calls that same play receiver with a one-second fade. Both feed the persistent
0x44bdf0 state machine. They do not own independent fade state.

The reconstruction instead retained one fade in FrontEndAudio and another in
MissionScriptRuntime. GameSession continued advancing the frontend during missions,
and both could set gain and stop the same device track. Preparation could
replace the device PCM while an old frontend stop remained scheduled. This is
a concrete authority defect even when a sufficiently long loading/message
sequence happens to let the old fade finish first.

One session MusicPlayback now owns commands and envelope. Frontend and mission
receive only a MusicCommands handle. The composition owner alone advances the
service from its existing control interval and retires it. The native output
continues owning PCM and two-source transport, independently of world pause.
The service preserves source command order: a play while playing is ignored
unless stop is pending; a new play during fade-out is retained until the old
source stops, then admits on the next service update. Preparation happens at
that admission, never while a predecessor can still fade the new track.

## Original mission timeline

Unchanged CM01 `start()` requests track7 after the acknowledged opening. CP610
sets checkpoint10, displays system text, waits three seconds, cancels text,
requests a three-second music fade and registers a five-second timer. The timer
requests track3. With continuing simulation, the intentionally quiet interval
is approximately two seconds after the fade ends. User pause can delay the
world timer while control-time fade continues. CP620 displays checkpoint text
but issues no music stop. This does not establish the user's earlier abrupt
cut/long silence as intended behavior.

## Verification and review boundaries

The native x86 functions 0x44bd90/0x44c190/0x44bdf0 were executed for 780
frames with explicit external playback observations. The reconstructed service
matches every active/track/start/stop observation; maximum gain difference is
4.96e-7 from printed float precision. The sequence includes an unfinished
frontend fade, a queued mission track, ignored play while already playing,
checkpoint fade and subsequent track. This checks the actual source service,
not a reference model derived from our implementation.

A separate real FACT/FAudio disk-output run played purchased track7 for 115 wall
seconds across its intro, loop and first repeat, with no inactive observation.
The capture contains 4,937,728 finite stereo frames; RMS around its first repeat
is nonzero. A second 35-wall-second native run uses the new service: pending
menu fade completes before track7 preparation; track7 remains active across its
intro/loop boundary; explicit three-second stop produces silence; the later
track3 request resumes playback. The captured PCM includes 2.23 seconds of
exact silence between those tracks. All tracked allocations return to zero.
SDL disk-output scheduling is not a hardware wall-clock calibration: captured
PCM durations are about112 and33.6 seconds respectively. These are native output
checks, not a user listening test or proof that every possible device is gapless.

The purchased-content focused route passes unchanged opening Lua, CP610/620
callbacks, music transitions and restart with the shared service. Its output is
a fixture; it supplements, rather than replaces, native captured PCM above.
Unit contracts cover incomplete frontend fade → mission admission, denied
clock authority on command handles, original duplicate-play admission, and
checkpoint stop/restart. The final full repository gate passes: host build,
six CTest suites, all 264 fatal clang-tidy commands, formatting and content
policy. The independent reviewer inspected the repaired source and closed all
three ownership/math findings; no further demonstrated ownership defect was
found in the reviewed slice. See
[independent review](mission-startup-ownership-review.md). The user accepted
the repaired slice on October 27, 2026; this does not retroactively prove the
precise cause of the earlier observed cutoff or expand native device coverage.

Evidence stays ignored under
`research/evidence/reconstruction/mission-startup/music-review/`: native x86
`source-timeline.json`, `source-comparison.json`, transport/handoff C++ probes,
`music-output-analysis.json`, `handoff-output-analysis.json`, PCM captures and
check logs. No original code or media is added to Git.
