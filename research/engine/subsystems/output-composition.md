# Final output composition

October 26, 2026. Implemented as the output portion of
[reorganization slice 2](lighting-output-integration.md). Native walkthrough
acceptance was recorded on October 26 and remains distinct from the automated
evidence below.

## Evidence and decision

The accepted `StartupHost::draw` implementation owned final output order as
well as window/input duties. Its existing algorithm was: fit the world/UI
logical canvas; prepare world HDR/postprocess; encode world display; reopen the
color pass with load; aspect-fit movie; encode ordered alpha/additive UI; present,
commit and wait for completion. The source-derived world pipeline and reset
semantics are recorded in [world display output](world-display-output.md) and
[output lifecycle](output-lifecycle-redesign.md). This slice preserves that
accepted reconstruction; it does not claim a newly recovered Cavia class layout.

Before implementation, inspected pinned Doom 3 commit
`a9c49da5afb18201d31e3f0a429a037e56ce2b9a`,
[`neo/framework/Session.cpp`](https://github.com/id-Software/DOOM-3/blob/a9c49da5afb18201d31e3f0a429a037e56ce2b9a/neo/framework/Session.cpp):
`idSessionLocal::Draw` selects the game view or GUI, then draws the wipe and
final console overlay; `UpdateScreen` brackets drawing with `BeginFrame` and
`EndFrame`. This supports an explicit final output boundary above sibling
producers. We retain Bullet Witch's current world/movie/UI order and opacity,
not Doom's concrete GUI policy, globals or backend scheduling. No reference
implementation code was copied. The inspected snapshot and pre-implementation
ownership decision are ignored under
`research/evidence/architecture/output-composition/`.

## Owners and authority

- `render::OutputFrame` is a fixed-size value containing final opacity and an
  optional engine presentation-timing snapshot. World output requires timing;
  movie/UI-only output can run without world timing. It owns no producer state.
- `render::OutputCompositor` exposes final presentation only. It grants no
  upload, scene membership, lighting mutation, audio or clock authority.
- Native `WorldRenderer` retains world geometry, materials, scene output and
  `WorldPostprocess`, with its existing lifecycle and luminance readback.
- Native `MovieRenderer` owns its retained decoded-frame texture and movie
  pipeline. Changed-size upload publishes a complete candidate; unchanged-size
  upload reuses completed storage. It only encodes its own aspect-fitted layer.
- Native `SpriteRenderer` owns the texture table, residency accounting, fixed
  submitted quad list and both UI blend pipelines. Complete validation/slot
  resolution precedes publication; release removes referencing queued commands.
- Native `OutputCompositor` borrows those three stable owners and retains the
  queue, library and window layer handles. It creates the final native command,
  performs composition/presentation and waits for completion. Native offscreen
  `compose` uses the same submission path with a borrowed BGRA8 target.
- `StartupHost` retains window/input/audio duties and construction of the
  siblings. Existing producer interfaces forward to their owners. It supplies
  drawable size and copied frame input, without a second texture/draw registry.

The host declares the compositor after its producer owners, so it is destroyed
before them. All access remains on the main thread. Siblings are noncopyable and
nonmovable; the compositor exposes no producer accessor. Producer resources must
not be changed during composition. There is no asynchronous work remaining after
successful composition returns. An unavailable drawable returns false before
producer encoding and leaves them intact for retry. Invalid frame/target inputs
reject before encoding. Native execution failures propagate to the host.

World output retains its centered, integer-aligned 1280×720 logical viewport.
Movie aspect fitting remains independent. UI reselects the logical viewport and
preserves submitted order, straight-alpha/additive blending and RGB-only global
fade. UI and movies never enter world HDR, bloom, tone mapping or edge filtering.
No shader, lighting equation, frame clock, media/audio service or gameplay logic
changes. Movie upload remains width × height × 4 bytes. UI keeps the existing
128-texture/128 MiB and 511-quad limits, with no per-frame heap allocation.
Intermediate images remain on the device; no new readback, graph or pipelining
scheme is introduced.

## C++ consultation

Both required corpora were queried independently for header design and concrete
implementation. Full rules retrieved in both phases:

- Core `R.1` (`rr-raii`): native handles and budgeted allocations retain automatic
  release through owner construction/unwinding.
- Core `I.11` (`ri-raw`): native encoder/target and producer references borrow;
  ownership stays with the explicitly constructed stable owners.
- Performance `COPY.5`
  (`guidelines/copy-move/COPY.5-resource-owning-types-are-move-only.md`): copying
  is prohibited; moving is also prohibited because the compositor borrows stable
  producer identities.
- Performance `GPU.1` (`guidelines/gpu/GPU.1-keep-data-on-device.md`): retain
  native textures/intermediate images and preserve the existing completion
  boundary. This reorganization makes no performance improvement claim.

## Verification

Focused native `memory-tests` and `bw-startup` builds passed. The existing
`memory` CTest group passed with the new `macos_output_compositor_test.mm` cases
using actual Metal output and authored fixtures, without a visible window:

- Real world output reaches the target and preserves black logical-canvas bars.
- Movie output covers world with independent aspect fit; UI reselects the
  logical canvas and overlays it in alpha/additive submission order.
- Final fade is applied once to each sibling's RGB.
- Stale UI submissions preserve prior commands; absent drawable presentation
  leaves producer state available for retry.
- Texture release removes queued UI consumers; invalid movie replacement
  preserves the previous uploaded pixels; clearing movie/UI reveals unchanged
  world output.
- World rendering rejects a missing engine timing snapshot; destroying completed
  output owners retires all accounted native resources. Compile-time checks
  reject copying/moving the three new native owners.

The initial fixture omitted explicit supported sampling state and was rejected
by the existing renderer admission contract. The authored fixture now supplies
its intended filters/address modes; no production contract or tolerance changed.

Focused fatal clang-tidy passed all five affected output translation units and
their included headers. Focused logs are under the ignored evidence directory
above. The final whole-repository gate passed; user walkthrough acceptance is
recorded by slice 2 integration. These cases establish native composition/retirement,
not complete visual/audio parity with the game.
