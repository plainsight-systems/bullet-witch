# Runtime subsystem inventory

**Source/build checkpoint · October 26, 2026 · `cac036f`.** This describes the
current single-player Bullet Witch reconstruction. It distinguishes working
mechanisms from the larger engine/game owners still required; it does not
prescribe one library or singleton per responsibility. Networking beyond the
original online services, a general editor and unrelated engine features are
not prerequisites for this port.

**Present** means an implemented owner exists for the stated scope, not complete
original-game parity. **Partial** means implemented behavior has explicit
remaining integration or coverage. **Planned** means there is no corresponding
runtime implementation, even where extensive original-source research exists.

The [build targets](../../../CMakeLists.txt), linked contracts and source are the
evidence. The [target architecture](../../../docs/architecture/README.md) is not
proof of implementation. Frontend, pause, timing, lighting and HUD acceptance
apply to their recorded slices, not the remaining full-gameplay systems.

## Implemented scope and missing boundaries

| Responsibility | Current implementation / build target | Remaining boundary |
| --- | --- | --- |
| Platform/devices | **Present for macOS.** [Platform contracts](../../../include/bw/platform/) and [native adapters](../../../src/platform/macos/); `bw_startup_host_macos`, `bw_world_renderer_macos`, device/backing/storage targets compose `bw-startup`. | Windows/Xbox runtime backends and platform verification are not implemented. Shared code compiling is not platform acceptance. |
| Math | **Present.** Header-only `bw_math`: [vectors, matrices, quaternions, frusta, scalar responses and two-bone solver](../../../include/bw/math/). | Target-specific SIMD kernels are future implementation choices behind these contracts, not a missing math library. |
| Memory | **Present foundation.** `bw_memory`: [Domain](../../../include/bw/memory/domain.hpp), [CoalescingHeap](../../../include/bw/memory/coalescing_heap.hpp), [DeviceBudget](../../../include/bw/memory/device_budget.hpp), allocator adapters and `bw_cpu_backing_macos`. Resource/runtime/media ownership, reclaiming Lua allocation and device accounting are implemented. | AI linear arenas, future world/worker reset lifetimes, asynchronous GPU retirement and remaining external/default-heap boundaries are not complete. See [migration](memory-migration-completion.md) and [concurrency](memory-concurrency-refinement.md). |
| Resources/loading | **Partial.** `bw_resources`: [ResourceService](../../../include/bw/resources/service.hpp), catalogs, archive/format readers and retained leases; `bw_game` [MissionLoading](../../../src/game/mission_loading.hpp) coordinates original load requests and presentation. | ResourceService is single-threaded. General asynchronous streaming and unsupported original handlers remain unfinished. |
| Frame/world time | **Present for admitted execution.** `bw_timing`: [FrameClock](../../../include/bw/timing/frame_clock.hpp) and [WorldClock / immutable WorldFrame](../../../include/bw/world/world_clock.hpp). `bw_game` [MissionControl](../../../src/game/mission_control.hpp) owns pause admission; [MissionGameplay](../../../src/game/mission_gameplay.hpp) publishes one frame and sequences consumers. | This is not the complete original entity hierarchy, physics/script phase schedule or a world aggregate. Local animation/media cursors do not own world time. See [timing](../../../docs/architecture/world-timing.md). |
| World/entity lifecycle | **Planned aggregate; partial composition.** Framework creates mission owners; [MissionGameplay](../../../src/game/mission_gameplay.hpp) and [StageInspection](../../../src/game/stage_inspection.hpp) coordinate the current bounded scene in `bw_game`. `include/bw/world/` currently provides the clock only. | No general entity registry, hierarchy, spawn/despawn lifecycle or complete world phase owner. A class named MissionGameplay does not establish those systems. |
| Rendering | **Partial.** `bw_render` [output/material/view/shadow contracts](../../../include/bw/render/), `bw_world_renderer_macos` and [Metal shaders](../../../src/platform/macos/shaders/) render static/actor geometry, skinning, shadows, HDR/postprocessing and authored sampler/mip paths. | Dynamic membership/visibility, particles and complete original pass coverage remain. Scene assembly still substantially lives in StageInspection. |
| Lighting/environment | **Present for admitted scene.** `bw_scene`: [SceneLighting](../../../include/bw/scene/lighting/scene_lighting.hpp), irradiance/reflection fields, local lights, [Environment](../../../include/bw/scene/environment.hpp), regions and shadow-caster selection. `bw_game` [stage environment](../../../src/game/stage_environment.cpp) maps original game channels. | Dynamic entities, weather/event/effect producers and destruction-driven membership require their owning gameplay systems. Accepted appearance does not close every original comparison; see [lighting architecture](../../../docs/architecture/lighting.md). |
| Animation/character motion | **Present mechanisms; partial actor coverage.** `bw_animation`: [pose, clips, playback, events, shape/secondary motion](../../../include/bw/animation/). `bw_game` owns Alicia policy, support arm, locomotion, targeting and camera. | NPC/boss consumers, world-affecting action events and shared actor publication ownership remain. Animation playback alone does not implement combat. |
| Static collision/custom controller | **Present for traversal; partial physics.** `bw_scene` [static and character collision](../../../include/bw/scene/spatial/); `bw_game` placed-collision preparation and custom PlayerController consume these queries. | This is genuine static contact, not a middleware simulation. Dynamic bodies, impulses, constraints, destruction and the original PhysX scene step have no implemented provider. [Original physics](physics-and-collision.md) and [placed geometry](placed-physics-collision.md) distinguish those boundaries. |
| Lua/task lifecycle | **Present mechanisms.** `bw_runtime`: [Context](../../../include/bw/script/context.hpp), original Lua 5.0.2 and [TaskLifecycleScheduler](../../../include/bw/tasks/task_lifecycle_scheduler.hpp); game root/mission drivers own bindings. | Lifecycle tasks run on their caller thread. Mission/native bindings and genuine world startup remain incomplete; this scheduler is not a job pool. |
| Jobs/workers | **Planned general service.** No general job-system target or shared worker scheduler exists. Video decoder/audio dependencies have their own workers. | Recover ordering, completion and quiescence contracts before adding concurrent world/resource jobs. Existing media threads do not supply that service. |
| Input/settings | **Present for current gameplay/UI.** `bw_input` [physical samples/repeat](../../../include/bw/input/); `bw_game` [controller mapping](../../../src/game/controller_input.hpp), [PlayerInputRouting](../../../src/game/player_input_routing.hpp), controller options and device options. Native sampling stays in platform; copied settings drive game mappings and fixed UI input. | Sampling is still exposed through StartupHost. Future actor/action contexts and gameplay vibration producers remain; persisted remapping, camera options and live settings are no longer missing. |
| Audio | **Present service; partial producers.** [Audio contracts](../../../include/bw/audio/), `bw_audio_decode` and `bw_audio_fact` in [GameAudio.cmake](../../../cmake/GameAudio.cmake); `bw_game` [GameAudio](../../../src/game/game_audio.hpp) owns session banks/output, FrontEndAudio and character/menu adapters consume them. Original menu BGM/cues and persisted mix levels work. | Full combat, impact, foley, environment and spell event producers belong to future gameplay slices. Backend/spatial playback capability is not proof those events are connected. |
| Video | **Present.** `bw_video`, `bw_video_ffmpeg` and native output implement [Player](../../../include/bw/video/player.hpp), decode queues, audio/video pacing, drain and retirement. | Opening/logo playback does not implement every in-game cinematic consumer. Media clocks remain distinct from world simulation. |
| HUD/menus/text | **Present accepted slice.** `bw_startup_presentation` provides purchased art/localization; `bw_game` owns [cockpit/HUD](../../../src/game/player_cockpit.hpp), spell ring, original frontend and pause/Help/Options/controller/calibration/costume flows. Frontend Power Up is reachable. | Live world effects/targets, mission-result production and subtitle event producers remain. Ranking follows original unavailable-provider behavior; Steam networking is not implemented. See [frontend acceptance](frontend-verification.md) and [pause](native-pause-menu.md). |
| Progression/live resources | **Present for current scope.** `bw_profiles`: [Profile/progression](../../../src/game/profile.hpp), MissionPlayerState and PowerUpFlow; `bw_game` [PlayerResources](../../../src/game/player_resources.hpp) owns health/magic/four magazines and restricted action transactions. Concept completion context is separate from permanent saves. | Combat outcomes, spell prerequisite owners, death/objectives and genuine result awards are not implemented merely because HUD values and purchases work. |
| Persistence | **Partial.** `bw_profiles` ProfileLoadFlow and narrow System/Record save capabilities; `bw_save_storage_posix` atomic storage and macOS save-directory adapter. Original load/create/overwrite/autosave dialogs and settings transactions work. | Validating imported checkpoint bytes does not implement nonzero world restoration. Consistent world/entity capture and mission-result persistence remain unfinished. |
| NPC AI/navigation | **Planned runtime; researched source.** [AI runtime](ai-runtime.md) maps original trees, Lua evaluation and native actions. `bw_game` [npc_palette](../../../src/game/npc_palette.hpp) prepares content metadata. | No running enemy/boss AI, navigation or action executor. Loading palettes and auditing scripts are not NPC execution. |
| Particles/effects | **Planned runtime; researched source.** [Particle contracts](particle-runtime-contracts.md) and [effects mapping](particles-and-effects.md) trace original emission, simulation, submission and retirement. | No particle instance/simulation/render service. Spell presentation, scalar animation and effect resource observations do not implement active particles or scenery interactions. |
| Diagnostics | **Partial.** Memory/device snapshots, media statistics, resource inspectors and controlled corpus tools exist; normal builds disable developer views. | No unified logging/profiling service or accepted new debug UI. [Developer tools](../../../docs/architecture/developer-tools.md) remain a design boundary. |

## Ownership work before full gameplay

1. Establish the world/entity aggregate and original phase ownership without
   replacing the accepted timing authority. [MissionDriver](../../../src/game/mission_driver.hpp)
   still stops at `awaiting_world_setup`; the sole authorized
   [initial-placement shim](../../../src/game/initial_placement_shim.hpp) is not
   script-owned spawning. Pause reentry/retirement now exists, while genuine
   checkpoint restoration and the full mission outcome loop do not.
2. Separate scene/actor publication from controller policy as more actors arrive.
   [PlayerController](../../../src/game/player_controller.cpp) still publishes
   through PlayerActor to WorldOutput; StageInspection still assembles scene
   observations. Preserve original parent/model ordering and keep game material,
   camera and progression policy out of generic engine owners.
3. Review file and build boundaries against those responsibilities. Current
   `bw_game` and `bw_startup_presentation` group many implemented owners; their
   flat file layout is not evidence that every owner is missing. Conversely,
   moving files cannot supply the absent world, physics, AI or particle systems.

## Original and established-reference boundaries

Original behavior remains primary: [timing source](world-timing-source.md),
[physics](physics-and-collision.md), [actor/AI lifecycle](actor-lifecycle.md),
[particle contracts](particle-runtime-contracts.md) and the frontend/pause reports
record recovered consumers and their limits. Keep research-only mappings separate
from runtime rows above.

Existing pinned Unreal comparisons remain in [timing ownership](world-timing-reference.md),
[memory](memory-unreal-reference.md), [media](media-reference-design.md) and
[lighting](lighting-reference-boundaries.md). They document concrete inspected
functions and applicability; they do not establish Bullet Witch parity or imply
adoption of Unreal's complete engine. No new implementation/reference choice is
made by this inventory.

[Godot's architecture overview](https://docs.godotengine.org/en/stable/engine_details/architecture/godot_architecture_diagram.html)
and [Doom 3's Common.cpp](https://github.com/id-Software/DOOM-3/blob/master/neo/framework/Common.cpp)
remain general completeness comparisons, not runtime evidence or required layouts.

## Verification scope

Source, build-target and existing acceptance-report inspection at `cac036f`.
Only this research note changed. Local links and whitespace were checked; no
production edits, build, tests or native launch were performed for this update.
Earlier source notes/queue entries may describe superseded pre-menu gaps; the
accepted slice reports and current source take precedence for implementation
status, without promoting their scope to complete gameplay.
