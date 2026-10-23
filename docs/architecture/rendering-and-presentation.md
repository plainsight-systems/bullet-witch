# Rendering and presentation architecture

The independent movie transport completes both company movies on the Mac.
Its [design record](../video-player-redesign.md),
[reference findings](../../research/engine/subsystems/media-reference-design.md)
and [native verification](../native-startup-host.md#recorded-native-verification)
separate the implementation from its measured assurance.

**Target architecture · working baseline · October 22, 2026.**
[Engine architecture](logical-overview.md) · [World model](world-and-simulation.md).

The renderer transforms scene observations and presentation requests into ordered
device work. It owns view/visibility processing, material and shader binding,
render-pass construction, command storage and device-facing lifetime. Game code
owns camera behavior and screen choices; a graphics adapter owns native API calls.
The distinction permits a platform port without replacing gameplay or losing
original draw semantics in an overly small device abstraction.

## Shared contracts and native implementations

The chosen platform split follows DoLittle: one portable contract header for a
subsystem, shared implementation for behavior that does not benefit from a
target-specific algorithm, and native implementations where platform services or
silicon change the work. Common headers describe operations, ownership, clocks,
completion and failures; they do not expose Metal, Windows or codec-library types.
Composition selects implementations. A common contract does not require every
backend to issue the same sequence of low-level API calls.

Screen sequencing and recovered fade progression belong to shared game code.
Decoding, buffering, audio/video clocks and timestamp selection belong to the
reusable engine media player. Native backends own the window, input acquisition,
audio device and graphics submission. The first Mac presentation implementation
uses Metal. Xbox device code remains future work; a Mac result does not verify it.
Software decoding of the original movie formats can be shared while its output
is presented through native device APIs. No asset transcoding is required by this
boundary.

Shaders are first-class source assets in each native graphics backend. They are
compiled by the build, with explicit entry points and CPU/shader binding contracts;
host implementation files do not embed shader strings. The Mac movie pipeline
uses `src/platform/macos/shaders/movie.metal`, compiled into `movie.metallib` and
loaded relative to the executable. This keeps shader changes independently
reviewable and makes native GPU profiling and target-specific tuning possible.
Shared material semantics can have different native shader implementations;
shared headers must not force a lowest-common-denominator shader language or
layout. Optimize from measured GPU/CPU timings, bandwidth and synchronization
costs while preserving the faithful baseline.

## Rendering structure

```mermaid
flowchart TD
    World["World: stage, actors, model poses and effects"] --> Scene["RenderScene: renderable membership and instance state"]
    Camera["Game camera controller"] --> View["View description: transforms, projection, viewport"]
    Scene --> Visibility["Visibility and group selection"]
    View --> Visibility
    Environment["Lights, environment and cubemap bindings"] --> Material["Materials, shader variants and parameter binding"]
    Assets["Mesh, texture and shader definitions"] --> Material
    Visibility --> Passes["Render-pass construction and ordered draw contributions"]
    Material --> Passes
    UI["UI scene: sprites and text"] --> Passes
    Video["Retained decoded video sample"] --> Passes
    Passes --> Commands["Command encoding and retained submission storage"]
    Commands --> Consumer["Render consumer"]
    Consumer --> Device["Selected graphics device adapter"]
    Device --> Output["Render targets and presentation"]
    Device --> Completion["Device completion / resource retirement"]
    Consumer --> CPU["CPU command-consumption completion"]
```

Arrows are data dependencies. Passes determine actual ordering; the drawing does
not claim UI/video always run last or that a particular lighting/postprocess
sequence has been recovered. CPU consumption and device retirement are separate
completion outputs. Exact camera, full visibility, lighting and pass behavior
remain unresolved within the assigned units below.

## Units and contracts

| Unit | Owns | Input → output | Boundary |
|---|---|---|---|
| RenderScene | Renderable registrations and renderer-facing instance state | Stage/model/effect updates → visible-candidate membership and stable draw inputs | Does not own gameplay actors or advance their AI/animation |
| View processing | View/projection/viewport representation and derived camera data | Camera request → view consumed by culling and shader binding | Game camera controller owns follow/aim/cutscene policy |
| Visibility | View-specific selection, masks/groups and required ordering information | Bounds/volumes/groups + view → eligible contributions | Does not change simulation eligibility; exact original rules must be recovered |
| Materials/shaders | Definition/instance distinction, variant selection and parameter interpretation | Material/texture/environment/pose inputs → complete required shader/state bindings | Native shader objects stay in the device implementation |
| Pass construction | Render-target usage, pass dependencies and contribution order | Eligible contributions + environment/output requirements → ordered pass work | No guessed forward/deferred/PBR pipeline or automatic draw reordering |
| Command encoding | Typed command families, storage and input retention | Pass work → submit-ready command packet | Includes model, constants, particles, sprites/text and video families |
| Render consumer | Consumption order and execution of submitted packet | Commands → device operations and CPU completion | Does not read a changing world or resolve arbitrary game names |
| Device adapter | Native resources, pipeline objects, uploads, synchronization and presentation | Device contract → API work/results | Implements required semantics; cannot invent game/material meaning |

These units need not each be a library or virtual interface. For example,
visibility helpers can be internal to scene preparation. The boundary matters
because changes to camera policy, shader equations, scene membership and API
resource lifetime have different callers and correctness criteria.

## Scene extraction and cameras

World/system producers publish renderable instance changes at designated phases.
A renderable includes stable identity, mesh/group selection, world transform,
required pose/deformation data, material overrides, bounds and its resource uses.
Stage geometry can be represented without an actor dispatcher. Effects can submit
model or sprite/triangle contributions through the same lifetime boundary.

RenderScene is a projection of authoritative state. Synchronous preparation can
borrow a stable pose; asynchronous consumption requires a copy or retained version
that will not be mutated. This is a per-submission contract, not a requirement to
copy the whole world every frame. Deregistration prevents new extraction, while
old packets retain what they already use.

A view description records camera transforms, projection, viewport and the
coordinate/depth conventions consumed downstream. The game camera controller
produces it from gameplay state. Camera equations, clipping conventions and the
complete interaction with stage visibility remain open. Their ownership is
explicit so these questions cannot disappear into either game update or an API
adapter. No modern camera smoothing or interpolation is assumed.

## Mesh, material, lighting and pass semantics

The recovered model path constructs commands with model/mesh references and
copied state. Execution checks group/readiness/deformation gates, then interprets
nested CNKH/DGRP/DVOL/declaration/stream/index/draw and related state records.
This provides a concrete translation boundary: preserve record meaning in typed
render operations, including inherited state, masks and alternate draw paths.
Container parsing does not issue device calls.

Materials retain shader-variant identity, texture roles, constant inputs and
instance overrides. The native registry maps 766 names to 490 cached shader
bytecodes; that closes registry selection, not the complete meaning of constants,
lighting, skinning or environment inputs. Texture roles, matrix/palette layout,
precision and blend/depth/cull state require faithful contracts before translation.
Do not collapse those definitions into a generic PBR material and call it a port.

Lighting/environment state has a renderer-owned representation with world/game
producers. Known stage cubemap requests belong here as bindings. Their presence
does not establish a probe-selection algorithm, deferred lighting, shadow method,
light list format or complete postprocess graph. The architecture assigns those
questions to material/pass preparation and keeps them visible in review.

Pass construction explicitly names attachments, required inputs/outputs, resource
uses and ordering. A future implementation may represent those dependencies with
simple pass records; the design does not mandate a general render-graph framework.
Original state inheritance and transparent/effect/UI ordering constrain batching.
Optimizations must demonstrate equivalent results before reordering contributions.

## Submission and device resources

The producer seals a packet with command storage and all referenced immutable or
versioned inputs. The consumer executes its ordered commands, reports CPU
consumption and handles device submission. Completion sufficient for CPU arena
reuse is not necessarily sufficient for destroying textures/buffers used by an
asynchronous backend. The device contract supplies the latter retirement evidence.

The recovered PC path already has a render-list arena, a producer completion wait,
a separate command worker and PresentEx. Preserve its observable task/presentation
relationship when selecting a backend. Ring sizing, buffering count and pacing
are not fixed by a generic “double buffering” assumption. Explicit device resource
uses also govern video samples and particle geometry, not just model buffers.

Resource handlers may request device object creation through the renderer's
resource interface. They retain the originating record and release association;
the generic resource service does not know a D3D/Metal object. CPU definition
availability, upload submission, renderability and release completion remain
separate states where the selected path requires them.

Device loss/reset, unsupported shader translation and failed uploads return
explicit results. They do not publish a usable placeholder for a required object.
The game's visible recovery policy and required retained CPU data must be specified
when those paths are reconstructed.

## UI, audio and video

UI has an engine presentation layer for sprite/font/layout primitives and retained
screen draw state. Concrete title/menu/dialog navigation, transitions and modal
gates belong to session screen tasks. Text/sprite geometry enters render commands;
a successful draw does not complete a screen's script-facing operation.

The audio service owns banks, cues, voice/device operations and playback status.
World audio bindings provide spatial observations; session/screens can play audio
without a world. Cue acceptance, completion, failure and ownership are distinct.
Backend selection must preserve required bank/codec/cue semantics rather than
substituting an unrelated sound effect on missing data.

A movie player owns its playback state and decoded samples through a video
backend. A screen consumes status and chooses how to present the sample; the
renderer retains that sample or its uploaded texture through consumption/device
completion. Decode completion, end-of-stream, screen transition and frame
presentation are different events. AV timing and failure policy remain explicit
media contracts, not inferred from a single “finished” flag.

### General media transport

The media player owns the transport algorithm independently of any screen or
asset name. Game code selects content, requests playback/stop, controls fades
and observes status. It does not decode batches inside its update function or
infer completion from a logo-specific timer.

1. A demux owner routes compressed packets to independently bounded audio and
   video queues. Separate codec workers publish owned video frames and timestamped
   PCM. Video backpressure does not directly block audio decoding. Budget and
   unsupported-interleave failures remain explicit; cancellation joins all owners.
2. Immutable metadata declares stream presence before clock selection. Priming
   requires video readiness and the audio target or true audio end. The main
   presentation owner polls video without waiting for decoding; the native audio
   service consumes PCM independently of rendering and game updates.
3. Audio consumption is the master clock when audio exists. Timestamped PCM is
   delivered continuously; sparse anchors do not insert silence between chunks.
   The native service maps consumed device ranges to their media timestamps.
   Temporary PCM starvation feeds maintenance silence while media time freezes.
   Video-only playback and a video tail after explicit audio drain use monotonic
   elapsed time. Host tick rate does not set media speed.
4. Video selection retains the latest due sample and bounded future lookahead.
   Completion requires both stream ends, actual audio drain and an accepted final
   video presentation lasting its display interval. Cancellation and device failure
   are separate outcomes. Screen completion follows the game's own lifecycle.

```mermaid
flowchart LR
    Game[Game screen policy] --> Player[Shared media player]
    Demux[Demux worker] --> AQ[Bounded audio packets]
    Demux --> VQ[Bounded video packets]
    AQ --> AD[Audio codec worker]
    VQ --> VD[Video codec worker]
    AD --> PCM[Timestamped PCM ring]
    VD --> Frames[Bounded video frames]
    Frames --> Player
    PCM --> Audio[Native audio service]
    Player -->|prepare / start / stop| Audio
    Audio -->|consumed media clock / drain| Player
    Player --> GPU[Native frame upload and presentation]
    Player -->|status| Game
```

Arrows show requests/data/observations, not a fixed frame schedule. Decoder workers
have no Lua, screen or device callbacks. The shared algorithm and portable sample
contracts can serve logos, advertise movies and other pre-recorded sequences;
codec implementations and output devices remain replaceable. The first native
implementation lives under `src/platform/macos/`, selected by the Apple build branch.
See [media redesign](../video-player-redesign.md) for budgets and reference choices.

Movie-file playback is distinct from in-engine cinematics. Scripted cinematic
cameras, animated actors and scene effects belong to the world, animation and
rendering contracts; they do not become decoded BGRA video. A sequence that
actually uses a pre-recorded asset can request media playback, while the game
owns its transition and input policy. The current FFmpeg path is a first shared
software decoder, and native Mac or other target decoder choices can be revisited
without changing cinematic/game semantics.

## Evidence boundary

| Established input | What remains open |
|---|---|
| [Model records and draw path](../../research/engine/subsystems/rendering.md) | Full visibility/pass structure, all command semantics, coordinate conventions |
| [Model data](../../research/engine/subsystems/model-data.md) and [animation](../../research/engine/subsystems/animation-data.md) | Complete skinning/morph/material and numerical interpretation |
| [Stage requests](../../research/engine/subsystems/mission-and-stage-loading.md) | Area/visibility topology, cameras and complete environment/lighting behavior |
| [Particle GPU path](../../research/engine/subsystems/particle-runtime-contracts.md) | Remaining numeric/state branches and unusual allocation/reuse paths |
| [UI](../../research/engine/subsystems/ui-and-screen-tasks.md), [audio/video](../../research/engine/subsystems/audio-and-movies.md) | All screen/media failure, timing, layout and reuse branches |

The implemented Mac movie path selects Metal and a small independent shader
pipeline. World render passes, original material-shader translation and visual
fidelity remain separate work. See [native startup](../native-startup-host.md)
for the implemented subset and verification results.

## Implemented sprite and bitmap-text subset

`render::SpriteOutput` owns opaque texture identities and copies up to 511
ordered quads on the original 1280×720 canvas. Native Metal retains uploaded
RGBA textures, selects straight-alpha color or a packed glyph coverage channel,
and composites movie then sprites under the shared fade. Texture release and
command replacement respect GPU completion. Bounds are explicit: 128 textures
and 128 MiB native sprite residency. These are current backend limits, not the
finished memory subsystem.

The title game adapter supplies recovered sprite positions, animation opacity
and glyph metrics. Its retained encoded archive leases, decoded images and
native textures have separate lifetimes/readiness. Shared PNG/FONT decoders
perform no file I/O or GPU work. The original native BGRA font flags map into
canonical RGBA channels before commands reach the renderer. See the
[sprite design](../../research/engine/subsystems/sprite-presentation-design.md)
for inspected reference implementations, ordering evidence and shader equations.
