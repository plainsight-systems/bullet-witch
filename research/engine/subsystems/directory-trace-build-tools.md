# Directory trace: build, verification and repository tooling

Date: 2026-10-26. Read-only trace of the unaccepted slice-3 working tree.
This is proposal evidence, not implementation approval. No build, test,
application launch, code change, commit or push was performed for this trace.
The proposed physical tree is integrated by the game/engine trace leads; paths
below express ownership rather than a competing final architecture.

## Findings that affect the proposed boundaries

1. `src/framework/application.cpp` is concrete Bullet Witch application
   composition. It mounts stable/PW/cm packages, creates the profile, frontend,
   native game-audio adapter, mission and HUD/pause surfaces, and owns the loop.
   Its public-to-the-repository header takes `game::GameAudioFactory` and stores
   `game::ConceptProfile`. Rename/move this to game application composition;
   it is not an engine framework. The platform entry is the executable's
   composition root, so its game dependencies are intentional.
2. `bw_profiles` is not a profile library: it compiles menu dialogs/audio
   requests, frontend Power Up and mission player state as well as save/profile
   codecs and controller settings. `bw_startup_presentation` contains permanent
   HUD, pause, shared Options and frontend surfaces. `bw_startup_framework`
   contains the concrete game application. These are dependency/target boundary
   corrections, not just filesystem labels.
3. `bw_resources` combines memory-backed catalog/service, every original format
   reader except the image family, source content policy (`content/stage` and
   `character_support`), and host filesystem implementation. The engine-format
   trace must decide exact format/compatibility ownership before splitting this
   broad target; do not claim it is a generic resource library because of its
   name. `content/stage.cpp` reaches the private parser with
   `../formats/reader.hpp`, an actual cross-family private-header dependency.
4. `bw_input` contains only repeat arithmetic; its neutral physical-state type
   is header-only, Mac polling is in `bw_startup_host_macos`, and binding/policy
   code is in `bw_game`. The target's size does not show actual input ownership.
   `bw_timing` already builds both FrameClock and the file in `src/world/`.
   Thus `src/world` does not indicate a separate world implementation/library.
5. `bw_profiles PUBLIC src`, several native targets `PUBLIC src`, and backend
   factories `INTERFACE src` expose the entire private source tree to consumers.
   In particular `bw_game PUBLIC bw_profiles` spreads that exposure. The singular
   `include/bw` root is intentional, but a broad private-root search path is not
   dependency enforcement. Shared include visibility needs explicit permitted
   edge checks, and consumer-specific backend factory headers must not grant
   unrelated private access. Merely using `PRIVATE` link keywords does not hide
   all private headers or eliminate static-library link requirements.
6. Developer review translation units are unconditionally in `bw_game`;
   `BW_DEVELOPER_VIEWS` is a public compile definition gating features, not a
   separate production-excluded target. MissionInteraction binds review
   catalogs through concrete review owners. Moving them out of `game/` alone
   cannot establish the approved compile-out/noop developer boundary. This needs
   a small capability/adapter correction and build exclusion; it does not
   authorize a new inspector, overlay or automation protocol.
7. Test target names retain milestone accidents too: `memory-tests` runs Metal
   final-edge/compositor/scene tests, `resource-tests` runs math/animation/render/
   lighting, and `runtime-tests` runs most game frontend/actor/pause behavior.
   Tests are grouped executables with driver calls, not one case per target.
   Moving each file under its actual owner must preserve driver registration and
   the eleven documented integration scenarios. No test run was done here.
8. `cmake/MacShaders.cmake` links movie, sprite, world depth/color and world
   output into `movie.metallib` via `bw_movie_shaders`. Those names are now
   misleading even though the native shader location is valid. `cmake/GameAudio`
   and `MovieDecoder` also combine dependency fetching with project target
   construction; separating dependency recipes from owned target declarations
   is useful build organization, not a runtime algorithm change.

## Actual target links and source ownership

`P` means CMake PUBLIC dependency, `p` PRIVATE. Imported/system/vendor targets
are identified separately. This comes from declarations in `CMakeLists.txt`
and all seven tracked `cmake/` files, not library names or a speculative graph.

| Target | Compiled responsibility | Direct links |
|---|---|---|
| bw_math | Header-only math | None |
| bw_memory | CPU domains/heap and device budgets | None |
| bw_resources | Original parsers + catalogs/services + source content assembly + HostFiles | P memory, math |
| bw_input | Action repeat | None |
| bw_timing | FrameClock + WorldClock | None |
| bw_render | Retained RenderScene, materials/output/view/shadow/math adapters | P math |
| bw_animation | Clip/pose/secondary/event/shape/playback/scalar mechanisms | P resources |
| bw_scene | Spatial collision/regions, environment, lighting/visibility/publication | P resources, animation, render |
| bw_images | Original image/font/world texture + mips | P resources; p png_static |
| bw_video | Player + PCM buffer | P memory |
| bw_video_ffmpeg | FFmpeg decoder backend | p video, FFmpeg avformat/avcodec/swscale/swresample/avutil, Threads |
| bw_audio_decode | FFmpeg original wave/music conversion | P resources; p avcodec/avutil/swresample |
| bw_audio_fact | XACT-compatible cue backend + WMA bridge (Apple-selected today) | P audio_decode; p FAudio-static |
| bw_runtime | Lua chunk/context + task lifecycle | P resources; p lua502 |
| bw_profiles | Profile/progression + UI dialogs/audio requests + mission state + Power Up + bindings | P resources |
| bw_game | Session, mission, actor, UI policy, game audio, presentation adapters, developer review | P runtime, profiles, timing; p images, render, scene, animation, audio_decode |
| bw_startup_playback | Game script fade/movie transport adapter | P game, video |
| bw_startup_presentation | Frontend + HUD + pause + shared Options sprite surfaces | P game; p images |
| bw_startup_framework | Concrete Bullet Witch application/options | P startup_playback; p startup_presentation |
| bw_save_storage_posix | Save-file storage (UNIX) | P resources |
| bw_cpu_backing_macos | Mac CPU provider | P memory |
| bw_save_directory_macos | Mac profile directory | P resources; p Foundation |
| bw_media_audio_macos | Movie PCM device output | P video; p AudioToolbox, CoreFoundation |
| bw_texture_allocation_macos | Metal texture allocation | P memory; p Metal, objc |
| bw_world_renderer_macos | Buffer allocation/world rendering/postprocess | P resources, render; p images, texture allocation, Metal, objc |
| bw_output_compositor_macos | Movie/sprite renderers + ordered output | P world renderer, video, render; p images, texture allocation, Metal, QuartzCore, objc |
| bw_startup_host_macos | Cocoa host + gamepad polling | P video, resources, input; p media audio, texture allocation, world renderer, compositor, Cocoa/Metal/QuartzCore/GameController |
| bw-startup | Mac executable/backend selection | p game application, host, FFmpeg video, POSIX saves, Mac save directory/CPU, FACT audio |
| bw-resource | Resource inspector CLI | p resources |

Pinned fetched libraries: Lua 5.0.2, FFmpeg 7.1.1 shared LGPL-only components,
libpng 1.6.59/zlib 1.3.2, FAudio/SDL commits. They live under ignored build
outputs; research references/vendors are distinct ignored material, not linked
runtime dependencies. Preserve their hashes, patches, licenses and selected
backends. The current backend selection is Apple-only for FACT despite the
underlying backend directory being technology-based; do not claim Linux runtime
coverage from a proposed move.

Test links: memory -> memory plus Mac capability/render targets on Apple;
profile -> profiles; image-font -> images; runtime -> startup_playback,
startup_framework,input plus Mac host; startup-corpus -> startup_presentation,
POSIX saves,audio_decode plus Mac CPU backing; resources -> resources,render,
scene,animation. resource-cli invokes bw-resource. startup-corpus is deliberately
not a default CTest test and requires purchased data. Policy tests run from the
Python gate rather than a CTest entry.

## Whole-repository boundary recommendations

Keep root `run`, README, BRIEF, IMPLEMENTATION, AGENTS/CLAUDE, all license/NOTICE
files, CMakeLists/Presets, .clang-format/.clang-tidy/.gitignore/.gitattributes,
.githooks and .vscode configurations discoverable in their existing roles.
No new product assets, scripts extracted from the game or vendors enter Git.

- `scripts/`: repository operations (`check.py`, `tidy.py`, `tooling.py`, pinned
  formatter wrapper, README). Move `tools/check_repository.py` into a clearly
  named repository-policy module here; update both hooks, check.py and policy
  tests atomically. This is optional hygiene, not a runtime ownership defect.
- `tools/resources/`: authored standalone resource-inspector executable source
  currently `tools/resource_inspector.cpp`. It calls public resource contracts;
  does not belong to game runtime or research evidence.
- `cmake/dependencies/`: pinned source acquisition and patches for Lua, FFmpeg,
  libpng/zlib, FAudio/SDL (current seven files reorganized as needed).
  Native shader build rules stay under `cmake/platform/macos/`; project target
  declarations grouped by engine/game/native/tools/tests. Avoid one library per
  directory; use targets to enforce actual compilation/selection boundaries.
- `tests/`: owner-mirrored contract files, shared fixtures, separately identified
  process/file integration workflows. The proposed exhaustive file grouping is
  below. Keep native in-process offscreen checks distinguishable from gameplay
  walkthroughs and synthetic corpus output.
- `docs/`: architecture, engineering process and dependency license notices;
  existing subsystem docs can be mechanically grouped under architecture after
  their links are updated. All 38 currently tracked documents remain authored
  documentation, not source or generated evidence.
- `research/`: retain all authored engine/subsystem reports, historical reports,
  source provenance, worklogs and reproducible investigation tools. The 177
  subsystem files indicate a future index/topic grouping opportunity, not a
  reason to move research utilities into runtime code. Research tool subgroups
  can separate archive/script/native/render/assets verification without changing
  provenance or command behavior. Current report count is an inventory only.
- Existing explicitly allowed evidence summaries remain exactly allowlisted.
  `research/evidence`, extracted data, references/vendor/source snapshots,
  experimental binaries and local outputs remain ignored. No wide ignore-rule
  relaxation accompanies moves. `research/experiments` includes only five
  reviewed Ghidra sources and two SteamStub runner sources; these remain
  investigation tooling, not public engine implementations.

`run` remains the single user entry and continues to work from another cwd.
If the executable is renamed from `bw-startup`, update launcher, shader copy,
checks, research verification callers and docs as one bounded change. Preserve
quiet capture logging, full-screen behavior, no stale executable after failed
build, and explicit developer-build OFF on ordinary launch. Moving sources also
requires preserving formatter/tidy discovery (`SOURCE_ROOTS` currently include,
src,tools,tests), compile database completeness, VS Code associations/vendor
exclusions, and no asset/evidence scans.

## Exhaustive current test-file grouping

Each of the 81 current files appears once below. This is an owner grouping
proposal; the game trace may refine actor subdirectories. Mixed suite drivers
(`runtime_test`, `resources_test`, `memory_test`) can retain calls temporarily,
then move driver-only code to a runners directory without losing registration.
`world_timing_test` is under game/mission because it checks engine clock plus
mission admission; isolate pure clock cases only when actually splitting it.
`gameplay_integration_test` presently uses in-process fixture contracts; its
filename does not by itself make it a new process/filesystem scenario.

- `tests/unit/engine/memory/`: `memory_test.cpp`, `device_budget_test.cpp`.
- `tests/unit/engine/resources/`: `resources_test.cpp`.
- `tests/unit/engine/formats/`: `model_test.cpp`, `audio_bank_test.cpp`, `stage_definitions_test.cpp`, `animation_definition_test.cpp`, `customization_test.cpp`, `image_font_test.cpp`, `texture_mips_test.cpp`.
- `tests/unit/engine/render/`: `render_scene_test.cpp`, `world_output_test.cpp`, `world_material_test.cpp`, `view_test.cpp`, `shadow_test.cpp`, `material_animation_test.cpp`.
- `tests/unit/engine/scene/`: `static_collision_test.cpp`, `irradiance_field_test.cpp`, `local_light_view_test.cpp`, `local_lights_test.cpp`, `reflection_field_test.cpp`, `shadow_casters_test.cpp`, `scalar_area_test.cpp`.
- `tests/unit/engine/animation/`: `skeletal_pose_test.cpp`, `secondary_motion_test.cpp`, `motion_events_test.cpp`, `shape_pose_test.cpp`.
- `tests/unit/engine/math/`: `math_test.cpp`.
- `tests/unit/engine/runtime/`: `runtime_test.cpp`, `task_memory_test.cpp`, `lua_memory_test.cpp`.
- `tests/unit/engine/video/`: `pcm_buffer_test.cpp`.
- `tests/unit/game/profile/`: `profile_test.cpp`, `profile_load_flow_test.cpp`, `progression_test.cpp`.
- `tests/unit/game/application/`: `startup_options_test.cpp`.
- `tests/unit/game/session/`: `startup_flows_test.cpp`, `startup_services_test.cpp`.
- `tests/unit/game/ui/frontend/`: `startup_playback_test.cpp`, `advertise_flow_test.cpp`, `title_flow_test.cpp`, `menu_flow_test.cpp`, `ranking_test.cpp`, `power_up_flow_test.cpp`.
- `tests/unit/game/ui/pause/`: `pause_menu_test.cpp`, `pause_help_test.cpp`.
- `tests/unit/game/ui/shared/`: `controller_options_test.cpp`, `menu_prompt_test.cpp`.
- `tests/unit/game/ui/hud/`: `ammunition_hud_test.cpp`, `gameplay_hud_test.cpp`.
- `tests/unit/game/ui/shared/` also owns `menu_audio_test.cpp` beside the source menu-audio policy; there is no proposed duplicate game audio subsystem.
- `tests/unit/game/camera/`: `initial_camera_test.cpp`, `player_camera_test.cpp`.
- `tests/unit/game/mission/`: `placed_collision_test.cpp`, `world_timing_test.cpp`, `gameplay_integration_test.cpp`.
- `tests/unit/game/presentation/`: `stage_shadow_test.cpp`.
- `tests/unit/game/actors/`: `motion_presentation_test.cpp`, `alicia_action_policy_test.cpp`, `alicia_reaction_policy_test.cpp`, `scripted_jump_policy_test.cpp`, `player_resources_test.cpp`, `spell_presentation_policy_test.cpp`, `aim_posture_test.cpp`.
- `tests/unit/platform/macos/`: `macos_cpu_backing_test.cpp`, `macos_texture_allocation_test.mm`, `macos_final_edge_test.mm`, `macos_texture_mips_test.mm`, `macos_scene_replay_test.mm`, `macos_output_compositor_test.mm`, `media_audio_memory_test.cpp`, `gamepad_targeting_test.mm`.
- `tests/integration/`: `resource_cli_test.py`, `test_repository_policy.py`, `startup_corpus.cpp`.
- `tests/fixtures/`: `media_memory_fixture.hpp`, `shape_fixture.hpp`, `model_fixture.hpp`, `camera_fixture.hpp`.
- `tests/`: `README.md`, `.clang-tidy` (test-fixture lint policy).

## Required versus optional work

Required before approving global layout: place concrete application policy with
its game owner; show all input/audio owners and backend edges; stop claiming a
profile/startup library owns unrelated permanent systems; implement any promised
developer compile exclusion; declare every allowed game/engine/native include
edge. A folder-only move cannot solve those.

Mechanical but coupled: update paths, target source lists, direct includes,
test drivers, shader names, analysis discovery and documentation together.
Optional housekeeping: split CMake recipe files and group research documentation
by topic. Neither is evidence of a better runtime architecture on its own.

No engine source outside concrete application/native entry currently includes
`game/...` based on the include scan; this is useful evidence of direction,
not proof that engine public data types are free of original-content semantics.
The format/content and host/input traces cover those deeper contracts.
