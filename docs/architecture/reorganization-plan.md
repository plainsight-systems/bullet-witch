# Engine and game reorganization plan

**Accepted design · October 26, 2026. Slices 1 and 2 accepted.**
The user agreed the comparison refinements. This document fixes the recommended
boundaries, destination groups and implementation sequence. The user authorized
execution; each slice remains subject to its own acceptance. No new gameplay
capabilities are claimed. Read the
[proposed diagrams](proposed-boundaries.md) alongside the
[current implementation](current-ownership.md).

## Settled design

- Keep one `src/` root with peer engine families and `src/game/` for Bullet Witch.
  Keep singular portable headers under `include/bw/`, native implementations and
  shaders under their existing selected backend, and target-owned executable
  entries. Do not add a second `src/engine/` hierarchy or empty future folders.
- Distinguish **game world** (mission actors, actions, rules and script state)
  from **RenderScene** (retained renderable/light membership and prepared data).
  RenderScene is a responsibility name; final C++ contracts are designed in the
  implementation slice. No generic entity framework is required by this pass.
- Establish register, update, remove and retire operations. A narrow publisher
  can be owned by an actor/component; actor-originated publication is not itself
  a defect. Remove controller responsibility for evaluating lights or managing
  renderer lifetime. Do not require centralized whole-world copying.
- Keep lighting identifiable at `scene/lighting`, with scene-local instance data
  and renderer-facing packets. Game adapters supply original channel/placement
  meaning. Preserve accepted equations, sampling, shaders and CPU/GPU placement.
- World rendering, movie output and UI rendering feed a final output compositor.
  UI is not a world material pass. Native presentation owns device submission;
  game screens own their behavior, artwork selection and timing.
- Keep concrete gameplay/entity/controller/AI rules in game code. Extract a shared
  mechanism only when its actual source contract or existing reuse warrants it.
  A future world registry is not an engine object that controls every subsystem.
- Keep resource resolution/residency separate from mission content readiness and
  activation. Game/application policy validates the purchased install and selects
  future packs; an ordered resolver applies the selected precedence. No pack loader,
  new scripts, AI, dynamic physics, particle runtime or debug UX is implemented here.

## Destination map

These are implementation destinations, not additional runtime owners. Header/source
pairs move together; portable engine headers keep their established paths. Existing
basenames remain unless a responsibility split makes them misleading. `.*` below
means the named implementation/header family, not arbitrary matching unrelated files.

| Current family | Destination | Kind |
| --- | --- | --- |
| `src/framework/startup_application.*` | `src/framework/application.*` | Rename after making process composition/phase calls explicit; preserve one loop. |
| `src/game/startup_services.*`, `root_driver.*`, `startup_flows.*` | `src/game/session/` | Session/native-task composition; rename startup services to session services only after contracts establish the permanent role. |
| `mission_driver.*`, `mission_loading.*`, `mission_package_inputs.cpp`, `mission_gameplay.*`, `mission_control.*`, `mission_transition.hpp`, `mission_interaction.*`, `mission_player_state.*`, `mission_help_state.*`, `initial_placement_shim.*` | `src/game/mission/` | Mission activation, control, reentry and effective state; retain explicitly named placement shim. |
| `player_actor.*`, `player_controller.*`, `player_resources.*`, `player_targeting.*`, `alicia_*`, `aim_posture.*`, `character_animations.*`, `character_motion_player.*`, `facial_animation.*`, `motion_presentation.*`, `scripted_jump_policy.*`, `spell_selection.*`, `spell_presentation_policy.*` | `src/game/actors/` | Concrete actor/action and authored animation policy; shared numerical animation mechanisms stay in existing engine families. |
| `placed_collision.*` | `src/game/mission/` | Mission/stage collision-content assembly consumed by simulation/spatial; never owned by visual presentation. |
| `src/game/camera/` | Unchanged | Game camera policy is already grouped. |
| `stage_inspection.*` | Split responsibilities into `src/game/mission/` and `src/game/presentation/`; preview-only code into `src/game/developer/` | Behavioral ownership repair first; do not move the existing mixed owner wholesale and call it fixed. |
| `stage_environment.*`, `placed_lighting.*`, `stage_shadow.*`, `resident_model.hpp`, `npc_palette.*` | `src/game/presentation/` | Original visual stage/model adapters and scene membership. |
| `frontend.*`, `menu_flow.*`, `title_flows.*`, `advertise_flow.*`, `startup_playback.*`, `startup_title_surface.*`, `startup_menu_surface.*`, `startup_loading_surface.*`, `title_surface.hpp`, `menu_surface.hpp`, `ranking_*`, `power_up_*` | `src/game/ui/frontend/` | Reachable frontend routing and presentation; Power Up mission-result producer remains future. |
| `pause_menu.*`, `pause_menu_surface.*`, `pause_help.*`, `pause_help_surface.*`, `mission_pause_presentation.hpp`, `mission_pause_surface.*` | `src/game/ui/pause/` | Actual gameplay pause, help and pause-specific presentation. |
| `gameplay_hud.*`, `gameplay_hud_surface.*`, `ammunition_hud.*`, `player_cockpit.*`, `hud_sprite.hpp` | `src/game/ui/hud/` | Gameplay observation/presentation; action owners retain resource mutations. |
| `pause_options.*`, `pause_options_surface.*`, `frontend_options.hpp`, `frontend_options_surface.*`, `controller_options.*`, `controller_options_surface.*`, `menu_dialog.*`, `menu_prompt.*`, `sprite_text.*`, `sprite_color.hpp`, `pause_surface_text.hpp`, `menu_input.hpp`, `input_icon.*`, `help_input_icons.*` | `src/game/ui/shared/` | One shared Options/dialog/presentation family used by frontend and pause; remove pause-only naming where it misstates the shared contract. |
| `controller_input.*`, `player_input_routing.*`, `controller_settings.*`, `device_options.*` | `src/game/input/`, settings data in `src/game/settings/` | Input policy versus persistent device options; shared physical input/repeat code stays `src/input/`. |
| `profile.*`, `progression.*`, `progression_save.*`, `profile_load_flow.*` | `src/game/profile/` | Permanent state, codecs and save/load orchestration, preserving narrow System/Record authorities. |
| `game_audio.*`, `frontend_audio.*`, `character_audio.*`, `character_audio_compatibility.inc`, `menu_audio.*`, `menu_sound_requests.cpp` | `src/game/audio/` | Original cue/bank/category policy; shared runtime/output stays `src/audio/` and selected backends. |
| `animation_review.hpp`, `stage_animation_review.cpp`, `developer_review_flow.*`, `developer_views.hpp` | `src/game/developer/` | Existing gated diagnostics only; no new activation controls or inspector implementation. |
| `source_random.hpp` | `src/game/compatibility/` | Recovered game-specific random behavior; do not silently replace it with generic math. |
| `src/scene/{lighting,spatial,visibility}`, `src/render/`, `src/animation/`, other engine peer families | Retain existing families and singular `include/bw/` contracts | Make scene membership/pipeline responsibilities explicit within these families, not a new general engine framework. |
| `src/platform/macos/` renderer/postprocess/shaders/host and target `main.cpp` | Retain selected backend family; group graphics files locally only if needed for discoverability | Native code stays native; OS, API and CPU ISA remain distinct selection axes. No generic shader replacement. |
| `tests/` and CMake source lists/targets | Follow resulting responsibility groups without new executables per folder | Separate mechanical paths from target dependency repairs. Preserve existing test identities initially. |

No implementation file is to be left behind merely because it was absent from a
wildcard above: each slice records its exact move manifest before editing. Ambiguous
mixed files are split by their proven consumers, not assigned by filename alone.

## Ordered implementation slices

| Slice | Work and dependency | Completion/review boundary |
| --- | --- | --- |
| 1. Mission and presentation authority | Split StageInspection actor/camera/control and static-contact simulation from retained scene presentation and existing diagnostics in place. Establish RenderScene registration/update/removal/retirement contracts; retain actor publishers through narrow capabilities. Transfer authority once: no temporary duplicate authoritative draw or light registries. Preserve mission loading readiness and source phase ordering. | Headers/contracts reviewed before implementation; explicit owner/destruction map; current actor, costume, reentry and pause behavior retained. No directory sweep yet. |
| 2. Lighting and output integration | Builds on slice 1. Give scene-local lighting registration/evaluation an explicit renderer-facing contract; keep game adapters separate. Expose world/movie/UI sibling output composition and resource retirement in portable contracts/native implementations. | Accepted lighting/pass order, UI overlay order, media audio timing and synchronous completion remain unchanged. No new frame graph, async submission or visual tuning. |
| 3. Group game responsibilities | Apply destination map to session/mission, actors/camera/presentation, UI frontend/pause/HUD/shared, profile/settings/input/audio/developer families. Rename only misleading permanent startup/pause names. | Mechanical move commit distinct from ownership repair; includes/path updates and exact manifest. Shared Options, Profile, audio runtime and clocks remain single authorities. |
| 4. Build boundaries and documentation closure | Repair target membership/dependencies exposed by grouping: e.g. menu policy must not hide inside `bw_profiles`, permanent UI must not pretend to be startup-only. Retain peer engine families, backend selection and public headers. Update source maps, architecture status and run documentation. | No library per folder requirement. Final dependency inspection, repository gate and short native acceptance walkthrough; no gameplay/features added. |

Each slice is a reviewable unit. User acceptance authorizes its commit; do not
push without instruction. Do not bundle unaccepted following slices into an
accepted commit. Slice 1 is committed as `fd773fb`. Slice 2 lighting/output
integration passes the repository gate and is accepted after the October 26
native review; slices 3 and 4 remain subsequent work.

## Required invariants and validation

Preserve original Lua task order/status, native menu eligibility/text/cues/BGM,
Options save authority, controls, HUD, current supported reentry and costume
retention. Preserve independent presentation/world/media clocks, pause admission,
loading rebases, lighting math and native CPU/GPU partition. Retain the purchased
installation requirement, read-only original data, repository content policy and
no-assets/raw-decompiler-output rules.

For C++ contracts and implementation, consult both required corpora at their
respective phases, following the existing workflow. Run the required repository
gate on each completed code slice. Add focused checks only for changed lifetime,
publication or dependency contracts; mechanical moves do not require invented
new behavioral tests. Existing tests cannot prove native appearance/audio parity.

Before native verification, notify the user and coordinate navigation. A bounded
walkthrough covers boot/title/frontend child return and audio; mission load;
movement/aim/evade/weapon changes; HUD/ring; pause Options/Help; costume change;
Stage Start/Return and title return. Inspect rendering at the accepted lighting
baseline rather than reopening already accepted visual limitations. Run additional
checks only for concrete failures or modified behavior. Documentation-only changes
need links, structural Mermaid and whitespace checks, not a build.

## Reference hierarchy and comparison decisions

1. **Bullet Witch controls semantics and order:** [world timing](../../research/engine/subsystems/world-timing-source.md),
   [lighting producer](../../research/engine/subsystems/lighting-scene-producer-contract.md),
   [mission launch](../../research/engine/subsystems/mission-launch.md), and
   [frontend verification](../../research/engine/subsystems/frontend-verification.md).
2. **Doom 3 is the overall custom game/engine model**, at
   `a9c49da5afb18201d31e3f0a429a037e56ce2b9a`:
   [Game.h](https://github.com/id-Software/DOOM-3/blob/a9c49da5afb18201d31e3f0a429a037e56ce2b9a/neo/game/Game.h)
   exposes game frame/draw versus imported services;
   [RenderWorld.h](https://github.com/id-Software/DOOM-3/blob/a9c49da5afb18201d31e3f0a429a037e56ce2b9a/neo/renderer/RenderWorld.h)
   defines entity/light add/update/free membership. This supports one product with
   explicit game/render-world contracts, not copying its globals or DLL system.
3. **Pinned Unreal supplies concrete ownership examples**, at
   `f45729c0b6f097038e72b52d366a62baeca3275b`:
   [RendererScene.cpp](https://github.com/EpicGames/UnrealEngine/blob/f45729c0b6f097038e72b52d366a62baeca3275b/Engine/Source/Runtime/Renderer/Private/RendererScene.cpp)
   `BatchAddPrimitivesInternal`, primitive removal and precomputed-light registration
   support retained scene membership and actor/component publication;
   [GameInstance.h](https://github.com/EpicGames/UnrealEngine/blob/f45729c0b6f097038e72b52d366a62baeca3275b/Engine/Source/Runtime/Engine/Classes/Engine/GameInstance.h)
   supports persistent session versus map lifetime. Existing
   [timing](../../research/engine/subsystems/world-timing-reference.md) and
   [lighting comparisons](../../research/engine/subsystems/lighting-reference-boundaries.md)
   retain the inspected functions and applicability. No Unreal threading, class
   hierarchy or modern lighting model is imported.
4. **Lyra supplies readiness examples**, at that same Unreal revision:
   [LyraGameMode.cpp](https://github.com/EpicGames/UnrealEngine/blob/f45729c0b6f097038e72b52d366a62baeca3275b/Samples/Games/Lyra/Source/LyraGame/GameModes/LyraGameMode.cpp)
   `OnExperienceLoaded` / `HandleStartingNewPlayer_Implementation` gate spawning;
   [LyraBotCreationComponent.cpp](https://github.com/EpicGames/UnrealEngine/blob/f45729c0b6f097038e72b52d366a62baeca3275b/Samples/Games/Lyra/Source/LyraGame/GameModes/LyraBotCreationComponent.cpp)
   keeps concrete bot policy game-side;
   [LyraAssetManager.cpp](https://github.com/EpicGames/UnrealEngine/blob/f45729c0b6f097038e72b52d366a62baeca3275b/Samples/Games/Lyra/Source/LyraGame/System/LyraAssetManager.cpp)
   layers game definitions above engine loading. Lyra is a source-available sample,
   not a reason to import its feature-plugin, experience or GAS framework.

The earlier [source organization comparison](../../research/engine/subsystems/source-organization.md)
records the pinned Quake/Doom alternatives. These references support the chosen
contracts, not a claim that Cavia used those engines or had these class names.
