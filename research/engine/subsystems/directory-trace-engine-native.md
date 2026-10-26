# Engine and native directory trace

October 26, 2026. Read-only implementation investigation; proposed destinations
are not approved moves. No production files changed, build/test run, game launch,
commit or independent review was performed. Companion
`directory-trace-engine-native-map.json` accounts for every one of the 189 current
non-game `src` files and `include/bw` files. This covers declaration contracts,
include consumers, runtime composition and relevant concrete implementations;
it is not a fresh numerical algorithm audit.

## Findings that affect the global plan

1. `src/world` contains only `world_clock.cpp`. This is a timing primitive,
   not the game's world. `framework/application.cpp::run_application` constructs
   its mission-scoped instance; `MissionControl` supplies admission and
   `MissionGameplay` sequences consumers. Move the implementation and portable
   declaration into timing. A game world folder must correspond to actual
   mission/entity ownership, not this clock.
2. `src/framework/application.*` is concrete Bullet Witch composition, not a
   reusable engine framework. It directly includes all concrete frontend,
   loading, HUD/pause, input, profile/session, audio and mission owners. Its loop
   paces the frame, samples/maps input, admits mission work, advances media,
   advances mission, services audio, handles transitions, advances session,
   presents UI and draws. Put it in `game/application`, retaining the separate
   engine `FrameClock` and `WorldClock` contracts.
3. Input is genuinely split across the wrong boundary. `StartupHost::pump`
   drains Cocoa events, translates keyboard usages, samples GameController,
   merges physical axes and derives device edges. But it ALSO maps A/B/Menu,
   arrows/dpad/stick threshold into confirm/cancel/start/navigation bits and
   owns `ActionRepeat`. `game::map_controller_input` then adds original UI
   aliases/wheel/defaults and maps saved settings to gameplay intentions.
   `platform::StartupInput` carries both neutral samples and the game's
   evade/reload/attack/weapon/aim/spell intentions. Merely moving files will
   preserve this leak. The proposed contract repair is neutral device/event
   `InputFrame` at the host boundary; game input owns original UI/action mapping,
   repeat policy and binding results. The fixed repeat arithmetic may remain an
   engine input utility. Preserve event/edge/focus/repeat ordering when doing it.
4. `scene` is not a world owner. It is a bucket containing spatial queries,
   environment-channel interpolation, lighting fields and shadow admission.
   Make lighting an identifiable engine family. Put area/collision/environment
   interpolation in spatial; shadow caster selection beside renderer shadow
   projection. None of this transfers gameplay state to the renderer.
5. `content/stage` and `content/character_support` are not neutral asset
   services. Character loading contains original `ea* -> ea0`, `pw*`/`m09 -> pw0`
   policies; stage loading owns original namespace exclusions, constructor
   admission and environment table selection. Put these original-game loaders
   in `game/assets/original`. Keep catalog acquisition/priority/interests/leases
   in engine content resources, and binary schemas explicitly identified as
   original format adapters. `formats/player_camera` interprets game standing,
   targeting and crouching settings; `formats/stage_environment` interprets
   `witch/stage/<name>` settings. Move those concrete settings readers to game.
6. Native code is flat too. Separate window/event, input, audio, files, memory
   and Metal rendering under `platform/macos`. `platform/files/save_storage.cpp`
   uses `unistd`, `mkstemp`, `fsync`: label it POSIX. `files/host.cpp` is standard
   C++ file reading. `platform/media_audio.hpp` declares the concrete macOS
   AudioQueue implementation, not the portable contract; put it with that
   backend. The portable contract already exists as `video::AudioOutput`.
7. Audio has a legitimate mechanism/policy split but duplicate vague folder
   names do not explain it. Engine `CueOutput`/`MusicOutput` define playback,
   gain, listener and pause contracts. FACT owns banks/voices/device and FFmpeg
   prepares samples. The game session owns original bank admission and desired
   cue policy; actor/UI requests belong with those game consumers. Keep one
   native runtime and the existing separate movie AudioQueue, not a second
   system produced by reorganizing filenames.

## Ownership and consumer trace

| Current family, all files covered by map | Actual responsibilities and consumers | Proposed physical owner |
|---|---|---|
| `animation` (7 cpp / 7 headers) | `PreparedClipCatalog` resolves immutable aliases; `PlaybackRing` schedules retained clips; `SkeletonPose` evaluates layers/hierarchy; `EventTimeline` calls supplied event receiver; `ShapePose` deforms prepared shapes; `SecondaryMotion` integrates authored bone constraints; `ScalarAnimation` feeds spatial environment. Consumers are game character animation/motion/facial/secondary owners and spatial Environment. None owns Alicia's action selection. | `engine/animation`, singular portable headers |
| `audio` (6 implementation/private files / 4 public) | FACT `Output` owns cue/bank/voice device lifetime and prepared samples; WMA bridge supplies callbacks. FFmpeg `wave` decodes WMAv2; `music` validates original two-segment container before decoding. `GameAudio` loads banks and services output; `FrontendAudio`, `CharacterAudio`, `MenuAudio` issue policy requests. Target main chooses FACT. | `engine/audio/{backends/fact,codecs/ffmpeg}` and public audio contracts |
| `content` (3 cpp / 3 headers) | `open_archive/open_catalog/archive_path` implement catalog-relative acquisition. StageDefinition and CharacterSupport retain original selected archive/typed asset inputs and game alias policy; mission loading and actor/presentation readers consume them. | access into `engine/content/resources`; stage/character importers into `game/assets/original` |
| `formats` (22 private/implementation / 21 public) | Bounded binary readers and retained schemas: archives/catalog/packages, models/shapes, animation/events/control points, placement/collision/area/animation tables, textures/mips/fonts/images/text, audio banks, customization. Engine mechanisms and game importers consume typed schemas. These are reconstructed original formats, not a claimed universal asset format. Camera and stage settings are the concrete game-semantic exceptions above. | `engine/content/formats`; game settings exceptions in exact map |
| `resources` (2 cpp / 2 headers plus umbrella) | `CatalogSet::resolve` chooses mount priority; `ResourceService::request/pump/acquire/release` owns encoded dependency interests, readiness and immutable leases. Frontend surfaces and game script/session/mission owners request named resources. This is not mission/entity activation. | `engine/content/resources` |
| `memory` (3 cpp / 5 headers) | Domain owns backing/allocation accounting, CoalescingHeap owns reclaiming subheap, DeviceBudget/Charge owns device admission/lifetime accounting. CPU backing virtual contract is implemented by macOS; allocators inject domains into content/script/actors/audio/render/video. | `engine/memory`; target backing under `platform/macos/memory` |
| `math` (9 headers) | Scalar/vector/matrix/quaternion/euler/frustum/half/direction and analytic two-bone operations; consumed by animation, game controller/camera, scene/spatial and render. No actor policy or owners. | singular `include/bw/math`; no empty cpp directory required |
| `render` (6 cpp / 9 headers) | RenderScene controls registration/update/removal/retirement over WorldPresentation, exposing limited ActorPublisher/LightingPublisher. Native WorldRenderer retains draw registry. Material curves/admission, view transforms, shadow projection and exposure control are shared calculations. SpriteOutput/world resource APIs and OutputCompositor define native output boundaries. | `engine/render/{scene,materials,shadows,postprocess,output}` |
| `scene/lighting` (5 cpp / 5 headers) | SceneLighting owns prepared irradiance/reflection/local-light selection. LightingPublication owns packing/distribution; sealed ReceiverPublication owns destination/cached receiver state and borrows owner. Game StagePresentation supplies authored policy/transforms; renderer gets packets. | `engine/lighting` |
| `scene/spatial` (3 cpp / 4 headers) | AreaRegions does first-containing-region queries; StaticCollision prepares retained triangles/BVH and clearance; character_collision does source contact correction through that owner. Mission prepares placed collision, camera and player request queries. No dynamic-body engine or mission/entity owner exists here. | `engine/spatial` |
| `scene/environment` (cpp/header) | Region-selected scalar channels with per-channel interpolation and explicit supplied transitions; no meaning for the channels. Only game StageEnvironment currently supplies those meanings. | `engine/spatial/environment` (file, not invented world owner) |
| `scene/visibility` (cpp/header) | `query_static_shadow_caster`, `query_model_shadow_caster`, `query_placed_shadow_caster` and source caster eligibility geometry; consumed by StagePresentation shadow publication. Not a general visibility system. | `engine/render/shadows` |
| `script` (2 cpp / 2 headers) | Chunk converter handles original 32-bit Lua ABI; Context owns Lua state/coroutines/binding wrappers and bounded heap. Game session/mission drivers register concrete original bindings. | `engine/script` |
| `tasks` (cpp/header) | Ordered same-thread task lifecycle scheduler; RootDriver/session and mission bindings use it. No thread pool/worker scheduler. | `engine/tasks` |
| `timing` and `world` (2 cpp / 2 headers) | FrameClock computes presentation intervals/deadlines from supplied monotonic samples. WorldClock scales/publishes immutable owner-tagged world intervals and accumulates admitted simulation. Application owns/paces clocks; game supplies admission. | `engine/timing` |
| `video` (5 private/implementation / 3 public) | Player owns media playback/clock/drain policy, PcmBuffer bounded producer/consumer storage, FFmpeg decoder worker/codec owns streams. Target creates decoder, game startup playback owns sequence, native movie/audio outputs implement endpoints. | `engine/video`, `codecs/ffmpeg` |
| `framework` (2 cpp / header) | Concrete launch config, CLI and game composition/process loop described above. | `game/application` |
| `platform` (30 implementation/private / 4 public) | Detailed native pipeline below. Includes public Files/SaveStorage contracts, concrete host facade and macOS MediaAudio declaration. | grouped target/common/POSIX directories, portable contracts kept singular |
| `input` (cpp / 2 headers) | `PhysicalState` is neutral copied device observation. `ActionRepeat` currently called by native host. Actual OS sampling is inside host, not this directory. | engine input contracts/repeat mechanism plus real native input directory and game binding/input owner |

## Native execution and output path

`platform/macos/main.cpp::main` selects CPU backing, creates resource/runtime/media
domains and device budget, chooses read/save adapters and constructs StartupHost.
It selects FACT via NativeGameAudioFactory and FFmpeg via decoder factory, then
calls concrete game application composition. This target entry is allowed to
know game and backend types; engine subsystem code must not.

`StartupHost::State` owns Cocoa window/delegate/layer, Metal device/queue,
WorldRenderer, MovieRenderer, SpriteRenderer, OutputCompositor and MediaAudio,
plus input state. The facade forwards producers; it is not a second registry.
`OutputCompositor::submit` performs world render/postprocess, movie and ordered
sprites into final drawable, commits/presents and waits for command completion.
WorldRenderer owns GPU model/textures/draw storage and delegates exposure/bloom/
color/output work to WorldPostprocess. MovieRenderer owns decoded movie texture;
SpriteRenderer owns UI texture/command storage. `buffer_allocation` and
`texture_allocation` wrap native resources and device charges. The five Metal
files remain with this backend; public portable shader-selection/material state
remains engine render code. This is the rendering pipeline's actual ownership,
not a proposal for a new frame graph or asynchronous renderer.

MediaAudio uses AudioQueue for decoded movie PCM and its playback clock. FACT
owns game effects/music voices separately. Neither belongs to a game actor or
menu folder. `save_directory` chooses macOS Application Support location;
POSIX storage performs bounded read/atomic replace. Native CPU backing uses the
target allocation API through `memory::CpuBacking`.

## Proposed layout consequences

Use an explicit `src/engine` parent to distinguish mechanism implementation from
`src/game`; keep singular portable `include/bw/<subsystem>` contracts. Engine
modules are **not asserted generally reusable just because they live there**:
several intentionally implement Cavia's original renderer/animation/format
rules. The concrete distinction is their contract and absence of game
session/actor/profile/menu ownership. No new game framework should be invented.

The map groups render headers by the same responsibility as their shared code;
engine `lighting`, `spatial`, `timing` headers match their corrected identities.
Native headers are private with the corresponding native implementation.

Input separation, host renaming/extraction and game-specific schema moves are
explicit dependency repairs, not a claim that slice 3's previous mechanical
move completed them. Preserve accepted runtime behavior and lifetimes while
implementing the eventual approved structure. This report proposes no new
features and does not close the unrelated menu-art/prompt backlog.
