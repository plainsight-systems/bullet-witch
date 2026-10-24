# Loading presentation and responsiveness

The original mission script activates `_ExecLoadingScene(5)`. The current native
route retained that state without drawing it, then constructed assets on the
main thread after the last black front-end frame. This explains the invisible,
unresponsive interval; it is not a physics or controller delay.

## Measured load phases

An ignored native probe uses the production cm01_00 stage, background, actor,
Metal resource owners and stage-view publication. Warm Debug measurements before
this change: catalogs 144 ms, background 776 ms, sky 115 ms, stage 4707 ms,
player 326 ms, world publication 136 ms, first render 43 ms. A separate breakdown
measured stage CPU definition loading at 486 ms (18 models, 179 textures), PNG
decoding at 3541 ms for 223,297,536 decoded bytes, and native texture upload at
23 ms. Timing is diagnostic wall time on the development machine, not a universal
load guarantee. Probe source, commands and logs are under
`research/evidence/reconstruction/traversal/load*`.

## Production reference and design

Inspected Doom 3 revision `a9c49da5afb18201d31e3f0a429a037e56ce2b9a`,
`neo/framework/Session.cpp`: `ExecuteMapChange`, `LoadLoadingGui`, and
`PacifierUpdate`. It publishes the loading GUI before synchronous map work,
pumps platform events and redraws it at bounded intervals between load operations,
and prevents recursively entering the loading renderer. This fits the existing
single-owner native resource interface. No renderer or Lua object becomes
implicitly thread-safe, and no arbitrary job framework is needed for this fix.

Adopt an explicit caller-owned progress callback at stable asset boundaries.
It can pump platform events and draw only the loading presentation; it must not
advance scripts, replace catalogs, cancel the owner in place, or inspect a partial
world. A close request is latched by composition and handled after the current
operation unwinds. Drawing is throttled by monotonic time, with an initial frame
before heavy work. Texture pixels still decode/upload once and remain resident.

The measured PNG cost comes from compiling pinned third-party codec internals
without optimization in the host Debug build. Compile only those dependency
translation units with normal optimization while retaining symbols and the
project's Debug checks. Preserve the decoder API and output bytes; validate this
against the same asset corpus and native probe. This is a build correction, not a
replacement texture format or a claim that source loading took the same time.

Original presentation source anchors: initializer `0x4a6b40`, update `0x4a7470`,
draw `0x4a6c00`. Item 5 indexes loading text; it is not the background selector.
The title chooses one of ten loading groups and its sprite 332. System sprites
447/448 supply the original animated motif. Detailed reconstruction follows below.

C++ design consultation: Core I.1 (`#ri-explicit`) for the visible callback
contract and CP.31 (`#rconc-data-by-value`) when considering thread handoff;
performance CDSA.32 (`guidelines/cpu-dsa/CDSA.32-transform-static-data-into-the-consumer-layout.md`)
for load-time transforms, GPU.1 (`guidelines/gpu/GPU.1-keep-data-on-device.md`)
for retaining uploads. The design deliberately keeps all native calls on their
existing main-thread owner.

## Source presentation reconstruction

Further bounded native disassembly corrects the old description of a spinner:
this is six groups of sliding repeated art. `0x4a69a0` takes a repeat count,
not an anchor flag. Sprite 448 is submitted 11, 7, 5 and 8 times; sprite 447 is
submitted 7 and 7 times. Its position is top-left and each sprite rotates around
its own center; the last two 448 groups rotate 20 degrees. The spacing direction
uses `(sin(angle), cos(angle))`. First two groups use 90 degrees, next two 250,
and last two zero. Art slides into place through quintic smoothstep; the last
two channels use the authored `3.75*q*q - 2.75*q*q*q` overshoot without clamping
its output. Hash-bound constants and disassembly are retained under
`research/evidence/reconstruction/loading/`.

`0x474970` draws the title-selected image at its authored per-image position,
with its native dimensions. It is not a stretched full-screen background.
`0x412110` loads category 1 from `data/menu/9951`; entries are six language
columns with file-relative string offsets. Its actual row count is 18, so this
file does not contain all indexes admitted by the broader script API. The
current English host selects column 1, preserving actual item 5’s Ancient Wall
tip. Missing rows fail explicitly. Text starts at (540, 152), with size 28 and a line gap of 4.
Source formatting uses colors and inline control images; these are retained
rather than displaying the encoded markup. Native fade duration is 0.2 seconds.

Optimized-codec measurement: PNG decode 1,629 ms, stage 3,033 ms, background 424 ms,
player 199 ms. All 179 decoded-texture hashes (223,297,536 bytes) agree with the
unoptimized pinned Debug decoder, also tested with its UBSan-instrumented build.
The hash pass is outside individual decode/upload timers. These measurements
support a narrow build change; loading still pumps and draws between assets.
Implementation consultation retrieved Core E.6 (`#re-raii`) and performance
GPU.1 again: callbacks cannot leak partial GPU owners, and loading frames reuse
resident textures without redecoding.

## Native loading review

The ordinary fullscreen menu route was observed while the user navigated.
`research/evidence/reconstruction/loading/native-review/frame-035.png` shows
the original animated motif, selected portrait, Ancient Wall tip and controller
LB/RB/B images. Frame 045 shows the loaded stage with costume 5; frames 055 and
070 show subsequent movement/jumping and grounded user control. Capture timestamps
are retained in `capture-times.log`; they are capture-session times, not measured
mission-load durations. The parent agent viewed these captures.

This run preceded the final correction that applies the original 0.2-second
black fade to the composed frame instead of multiplying individual sprite alpha.
Its stable loading-screen appearance remains applicable; the first 0.2 seconds
of that final correction are source/corpus verified, not separately GUI reviewed.
The combined build, six host tests, pinned fatal clang-tidy and repository policy
checks pass. User review subsequently reopened jump timing and floor penetration;
visible loading does not establish acceptance of those separate behaviors.
