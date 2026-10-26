# Frontend audio preparation and ownership

The recovered title event 11 selects BW_COMMON cue 8 (`SE_SYS_TTL`). Main-menu
entry schedules event 4 after 0.3 seconds; changed-row, confirmation and cancel
publish events 3, 1 and 2 through the existing menu cue mapping. Difficulty
acceptance emits logical event 13, common cue 10, before its screen conceal. These are game
policy events, consumed by the shared session audio service.

The original main initializer at 0x49a690 requests packed music ID 0x30008.
The persistent music service at 0x44bdf0 resolves low ID 8 through the filename
table at 0x956b68 to `bgm_16.xwv`; it does not play an XACT BGM-bank cue. The
main flow at 0x49b320 requests a one-second stop on returning to title and a
three-second stop on accepted mission entry. Options preserves music. The
main destructor at 0x49a8d0 does not stop music. The service applies a linear
supplied-time envelope before the profile music gain; it owns no UI lifetime.

## Segments and native transport

The loader at 0x507e30 reads two four-byte-aligned RIFF XWMA containers and
then two little-endian frame positions. The parser at 0x5089b0 uses `fmt `,
`dpds` and `data`; 0x508ab0 submits packets through a native source voice.
The update at 0x508500 polls native SamplesPlayed and alternates two voices.
It starts the second segment when the first threshold is reached, then uses
the difference of the two authored positions as each subsequent period.
The old voice is prepared again after ten updates. No crossfade is applied.

For the purchased menu asset, decoded segment lengths are 456,704 and
3,176,960 stereo frames at 44,100 Hz. Authored positions are 454,960 and
3,630,081: the first switch is 454,960 frames and the repeating period is
3,175,121 frames. The tails overlap by 1,744 and 1,839 frames respectively.
The port retains both raw decoded segments and alternates native voices on
these source thresholds; it neither pads the loop to the RIFF duration nor
premixes an expanded periodic buffer. Retained PCM is 29,069,312 bytes.

The existing pinned FFmpeg 7.1.1 xWMA contract (`libavformat/xwma.c`,
`xwma_read_header`) supplies WMAv2 codec setup, cumulative decoded-byte
`dpds` timing and the documented bitrate corrections. The same existing
prepared-wave implementation handles explicit packet metadata from XACT and
RIFF; the RIFF adapter does not fabricate a sound bank or compressed format.
The native backend uses the already pinned FAudio implementation and existing
shared device. No second mixer or independent audio clock is introduced.

## Loading boundaries and measured costs

One session `GameAudio` owns the existing cue runtime. Initial preparation
admits only common menu cues; main entry prepares music once. Mission loading
extends common and registers voice closures once, publishing an immutable
callback lookup before new cue admission. Scene and menu policies borrow the
output; the runtime survives scene retirement and is destroyed after borrowers.
Bounded loading-screen pulses also advance the departure envelope and service
the same native runtime from the existing presentation-clock sample. They do
not advance world state or reenter Lua; loading cannot freeze the menu fade
or prevent a native segment handoff.
Mission policy publication occurs only after all bank admission and lookup
publication succeeds. A partial native admission failure is terminal for that
owner; later calls reject rather than claiming ready or duplicating banks.

Separate real-asset native dummy-device probes measured common menu subset
at 9,036,220 domain bytes / 0.155 seconds before adding the recovered
difficulty-accept cue. The earlier eleven-cue subset measured 9,430,436 bytes /
0.157 seconds (three bank capacity and 32 cue slots). Full common at 149,008,828 bytes /
0.487 seconds, and full VOICE at 43,569,662 bytes / 0.184 seconds. The full frontend adds purchase cue 17: the twelve-cue closure measured
9,692,580 bytes / 0.159 seconds with the same capacity. These include
encoded/native overhead, not only PCM. All returned to zero retained bytes.
The final session allows 60,000,000 samples: the established mission closure
is 49,114,112 samples and retained frontend music adds 7,267,328, totaling
56,381,440. The former 50,000,000 mission-only limit correctly rejects that
combined residency. These measurements justify deferring mission preparation
rather than eagerly adding roughly
140 MB of common sounds to title startup. Probe data is ignored under
`research/evidence/reconstruction/frontend-audio/`. Timing is machine-local
and is evidence, not a universal latency guarantee.

The two C++ corpora were consulted for design and implementation: Core E.6
scopes native/codec lifetimes; Performance MEM.9 moves decoding and storage
construction out of callbacks. The frontend facade only publishes commands
and advances fades using caller time; the session remains the sole device pump.

## Verification boundary

The real purchased `bgm_16.xwv` decoded to 7,267,328 finite float samples,
RMS 0.0618242, in 0.137 seconds. The domain retained exactly 29,069,312 PCM
bytes and returned to zero after destruction. Its authored segment lengths
and switch positions matched the source trace. The startup corpus now checks
that asset metadata plus rejection of insufficient sample budgets and a
truncated trailer. A corpus failure injection rejects bank 50 after voice bank
admission and verifies that no prepared policy is published and retry rejects.
Frontend policy tests cover lazy preparation, title cue,
one- and three-second fades, reentry, failure retry and no duplicate pump.
A 14.234-second run through the real FACT/FAudio runtime and SDL disk output
passed. Common expanded while music played; VOICE then registered and played
without replacing the runtime. World pause and world retirement preserved music.
A live gain change produced 0.98 seconds of exact zero samples, followed by
nonzero output. Music remained active after the intro duration and the loop's
11–12-second output window had RMS 0.0594. Explicit stop produced final silence.
All 1,255,424 captured float samples were finite; all domain allocations returned
to zero. Observed peak resident bytes were 221,680,202 for common, VOICE and
music (streamed Alicia voice was not part of this probe). Existing FFmpeg
packet diagnostics were printed during bank preparation; the APIs completed.
Evidence is `music-output.log`, `music-output.f32` and
`music-output-analysis.json` in the ignored frontend audio directory.

Final integrated gate results remain the composition owner's responsibility.
The 14-second native capture proves the intro-to-loop handoff, not a complete
72-second loop repetition. No user listening or gameplay walkthrough is implied
by captured native output.

## Integration review findings

The focused independent review found two concrete omissions in the first
composition: the declared eleven-cue menu closure had only ten initializers,
leaving mission-start cue 10 unprepared, and synchronous loading pulses did
not advance frontend audio. The cue set now explicitly includes 10 and the
existing corpus checks its exact membership. Loading consumes the existing
frame-clock presentation delta for the fade and services the same GameAudio
owner; it neither creates a clock nor advances world simulation. Native
rebuild and the original-content integration corpus pass. The surface repair
admits the missing original system image and consumes purchased child
localization. The corpus exercises menu entry, child returns and mission
handoff; it remains controlled audio/render observation, not a listening test.

## Earlier root/audio snapshot verification

Before the entire-menu expansion, the repository gate passed: pinned formatting, native build, all six
CTest suites, all 228 fatal clang-tidy commands, five repository-policy cases
and the full proposed text-only content check. The original-root startup corpus
also passes with purchased menu/text/audio inputs, native profile storage,
menu/costume/mission handoff, and zero live resource-domain allocations after
teardown. Its renderer/audio observations remain controlled fixtures. Separate
real FACT/FAudio disk-output evidence is described in the audio report. The
user's native visual/audio walkthrough remains the acceptance boundary; no
commit or launch is implied by these checks. Logs remain ignored under
`research/evidence/reconstruction/frontend-audio/`.
