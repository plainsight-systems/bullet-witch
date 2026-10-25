# World timing verification

This records the scope of step 2, not complete gameplay or rendering parity.
The [source trace](world-timing-source.md), [pinned reference inspection](world-timing-reference.md)
and [architecture](../../../docs/architecture/world-timing.md) separate recovered
behavior from interface and safety choices. No game data or generated captures
belong to this authored report.

## User acceptance

The user accepted the timing work after an interactive walkthrough, reporting
that the timing felt right. This records acceptance of the timing slice and its
ownership corrections; no individual control sequence was supplied. It does not
establish lighting, complete gameplay or the remaining script/world integration
boundaries as accepted.

## Automated contracts

Nine in-process cases exercise presentation clamping before world scale,
normal/control time accumulation, immutable observations, pacing and loading
rebase, invalid-state preservation, independent pause/input gates, nested script
snapshot restoration, neutral input/UI edge consumption and the source input
repeat bank. Existing menu fixtures borrow the same pause owner.

The required gate is `python3 scripts/check.py all`: pinned formatting, full
build, six CTest targets, fatal clang-tidy, repository policy tests and content
checks. The final run after input/exposure wiring passed. Logs remain
under ignored `research/evidence/reconstruction/world-timing/`.

## Original-content corpus

The existing command is:

```sh
build/host-debug/startup-corpus "$GAME_DIRECTORY"
```

It loads the purchased original data/Lua and retains the existing mission,
traversal, animation, event, audio-owner and allocation assertions with a
controlled renderer. Every retained Stage now borrows an engine WorldClock and
MissionControl. New assertions reject foreign/replayed frames and ensure paused
world time stays unchanged. The final ownership-corrected run passed in
2,193.133 seconds (36 minutes 33 seconds), consistent with the previous
2,160.885-second run. It passed all four
live weapon classes, lower-to-melee interruptions, damage/magic/death exits,
profile reload and teardown with zero live tracked CPU memory. This is
test-fixture cost, not game FPS.

The batching-equivalence fixture now starts both scenes from the original float
world interval. It splits a frame into independently published intervals only
when every actor subdivision is exactly representable as a float. Otherwise both
scenes receive the identical whole frame. All 120 outer frames, elapsed choices,
and existing cursor, phase, binding, pose, velocity, audio-event and GPU-publication
assertions remain. Both partition paths are required to execute.

The native host already supplied float intervals; the former direct-Stage test
API could bypass that boundary with doubles. This correction does not change
the production actor subdivision algorithm.

This is necessary because a balanced subdivision of a 50 ms float interval can
need more than float precision. Republishing its two final pieces independently
changed total time by 1.862645 ns; a later float contact/velocity calculation
showed a 0.000028 velocity difference. These were different inputs, not evidence
for changing the production actor algorithm or progressively widening tolerances.
The original cursor allowance remains 1 ns. Existing separate 50 ms landing
cancellation and locomotion checks retain their original coverage.

## Native checks completed

The existing offscreen Stage/Metal witness was adapted without changing its
production resource, actor, collision, renderer or audio ownership. It verified:

- user pause froze actor position, full pose, clip state, material/environment
  publication and audio updates; resume restarted them;
- input-only pause neutralized gameplay commands while world and idle animation
  advanced; retained material curves followed admitted world time;
- duplicate and foreign frame observations were rejected;
- before-pause and paused captures were byte-identical, and resumed output changed.

Its independent environment instance checks timing/publication against retained
production sampling, not an independent proof of the lighting equations. The
witness uses real FACT ownership with a dummy audio device, so it makes no
listening claim. Offscreen rendering is not a sustained FPS or fullscreen claim.

The HDR witness additionally exercised production renderer forwarding and a
read-only observation patch on a copy of the current postprocess implementation.
With world time paused at zero, a zero presentation interval left exposure
history empty; explicit 60 Hz and 30 Hz intervals filled the 64-entry history.
Changing world scale did not change presentation adaptation. The patch, 87
observations, commands and hashes are retained under ignored `world-timing/native/`.
No exposure equation, authored lighting value or history policy was tuned.

The ordinary native `--verify-startup --windowed` route completed four movies,
including the intro, with every source PCM frame played, zero underruns and final
frames presented. It reached the title route in approximately 152 seconds.
A fullscreen attempt failed before timing initialization with the already recorded
native activation rejection. That platform issue remains separate and open.

The final repeat-bank wiring also passed the native windowed startup route in
152.161 seconds with all four movies, complete PCM playback and zero underruns.
Neither this evidence nor the corpus implements full Lua script-pause side effects,
paused hierarchy/physics consumers, gameplay HUD or mission outcomes.

## Ownership correction after review

Review found excess mutable access despite the correct current call paths:
Stage retained a mutable MissionControl reference for a test-only convenience
method, and PauseFlow received script/input capabilities it never needed.
FrameClock and MissionControl also permitted implicit copies/moves. Private
fields and guideline citations did not enforce the intended authority boundary.

Stage now borrows const control observations; its pause mutation method is
removed. The existing corpus requests pause through its owning control and
performs the same audio synchronization. PauseFlow receives a concrete
UserPauseControl value with only user-pause observation/request. FrameClock and
MissionControl reject copying and moving. Framework presentation/loading state
now outlives the mission services retaining its callbacks by declaration order. WorldFrame explicitly borrows its
issuing clock lifetime; current synchronous use does not need a generation
registry. No frame may survive mission-owner destruction/reconstruction.

Both MCP corpora were queried again in design and implementation. Core I.4 and
Con.2 translate into restricted signatures; performance COPY.5 translates into
nonduplicable authority, with moves additionally forbidden for stable borrowed
identity. The small menu handle intentionally remains copyable: copies alias one
owner, do not copy pause flags, and expose no owner reference. Static assertions
reject widened Stage/menu mutation, full-control menu construction and movable or
copyable authorities. A new behavioral case verifies handle aliasing and denial
while source script policy excludes user pause. This is guidance applied to APIs
and verification, not a claim that the MCP corpora analyze our code.

The corrected full gate passes, including build, six CTest targets, fatal lint
and policy, including the final framework lifetime reorder. The ordinary
windowed startup verification also passed after that reorder in 152.196 seconds:
all four movies, complete source PCM playback, zero underruns and final frames
presented. The rebuilt offscreen native timing witness passes in 7.716 seconds
with unchanged assertions and matching paused capture hashes. The existing
original-content corpus passed in 2,193.133 seconds, including final action exits,
profile reload and zero live tracked CPU memory after teardown. All prior
assertions are retained. Final formatting, diff and content policy checks pass.

A separate source cadence check confirms user-pause suppression of both known
AREA paths, but script pause alone can leave the root producer active. This is
an explicit obligation for genuine script-pause/root integration; the witness
does not prove original environmental behavior for that unimplemented path.
