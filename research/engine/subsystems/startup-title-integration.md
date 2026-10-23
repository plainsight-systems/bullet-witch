# Opening cinematic and title integration

October 22, 2026. Implementation design for the authorized next startup slice.
The original root still owns screen order. Recovered game flow contracts live
in `startup-screen-behavior.md`; image/font evidence in `title-assets.md`; native
sprite presentation and pinned renderer references in `sprite-presentation-design.md`.
These are source reconstruction changes, not replacement startup scripts.

## Ownership and readiness

The startup session owns advertise/title policies and lifecycle tasks. A borrowed
TitlePresentation capability supplies resource request/readiness and draw data;
its concrete game adapter owns title/font/selected-loading resource interests and
renderer texture handles. ResourceService continues to publish immutable encoded
payloads. A lease retains its whole archive and can expose sibling records because
original DDS image records have no independent catalog publication names. It does
not imply that image decoding or GPU construction has finished. Title readiness
requires those separate construction steps; failures remain failures.

Only USER and encoded DDS records needed by these archives are accepted. USER's
existing inline-reference restriction stays intact. DDS selected byte ranges are
already validated by the archive parser; the image decoder must additionally
validate the selected image. Unknown executable handlers still fail. Native
0x499a10 waits `sprite.title` followed by the selected loading group; the font
resource is a renderer prerequisite. Their actual construction data comes from
the purchased installation and remains read-only.

The game adapter constructs persistent renderer textures once per load, then
supplies bounded ordered sprite commands from recovered definitions and font
metrics. The renderer owns native storage and completion. Game screen policy
cannot call Metal, and the native renderer cannot know title filenames or script
outcomes. Shared PNG decoding uses a pinned established library. Media transport
continues through the existing independently clocked video player.

## Verification intent

Extend existing in-process contracts for resource retention, screen lifecycle,
readiness, input/timeout results and cleanup. Keep the existing original-root
integration scenario, extending its production route instead of adding another
scenario. Native verification must play advertise and title using real assets,
show original overlays/text and observe the natural completed title route;
interactive confirmation should reach the explicit character-load boundary.
The menu/profile implementation and full memory subsystem remain later work.

## Delivered boundaries and observations

Advertise policy and its native replay completed first: both logos plus the
opening movie reached the then-unimplemented title boundary in 130.551 seconds.
Advertise consumed 5,899,200 PCM frames, zero underruns and no inserted silence;
its terminal frame was presented. `advertise-native.log` retains the run.

The first full title run completed all four movie transports in 152.153 seconds
with outcome 2 and zero audio underruns, but the user identified garbled version
text. This was a real visual failure: native FONT flags use a BGRA-oriented
channel convention. Canonical PNG channels require 1→blue, 2→green, 4→red,
8→alpha. Pixel-for-pixel PNG decoder checks did not validate that semantic
association. The FONT parser and explicit four-channel tests were corrected;
original glyph shapes from all four channels were independently inspected.
`title-native.log` remains the failed visual run; the corrected run is recorded
separately. No native shader swizzle or substitute font was added.

The integration scenario now constructs the original title/font/loading assets
and emits actual game draw commands through a synthetic renderer while driving
the production session to character load with controlled media observations.
Native Metal/audio acceptance is separate. The resource lease retains bounded
encoded sibling bytes after unpublication; decoded images and renderer handles
have separate ownership. A title resource failure cannot satisfy readiness.

Native fullscreen is now the default. AppKit completion is a startup gate,
independent of media clocks. `--windowed` and requested content-point dimensions
provide a development path. These settings never change the 1280×720 game canvas.

Remaining explicit limits: character-load/profile/menu work, original title
confirmation cue 11, startup-check artwork and original-game parity comparison.
Flows preserve the recovered ordinary open-false fallback, while transport or
device exceptions remain terminal; the current FFmpeg adapter throws on open
errors rather than silently treating unsupported playback as an ordinary miss.
The full memory subsystem remains separate work.

## Corrected acceptance

The corrected fullscreen replay completed the original logo/check/advertise/title
route in 151.036 seconds and published title outcome 2. All four movies consumed
their full PCM output with zero underruns, no inserted silence and terminal
frames presented. Title consumed all 958,399 PCM frames; eight late video frames
were dropped in this observed run, so this is not a frame-perfect pacing claim.
A native CUA screenshot visibly confirmed readable `Version 1.0.5`, the original
START prompt and copyright overlay over the title movie. That screenshot is
retained in the task conversation; generated playback counters are in
`research/evidence/reconstruction/startup-title/title-corrected-native.log`.

Strict compilation, formatting, all four CTest entries, fatal clang-tidy across
38 authored translation units, content guards, original-root integration and
UBSan pass. The 14 original PNG decoder comparisons and four-channel FONT
checks are recorded in `title-assets.md`. These are distinct evidence layers;
passing transport checks did not excuse the first run's incorrect glyphs.
