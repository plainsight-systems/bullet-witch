# Engine and game reorganization plan

**October 26, 2026 · revised architecture implemented, verified and accepted.**
Slices 1 and 2 are accepted and committed. The initial slice 3 mechanical grouping
was rejected: moving files had not resolved application, input, actor, developer
and build ownership. Its passing checks are historical behavior-preservation
evidence, not acceptance of this redesign. The user approved the
[whole-repository architecture](../../research/engine/subsystems/architecture-layout-proposal.md)
and accepted the integrated implementation. The rejected grouping was adapted
directly and is not landed as an intermediate commit.

## Approved structure

Use explicit `src/engine`, `src/game`, `src/platform`, and `src/developer` roots.
Singular portable engine headers stay under `include/bw`; private declarations
remain beside their implementation. This supersedes the earlier peer-root choice.
No empty future systems or library-per-directory scheme is required.

| Root / family | Responsibility |
| --- | --- |
| `src/engine/` | Animation, audio/video, content formats/resources, input mechanism, lighting, spatial, render, memory, script/tasks and timing |
| `src/game/application/` | Bullet Witch process composition, launch policy and main loop |
| `src/game/session/`, `mission/`, `world/` | Root/session policy; mission admission/loading/effective state; current actor/contact/camera and world presentation |
| `src/game/actors/animation/`, `actors/alicia/` | Common original animation publication adapter versus Alicia actions, animation modifiers, resources and audio policy |
| `src/game/player/{input,camera}/` | Human-player mapping/routing and camera policy |
| `src/game/content/original/` | Original game loader/namespace/channel policy; this is source code, not asset data |
| `src/game/ui/`, `profile/`, `settings/` | Original screens, shared Options, persistent authority and device settings |
| `src/platform/` | Actual common/POSIX/macOS adapters; macOS window/input/audio/files/memory/render, native shaders and process entry |
| `src/developer/animation_review/` | Existing review tooling selected only in developer builds |

The [complete file map](../../research/engine/subsystems/architecture-layout-file-map.json)
records every traced source/header/shader and required split. Actual assets stay
outside `src/` and Git. Optional local `/assets/original` and `/assets/overrides`
are ignored workspace locations, not a newly implemented pack resolver.

## Ownership repairs in this implementation

- Game application owns composition and the main loop; engine timing owns pacing
  and world-clock mechanisms. `WorldRuntime` composes current simulation and
  presentation; it does not claim a complete entity registry or mission startup.
- Native `WindowHost` returns a neutral `InputFrame`. Native `InputSource` owns
  physical acquisition; game `ControllerInput` owns fixed UI/saved mappings and
  one supplied-time repeat history. Loading uses that same owner. See
  [input contract](../../research/engine/subsystems/neutral-input-boundary.md).
- Developer implementations compile only with `BW_DEVELOPER_VIEWS`; production
  hooks are inert and do not link review navigation/preview owners or resources.
  This is dependency repair, not a new inspector, overlay or activation UX.
- Alicia raised-weapon action lifetime is separate from aim-pose blending.
  Alicia/weapon semantic classification is supplied explicitly to the common
  original animation adapter; unknown semantics remain unknown. The existing
  support-hand adapter stays with Alicia animation; numerical two-bone geometry
  stays in engine math.
- Engine lighting, spatial and timing are visible separate families. Game world
  presentation retains authored channel meanings. RenderScene retains membership
  authority; actors publish pose/shape/visibility through narrow capabilities.
- World, movie and UI remain sibling producers for native output composition.
  Keep accepted source ordering, shader/math behavior and synchronous completion.
- Game original-content loaders are distinct from engine binary schemas,
  bounded `binary_reader.hpp`, catalog acquisition and residency. Session owns
  game bank admission; UI/Alicia own their cue policy; no second audio runtime.
- Build membership follows these dependencies in this changeset. `bw_profiles`
  contains profile/settings authority; permanent application/presentation/playback
  use `bw_game_application`, `bw_game_presentation`, `bw_game_playback`.
  Native host is `bw_window_host_macos`; executable is `bullet-witch`;
  `bw_metal_shaders` builds the complete `bullet_witch.metallib`.

## Sequence and acceptance

| Work | State / completion boundary |
| --- | --- |
| 1. Mission and presentation authority | Accepted and committed `fd773fb`: simulation/presentation split and retained RenderScene publication/lifetime. |
| 2. Lighting and output integration | Accepted and committed `a5577bb`: renderer-facing lighting, sibling output composition and retirement. |
| Initial 3. Mechanical game grouping | Rejected; uncommitted work was replaced by the approved full architecture. Earlier checks remain historical only. |
| Revised 3. Whole-repository ownership and layout | Implemented: concrete repairs above, exact source map, owner-mirrored tests, dependency/tooling/docs integration. Full gates for developer OFF/ON and purchased-content traversal pass. User accepted the integrated revision on October 26, 2026 and authorized its commit. |
| Former 4. Build/documentation closure | Folded into revised slice 3. Do not defer required dependency repair to a cosmetic follow-up. |

User acceptance authorizes the scoped implementation commit; pushing requires
instruction. Both UI observations (costume pink block and A/B prompts) stay in
backlog. No new gameplay, dynamic physics, NPC AI, effects, asset-pack runtime,
render thread or debug UI belongs to this reorganization.

## Verification and preserved behavior

Preserve original Lua ordering, screen text/eligibility/cues, shared Options save
authority, current controls/HUD, supported reentry and costume retention. Preserve
separate presentation/world/media clocks, pause admission, loading rebases and
accepted lighting/pass/CPU-GPU behavior. Originals remain purchased/read-only;
assets and raw decompiler output stay out of Git.

Consult both C++ corpora for design and implementation. Run the full repository
gate, existing purchased-content traversal, and relevant native contracts after
integration. Verify production developer exclusion and owner dependencies, not
only runtime appearance. Existing tests are retained and regrouped by owner;
fixture traversal is not native visual/audio parity.

Notify before the native walkthrough: boot/title/frontend/audio, mission load,
movement/aim/evade/weapons, HUD/ring, pause Help/Options, costume, restart/return.
Do not reopen accepted lighting limitations or the separate UI backlog as
incidental reorganization work. No current validation result is claimed here
until the integrated revision has actually completed those checks.

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
