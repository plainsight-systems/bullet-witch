# Runtime subsystem inventory

**Source inventory · October 24, 2026.** This is a status checkpoint for the
single-player 3D Bullet Witch reconstruction, not a universal engine checklist.
Responsibilities need explicit ownership; they do not each require a singleton,
library or new abstraction. Networking, a general editor and unrelated engine
features are not missing requirements for this port.

## Reading the status

- **Present:** an implemented owner/mechanism exists for the stated scope. This
  does not mean complete original-game parity or physical acceptance.
- **Partial:** working code covers part of the responsibility, but integration,
  ownership or required behavior is incomplete.
- **Planned:** described/researched, without the corresponding runtime system.

The [build targets](../../../CMakeLists.txt), headers and implementations are
the evidence below. The [architecture](../../../docs/architecture/README.md)
also describes the finished target; a box in that architecture is not evidence
that the system exists. This inspection found ownership/completion gaps, not a
new demonstrated rendering, animation or gameplay correctness failure.

## Implemented responsibilities and remaining scope

| Responsibility | Status and source evidence | Remaining boundary |
|---|---|---|
| Platform and devices | **Present, Mac scope.** [Platform contracts](../../../include/bw/platform/) and [Mac adapters](../../../src/platform/macos/) cover files, storage, window/events, graphics and audio. | Windows/Xbox implementations and verification are planned. |
| Shared math | **Present.** `bw_math`, [math headers](../../../include/bw/math/): vectors, matrices, quaternions, frusta and scalar/direction helpers. | Target-specialized kernels can replace shared calculations behind the same contracts when warranted. |
| Memory | **Present foundation.** [`Domain`](../../../include/bw/memory/domain.hpp), [`CoalescingHeap`](../../../include/bw/memory/coalescing_heap.hpp), [`DeviceBudget`](../../../include/bw/memory/device_budget.hpp) and native backing implement ownership, coalescing, accounting and retirement. | World/AI allocation lifetimes, asynchronous retirement and documented default-heap migration boundaries remain. Do not invent arenas before their reset/quiescence contracts exist. |
| Resources and loading | **Partial.** [`ResourceService`](../../../include/bw/resources/service.hpp), catalog/format readers and [`MissionLoading`](../../../src/game/mission_loading.hpp) provide phased publication, retained leases and mission resource residency. | General asynchronous loading/streaming and complete handlers are unfinished. `ResourceService` is explicitly single-threaded. |
| Rendering | **Partial.** [`WorldOutput`](../../../include/bw/render/world.hpp), shared material/view/shadow/output kernels, native [`WorldRenderer`](../../../src/platform/macos/world_renderer.hpp) and [`WorldPostprocess`](../../../src/platform/macos/world_postprocess.hpp). Static/actor geometry, skinning, shadows and HDR composition work. | Dynamic-world membership/visibility, effects and complete original pass coverage remain. Scene assembly still resides substantially in game inspection code. |
| Lighting and environment | **Present for admitted scene.** [`SceneLighting`](../../../include/bw/scene/lighting/scene_lighting.hpp) owns fields, emitters and receiver selection; [`Environment`](../../../include/bw/scene/environment.hpp) owns region/curve transitions. | Dynamic entity membership must join the future world owner. [`stage_environment`](../../../src/game/stage_environment.cpp) correctly maps original game channel meanings outside the engine. |
| Animation | **Present mechanisms; partial game coverage.** [Animation library](../../../include/bw/animation/) owns poses, clip catalogs, playback rings, events, shape/secondary motion and scalar curves. | Other actors and world-affecting animation events need their own consumers. Alicia action policy belongs in game code. |
| Collision and spatial queries | **Partial.** [Static/character collision and area regions](../../../include/bw/scene/spatial/) provide prepared geometry queries and custom character contact. | Dynamic bodies, destruction and associated world ownership are planned. Static contact is real collision, not a substitute claim of completed physics. |
| Scripting and task lifecycle | **Present mechanisms.** [`script::Context`](../../../include/bw/script/context.hpp) and [`TaskLifecycleScheduler`](../../../include/bw/tasks/task_lifecycle_scheduler.hpp). | Mission/native bindings are incomplete. The lifecycle scheduler runs on its caller thread; it is not a worker pool or authoritative world scheduler. |
| Input | **Partial.** [`StartupInput` / `StartupHost::pump`](../../../include/bw/platform/startup_host.hpp), [native action edges](../../../src/platform/macos/gamepad_actions.hpp). Hardware input, focus handling and button edges work. | Input remains bundled with the startup host. Gameplay/UI contexts, configurable bindings and persisted options require their owning boundary. |
| Audio | **Partial.** [`CueOutput`](../../../include/bw/audio/output.hpp) and [FACT backend](../../../src/audio/fact/) implement bank/cue lifecycle, listener/spatial playback and pause. Character/mission adapters resolve game events. | Persisted volumes do not yet control gain; menu cues/music and broader world event audio remain. |
| Video | **Present.** [`video::Player`](../../../include/bw/video/player.hpp), decoder/output interfaces, FFmpeg workers and native presentation provide separate media timing, drain and lifetime ownership. | Wider gameplay integration and original observation gaps remain; media consumption clocks are not world simulation clocks. |
| UI and text | **Partial.** [`SpriteOutput`](../../../include/bw/render/sprites.hpp), font/image decoding and game title/menu/loading/pause surfaces work. | Authoritative gameplay HUD and complete original pause/retry/exit flow remain. UI presentation primitives belong in engine; menu/progression policy belongs in game. |
| Persistence | **Partial.** [`Profile`](../../../src/game/profile.hpp), profile load flow and [`SaveStorage`](../../../include/bw/platform/save_storage.hpp) implement profiles/settings. | Consistent world/checkpoint capture and restoration are unfinished. Storage and serialization mechanisms must remain separate from progression policy. |
| Diagnostics | **Partial.** Domain/device snapshots, video statistics, startup timing and resource/native inspection tools exist. | No unified engine logging/profiling service. These counters do not establish whole-level performance acceptance. |

## Integration gaps to close deliberately

1. **World lifecycle and authoritative time/pause — planned owner, partial
   execution.** [`startup_application`](../../../src/framework/startup_application.cpp)
   and [`StageInspection`](../../../src/game/stage_inspection.hpp) orchestrate a
   bounded traversal scene. They do not implement the target world entity
   registry, hierarchy, phase schedule or universal pause contract. Establish
   those contracts before adding more independently timed gameplay systems.
2. **Scene assembly — partial ownership.** `StageInspection` still selects draws,
   combines model observations and coordinates presentation updates. Its working
   engine mechanisms should be composed through world/render-scene ownership;
   do not move game material compatibility or camera-follow policy into generic
   engine code merely to empty this class.
3. **Simulation/render publication — coupled existing responsibility.**
   [`PlayerController::advance`](../../../src/game/player_controller.cpp) accepts
   `render::WorldOutput&` and invokes
   [`PlayerActor::publish_pose` / `publish_completed_pose`](../../../src/game/player_actor.cpp).
   The future common actor/world owner needs an explicit model/render observation
   phase, preserving the recovered parent/model update order. Repeating this
   controller-to-render dependency for every NPC would compound the ownership
   gap. This is an integration risk, not a newly demonstrated rendered defect.
4. **Input ownership — partial existing responsibility.** `StartupHost` combines
   window/device presentation with action sampling. Keep native event/controller
   access in platform code, but give input state and gameplay/UI context routing
   explicit ownership before more screens and actors compete for actions.
5. **Mission startup and outcome — planned game integration.**
   [`MissionPhase::awaiting_world_setup`](../../../src/game/mission_driver.hpp)
   preserves the original VM at the missing startup boundary. Genuine spawn,
   checkpoint, death/retry/exit and completion must replace the sole authorized
   [initial-placement shim](../../../src/game/initial_placement_shim.hpp).
6. **AI/navigation, combat and effects — planned runtime systems.** Loading NPC
   palettes or mapping effect formats is not executing enemies, hits/damage,
   objectives, particle instances or spells that affect scenery. Reusable
   entity/query/effect mechanisms belong in engine; enemy decisions, damage
   rules, spell policy and mission goals remain game responsibilities.
7. **Worker/execution services — planned where source requires them.** Media
   workers already exist, but no general job/resource worker system follows from
   that fact. Recover ordering and completion obligations rather than assigning
   one thread per subsystem or introducing concurrency for its own sake.

The current [queue](../../../docs/process/QUEUE.md) already places authoritative
world tick/pause and real gameplay pause/HUD after animation-slice acceptance.
The missing runtime systems above are mostly planned work. The ownership issue
is allowing temporary application/inspection composition to become the permanent
home for world, input or scene responsibilities as those systems grow.

## Documentation drift

The [architecture index](../../../docs/architecture/README.md) still calls
extra-mission native status/routing unresolved, although the current mission
implementation and [MEMORY](../../../docs/process/MEMORY.md) record that routing.
Some broad renderer/lighting unknowns also predate the implemented environment
and lighting owners. Keep target architecture and implementation status distinct;
reconcile these descriptions in the next architecture update. This inventory
does not silently rewrite those files or declare their entire target complete.

## External comparison boundary

[Godot's official architecture overview](https://docs.godotengine.org/en/stable/engine_details/architecture/godot_architecture_diagram.html)
distinguishes scene organization, rendering/audio/physics services, native drivers,
shared core and engine lifecycle. It is a useful completeness check, not a reason
to adopt Godot's singleton/server design.

[Doom 3's `Common.cpp`](https://github.com/id-Software/DOOM-3/blob/master/neo/framework/Common.cpp)
provides a game-and-engine comparison for shared lifecycle and diagnostics rather
than requiring a general-purpose editor engine. These references are general
comparison material only: no implementation was copied and neither establishes
Bullet Witch source behavior or parity.

## Verification scope

Read-only source/build-target/document inspection, followed by this research
note. No new runtime defect was reproduced, no production code was changed and
no builds or tests were run for this inventory. The note receives a whitespace
diff check; existing slice verification is recorded separately.
