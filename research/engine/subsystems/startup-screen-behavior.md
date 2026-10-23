# Opening cinematic and title behavior

October 22, 2026. Static reconstruction from the already unpacked PC executable
with SHA-256 `bdaff1746d3092215699b09eab11315c6adf8fb71eaa8f46c280b11b3cd448fd`.
The production media design remains the established
[Unreal/FFplay comparison](media-reference-design.md); these state machines
are game policy layered over that implementation, not a new movie algorithm.
Native acceptance is recorded separately by the implementation task.

## Method and durable evidence

Read existing Ghidra decompilation and instruction exports in engine passes
06r, 06s and 06w; interpret x86 call arguments and component offsets together.
Read constant/table bytes through the unpacked PE section map using Python
`struct.unpack_from`. Reproduce with `python3 research/tools/startup_screen_constants.py`. Saved raw constants in
`research/evidence/reconstruction/startup-title/screen-constants.json`.
The raw exports and executable stay ignored; this note records conclusions.
No files were written in the game installation.

## Opening cinematic

`0x49e610` opens `data/movie/advertise.ssu` during activation. A false open is
stopped and leaves the movie-available bit unset. `0x49e900` performs:

1. State 0 enables visibility and begins a 0.25-second reveal; state 1 waits
   for the shared fade to finish, then enters state 500.
2. State 500 accepts pressed action masks `0x1010` or `0x2020`. With a movie,
   it also accepts movie completion. Without one, its phase timer must reach
   the float at `0x6c13a4`, **3 seconds**.
3. State 8000 starts a 0.25-second conceal; 8001 waits for the fade; 8002
   hides; 8003 publishes result 1, stops the movie and requests task removal.

Each update executes one branch and then adds game delta. State changes reset
that phase clock before delta is added. Unlike company-logo open failure,
this fallback is unambiguous and is preserved. A missing implementation or
thrown transport error still fails explicitly rather than using this fallback.

## Title lifetime and outcomes

`0x499ae0` opens `data/movie/witchLOGO.ssu` before update initialization gates.
`0x499a10` requests `sprite.title`, waits its group, selects one of ten loading
resources, then waits that separate sprite group. The table at `0x9595e0` has
12-byte entries, not an array of adjacent pointers. Until this initialization
returns ready, `0x499f10` returns before advancing animation or phase clocks.
Visibility remains disabled; the movie service has already been started. The
movie's transport clock belongs to the media implementation and is not reset by
readiness. The concrete transport runs independently of screen visibility.

After readiness, state 1 enables visibility, initializes animation and begins
one-second reveal. State 2 waits fade completion, starts the two prompt pulses
and the details ramp, then enters state 100. State 100 accepts pressed `0x1010`
(outcome 1 path). Otherwise it waits **16.5 seconds** with a movie (float at
`0x95a6a4`), or **8 seconds** without one (`0x6c1ff8`). Movie completion does
not itself bypass this timer.

The timeout path (2000) targets common overlay opacity to zero. State 2001
waits movie completion if available, then conceals with duration zero. Without
a movie it conceals over 0.5 seconds. State 2002 waits fade completion and
selects outcome 2. Cleanup at 10000 hides, stops the movie, and publishes status;
10001 requests removal. These are distinct lifecycle boundaries.

The accept path (500) restarts the prompt animations and invokes the original
sound helper with ID 11. At 0.6 seconds in state 501 (`0x82cfa0`) common overlay
opacity targets zero; at 0.9 seconds (`0x6c1d88`) conceal begins over 0.5 seconds.
State 502 waits fade, 1000 selects outcome 1, 1001 advances, and 1002 routes to
cleanup. The new flow preserves this timing; the ID-11 sound cue is not yet
implemented by its media-only capability and remains an explicit limitation.

## Title drawing and animation

`0x499d00` submits the following definitions through `0x564680`. Coordinates
are on the original 1280 by 720 presentation canvas. Crop values are all zero
(default whole texture), scale is (1,1), angle zero, color white and relative
sort key zero. Sprite numeric IDs remain compatibility metadata.

| Definition | Sprite ID | Position | Opacity |
| --- | --- | --- | --- |
| `0x95a6a8` | 424 | (350,505) | highlight pulse × common |
| `0x95a6d8` | 423 | (350,505) | prompt pulse × common |
| `0x95a708` | 452 | (412,632) | details × common |

Then the movie helper is called, followed by `Version 1.0.5` at (10,700),
font selector `0x10`, with the same opacity as sprite 452. The movie render command is queued in title draw; sprite registration is
published later by **TaskOverlay**, whose create function `0x4ac950` copies the
name at `0x831eb8` and writes priority `0x2002`. Its vtable at `0x82bd50` has
draw slot `+0x1c = 0x4ac9c0`, which publishes through `0x569170`. Title has
priority `0x2000`; the already mapped ascending task order therefore puts the
movie before the overlay publication. Equal sprite sort keys preserve insertion
order (`0x569620`). Default blend indices 4 and 5 map through table `0x841050`
to D3D source-alpha and inverse-source-alpha. This supports movie first, then
the ordered title sprites and text with normal straight-alpha blending. The
overlay constructor instructions are saved as `overlay-priority.txt` beside
the constants.

The linear helper `0x572660` moves common opacity at 2 units/second and details
at 1 unit/second, clamped at the target. Initial common is 1; initial details
is 0. The pulse helper `0x5724a0` evaluates the previous phase time before adding
delta, using `0.5 + 0.5*sin(2*pi*clamp(t,0,period)/period)`, then wraps phase
with `fmod`. The two pulses start paused at phase `0x3f799999` (approximately
0.975), period `0x3fa66666` (approximately 1.3), and zero output. Once reveal
ends both resume. Accept restarts prompt at phase zero with 0.1-second period;
highlight uses the helper's constant 1→1 range. Version/details ramp starts
only after reveal. These values are taken from machine constants, not tuned
against the reconstructed output.

## Ownership and verification boundary

`AdvertiseFlow` and `TitleFlow` exclusively borrow the shared movie/fade
capability, own one operation's cleanup, and return presentation values by
value. Session owns title/loading resource readiness and task lifecycle;
renderer owns textures, draw submission and device retirement. Both flows are
single-threaded game logic. Invalid delta is rejected before mutation; media
exceptions stop and fail. Per-update policy is fixed work with no normal heap
allocation. Their in-process tests establish state/outcome/animation contracts,
not native A/V acceptance or original-game visual equivalence.
