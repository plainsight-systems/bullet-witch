# Game responsibility grouping — slice 3

> **Slice 3 rejected in review.** The mechanical grouping is not architecture
> acceptance. Further production moves and edits are paused while the full
> implementation is traced and a revised physical structure is discussed.
> Earlier passing checks establish behavior preservation only.

October 26, 2026. Implemented and verified; user review pending. Mechanical grouping of the accepted slice 2 tree (`a5577bb`).
This exact manifest was recorded before moving production files. Destinations
implement the [approved plan](../../../docs/architecture/reorganization-plan.md);
this slice does not redesign target membership or dependencies.

## Ownership and naming decisions

- Session/root composition moves to `game/session`; `StartupSession` and its
  borrowed `StartupServices` interface become `GameSession` and `SessionServices`.
  Existing Lua task order and one session remain unchanged. Logo/check flow and
  playback names retain startup because that is their actual role.
- The one process loop and launch parser become `framework/application.*` and
  `application_options.cpp`, with `ApplicationOptions`, `ApplicationMemory`,
  `parse_application_options` and `run_application`. The portable native
  `StartupHost` contract and selected backend remain unchanged.
- Shared `PauseOptions*`/`PauseOption` names become `Options*`/`Option`, and the
  shared `pause_text` helper namespace becomes `localized_text`. Frontend and
  mission still use one Options transaction; Profile and DeviceOptions retain
  their original mutation/save authorities.
- Permanent frontend title/menu surfaces become `FrontendTitleSurface` and
  `FrontendMenuSurface`; loading presentation becomes `LoadingSurface`.
  Paths and their concrete types change together; abstract surfaces are retained.
- Slice 1 split owners move separately: MissionSimulation and StageInspection's
  composition facade to mission, StagePresentation to presentation, and
  PlayerAnimationReview plus stage_animation_review to developer. StageViewSource
  and its immutable mission asset observations belong to mission/stage_source.
  Static contact assembly (`placed_collision`) stays with mission content.
- Controller input/routing belongs to input; stored controller/device settings
  belong to settings. Audio cue/bank policy stays in audio with one runtime.
  Existing camera files stay where they are. No game implementation remains
  unclassified at the `src/game` root.
- Portable `include/bw` contracts, engine peer families, native backend, shaders,
  numeric algorithms, timing, allocation and owner special-member policies are
  unchanged. No compatibility forwarding headers or duplicate owners are added.

## Consultation

Design consultation retrieved Core SF.11 (`rs-contained`) and performance COPY.4
(`guidelines/copy-move/COPY.4-rule-of-five-rule-of-zero.md`). Includes must continue
to resolve directly to their original dependencies; moving/renaming declarations
must not introduce new copies, moves or lifetime behavior. Existing file contracts
and their original algorithm citations are preserved. Implementation separately queried both corpora and retrieved full Core SF.13
(`rs-portable-header-id`) and performance COPY.4 again. Cross-group includes now
use explicit forward-slash `game/<responsibility>/...` paths; same-directory
pairs retain local includes. Seven old parent-relative private format includes
were made source-root-relative because their files moved one level deeper.
No header declarations, special-member restrictions or include dependency
identity were changed beyond the approved role names.

## Exact move manifest

| Before | After |
| --- | --- |
| `src/game/advertise_flow.cpp` | `src/game/ui/frontend/advertise_flow.cpp` |
| `src/game/advertise_flow.hpp` | `src/game/ui/frontend/advertise_flow.hpp` |
| `src/game/aim_posture.cpp` | `src/game/actors/aim_posture.cpp` |
| `src/game/aim_posture.hpp` | `src/game/actors/aim_posture.hpp` |
| `src/game/alicia_action_owner.cpp` | `src/game/actors/alicia_action_owner.cpp` |
| `src/game/alicia_action_owner.hpp` | `src/game/actors/alicia_action_owner.hpp` |
| `src/game/alicia_action_policy.cpp` | `src/game/actors/alicia_action_policy.cpp` |
| `src/game/alicia_action_policy.hpp` | `src/game/actors/alicia_action_policy.hpp` |
| `src/game/alicia_motion_bindings.cpp` | `src/game/actors/alicia_motion_bindings.cpp` |
| `src/game/alicia_reaction_policy.cpp` | `src/game/actors/alicia_reaction_policy.cpp` |
| `src/game/alicia_reaction_policy.hpp` | `src/game/actors/alicia_reaction_policy.hpp` |
| `src/game/alicia_secondary_motion.cpp` | `src/game/actors/alicia_secondary_motion.cpp` |
| `src/game/alicia_secondary_motion.hpp` | `src/game/actors/alicia_secondary_motion.hpp` |
| `src/game/alicia_support_arm.cpp` | `src/game/actors/alicia_support_arm.cpp` |
| `src/game/alicia_support_arm.hpp` | `src/game/actors/alicia_support_arm.hpp` |
| `src/game/ammunition_hud.cpp` | `src/game/ui/hud/ammunition_hud.cpp` |
| `src/game/ammunition_hud.hpp` | `src/game/ui/hud/ammunition_hud.hpp` |
| `src/game/animation_review.hpp` | `src/game/developer/animation_review.hpp` |
| `src/game/camera/initial_camera.cpp` | `src/game/camera/initial_camera.cpp` |
| `src/game/camera/initial_camera.hpp` | `src/game/camera/initial_camera.hpp` |
| `src/game/camera/player_camera.cpp` | `src/game/camera/player_camera.cpp` |
| `src/game/camera/player_camera.hpp` | `src/game/camera/player_camera.hpp` |
| `src/game/character_animations.cpp` | `src/game/actors/character_animations.cpp` |
| `src/game/character_animations.hpp` | `src/game/actors/character_animations.hpp` |
| `src/game/character_audio.cpp` | `src/game/audio/character_audio.cpp` |
| `src/game/character_audio.hpp` | `src/game/audio/character_audio.hpp` |
| `src/game/character_audio_compatibility.inc` | `src/game/audio/character_audio_compatibility.inc` |
| `src/game/character_motion_player.cpp` | `src/game/actors/character_motion_player.cpp` |
| `src/game/character_motion_player.hpp` | `src/game/actors/character_motion_player.hpp` |
| `src/game/controller_input.cpp` | `src/game/input/controller_input.cpp` |
| `src/game/controller_input.hpp` | `src/game/input/controller_input.hpp` |
| `src/game/controller_options.cpp` | `src/game/ui/shared/controller_options.cpp` |
| `src/game/controller_options.hpp` | `src/game/ui/shared/controller_options.hpp` |
| `src/game/controller_options_surface.cpp` | `src/game/ui/shared/controller_options_surface.cpp` |
| `src/game/controller_options_surface.hpp` | `src/game/ui/shared/controller_options_surface.hpp` |
| `src/game/controller_settings.cpp` | `src/game/settings/controller_settings.cpp` |
| `src/game/controller_settings.hpp` | `src/game/settings/controller_settings.hpp` |
| `src/game/developer_review_flow.cpp` | `src/game/developer/developer_review_flow.cpp` |
| `src/game/developer_review_flow.hpp` | `src/game/developer/developer_review_flow.hpp` |
| `src/game/developer_views.hpp` | `src/game/developer/developer_views.hpp` |
| `src/game/device_options.cpp` | `src/game/settings/device_options.cpp` |
| `src/game/device_options.hpp` | `src/game/settings/device_options.hpp` |
| `src/game/facial_animation.cpp` | `src/game/actors/facial_animation.cpp` |
| `src/game/facial_animation.hpp` | `src/game/actors/facial_animation.hpp` |
| `src/game/frontend.cpp` | `src/game/ui/frontend/frontend.cpp` |
| `src/game/frontend.hpp` | `src/game/ui/frontend/frontend.hpp` |
| `src/game/frontend_audio.cpp` | `src/game/audio/frontend_audio.cpp` |
| `src/game/frontend_audio.hpp` | `src/game/audio/frontend_audio.hpp` |
| `src/game/frontend_options.hpp` | `src/game/ui/shared/frontend_options.hpp` |
| `src/game/frontend_options_surface.cpp` | `src/game/ui/shared/frontend_options_surface.cpp` |
| `src/game/frontend_options_surface.hpp` | `src/game/ui/shared/frontend_options_surface.hpp` |
| `src/game/game_audio.cpp` | `src/game/audio/game_audio.cpp` |
| `src/game/game_audio.hpp` | `src/game/audio/game_audio.hpp` |
| `src/game/gameplay_hud.cpp` | `src/game/ui/hud/gameplay_hud.cpp` |
| `src/game/gameplay_hud.hpp` | `src/game/ui/hud/gameplay_hud.hpp` |
| `src/game/gameplay_hud_surface.cpp` | `src/game/ui/hud/gameplay_hud_surface.cpp` |
| `src/game/gameplay_hud_surface.hpp` | `src/game/ui/hud/gameplay_hud_surface.hpp` |
| `src/game/help_input_icons.cpp` | `src/game/ui/shared/help_input_icons.cpp` |
| `src/game/help_input_icons.hpp` | `src/game/ui/shared/help_input_icons.hpp` |
| `src/game/hud_sprite.hpp` | `src/game/ui/hud/hud_sprite.hpp` |
| `src/game/initial_placement_shim.cpp` | `src/game/mission/initial_placement_shim.cpp` |
| `src/game/initial_placement_shim.hpp` | `src/game/mission/initial_placement_shim.hpp` |
| `src/game/input_icon.cpp` | `src/game/ui/shared/input_icon.cpp` |
| `src/game/input_icon.hpp` | `src/game/ui/shared/input_icon.hpp` |
| `src/game/menu_audio.cpp` | `src/game/audio/menu_audio.cpp` |
| `src/game/menu_audio.hpp` | `src/game/audio/menu_audio.hpp` |
| `src/game/menu_dialog.cpp` | `src/game/ui/shared/menu_dialog.cpp` |
| `src/game/menu_dialog.hpp` | `src/game/ui/shared/menu_dialog.hpp` |
| `src/game/menu_flow.cpp` | `src/game/ui/frontend/menu_flow.cpp` |
| `src/game/menu_flow.hpp` | `src/game/ui/frontend/menu_flow.hpp` |
| `src/game/menu_input.hpp` | `src/game/ui/shared/menu_input.hpp` |
| `src/game/menu_prompt.cpp` | `src/game/ui/shared/menu_prompt.cpp` |
| `src/game/menu_prompt.hpp` | `src/game/ui/shared/menu_prompt.hpp` |
| `src/game/menu_sound_requests.cpp` | `src/game/audio/menu_sound_requests.cpp` |
| `src/game/menu_surface.hpp` | `src/game/ui/frontend/menu_surface.hpp` |
| `src/game/mission_control.cpp` | `src/game/mission/mission_control.cpp` |
| `src/game/mission_control.hpp` | `src/game/mission/mission_control.hpp` |
| `src/game/mission_driver.cpp` | `src/game/mission/mission_driver.cpp` |
| `src/game/mission_driver.hpp` | `src/game/mission/mission_driver.hpp` |
| `src/game/mission_gameplay.cpp` | `src/game/mission/mission_gameplay.cpp` |
| `src/game/mission_gameplay.hpp` | `src/game/mission/mission_gameplay.hpp` |
| `src/game/mission_help_state.cpp` | `src/game/mission/mission_help_state.cpp` |
| `src/game/mission_help_state.hpp` | `src/game/mission/mission_help_state.hpp` |
| `src/game/mission_interaction.cpp` | `src/game/mission/mission_interaction.cpp` |
| `src/game/mission_interaction.hpp` | `src/game/mission/mission_interaction.hpp` |
| `src/game/mission_loading.cpp` | `src/game/mission/mission_loading.cpp` |
| `src/game/mission_loading.hpp` | `src/game/mission/mission_loading.hpp` |
| `src/game/mission_package_inputs.cpp` | `src/game/mission/mission_package_inputs.cpp` |
| `src/game/mission_pause_presentation.hpp` | `src/game/ui/pause/mission_pause_presentation.hpp` |
| `src/game/mission_pause_surface.cpp` | `src/game/ui/pause/mission_pause_surface.cpp` |
| `src/game/mission_pause_surface.hpp` | `src/game/ui/pause/mission_pause_surface.hpp` |
| `src/game/mission_player_state.cpp` | `src/game/mission/mission_player_state.cpp` |
| `src/game/mission_player_state.hpp` | `src/game/mission/mission_player_state.hpp` |
| `src/game/mission_simulation.cpp` | `src/game/mission/mission_simulation.cpp` |
| `src/game/mission_simulation.hpp` | `src/game/mission/mission_simulation.hpp` |
| `src/game/mission_transition.hpp` | `src/game/mission/mission_transition.hpp` |
| `src/game/motion_presentation.cpp` | `src/game/actors/motion_presentation.cpp` |
| `src/game/motion_presentation.hpp` | `src/game/actors/motion_presentation.hpp` |
| `src/game/npc_palette.cpp` | `src/game/presentation/npc_palette.cpp` |
| `src/game/npc_palette.hpp` | `src/game/presentation/npc_palette.hpp` |
| `src/game/pause_help.cpp` | `src/game/ui/pause/pause_help.cpp` |
| `src/game/pause_help.hpp` | `src/game/ui/pause/pause_help.hpp` |
| `src/game/pause_help_surface.cpp` | `src/game/ui/pause/pause_help_surface.cpp` |
| `src/game/pause_help_surface.hpp` | `src/game/ui/pause/pause_help_surface.hpp` |
| `src/game/pause_menu.cpp` | `src/game/ui/pause/pause_menu.cpp` |
| `src/game/pause_menu.hpp` | `src/game/ui/pause/pause_menu.hpp` |
| `src/game/pause_menu_surface.cpp` | `src/game/ui/pause/pause_menu_surface.cpp` |
| `src/game/pause_menu_surface.hpp` | `src/game/ui/pause/pause_menu_surface.hpp` |
| `src/game/pause_options.cpp` | `src/game/ui/shared/options.cpp` |
| `src/game/pause_options.hpp` | `src/game/ui/shared/options.hpp` |
| `src/game/pause_options_surface.cpp` | `src/game/ui/shared/options_surface.cpp` |
| `src/game/pause_options_surface.hpp` | `src/game/ui/shared/options_surface.hpp` |
| `src/game/pause_surface_text.hpp` | `src/game/ui/shared/localized_text.hpp` |
| `src/game/placed_collision.cpp` | `src/game/mission/placed_collision.cpp` |
| `src/game/placed_collision.hpp` | `src/game/mission/placed_collision.hpp` |
| `src/game/placed_lighting.cpp` | `src/game/presentation/placed_lighting.cpp` |
| `src/game/placed_lighting.hpp` | `src/game/presentation/placed_lighting.hpp` |
| `src/game/player_actor.cpp` | `src/game/actors/player_actor.cpp` |
| `src/game/player_actor.hpp` | `src/game/actors/player_actor.hpp` |
| `src/game/player_animation_review.hpp` | `src/game/developer/player_animation_review.hpp` |
| `src/game/player_cockpit.cpp` | `src/game/ui/hud/player_cockpit.cpp` |
| `src/game/player_cockpit.hpp` | `src/game/ui/hud/player_cockpit.hpp` |
| `src/game/player_controller.cpp` | `src/game/actors/player_controller.cpp` |
| `src/game/player_controller.hpp` | `src/game/actors/player_controller.hpp` |
| `src/game/player_input_routing.cpp` | `src/game/input/player_input_routing.cpp` |
| `src/game/player_input_routing.hpp` | `src/game/input/player_input_routing.hpp` |
| `src/game/player_resources.cpp` | `src/game/actors/player_resources.cpp` |
| `src/game/player_resources.hpp` | `src/game/actors/player_resources.hpp` |
| `src/game/player_targeting.cpp` | `src/game/actors/player_targeting.cpp` |
| `src/game/player_targeting.hpp` | `src/game/actors/player_targeting.hpp` |
| `src/game/power_up_flow.cpp` | `src/game/ui/frontend/power_up_flow.cpp` |
| `src/game/power_up_flow.hpp` | `src/game/ui/frontend/power_up_flow.hpp` |
| `src/game/power_up_presentation.hpp` | `src/game/ui/frontend/power_up_presentation.hpp` |
| `src/game/power_up_surface.cpp` | `src/game/ui/frontend/power_up_surface.cpp` |
| `src/game/power_up_surface.hpp` | `src/game/ui/frontend/power_up_surface.hpp` |
| `src/game/profile.cpp` | `src/game/profile/profile.cpp` |
| `src/game/profile.hpp` | `src/game/profile/profile.hpp` |
| `src/game/profile_load_flow.cpp` | `src/game/profile/profile_load_flow.cpp` |
| `src/game/profile_load_flow.hpp` | `src/game/profile/profile_load_flow.hpp` |
| `src/game/progression.cpp` | `src/game/profile/progression.cpp` |
| `src/game/progression.hpp` | `src/game/profile/progression.hpp` |
| `src/game/progression_save.cpp` | `src/game/profile/progression_save.cpp` |
| `src/game/progression_save.hpp` | `src/game/profile/progression_save.hpp` |
| `src/game/ranking_flow.cpp` | `src/game/ui/frontend/ranking_flow.cpp` |
| `src/game/ranking_flow.hpp` | `src/game/ui/frontend/ranking_flow.hpp` |
| `src/game/ranking_surface.cpp` | `src/game/ui/frontend/ranking_surface.cpp` |
| `src/game/ranking_surface.hpp` | `src/game/ui/frontend/ranking_surface.hpp` |
| `src/game/resident_model.hpp` | `src/game/presentation/resident_model.hpp` |
| `src/game/root_driver.cpp` | `src/game/session/root_driver.cpp` |
| `src/game/root_driver.hpp` | `src/game/session/root_driver.hpp` |
| `src/game/scripted_jump_policy.cpp` | `src/game/actors/scripted_jump_policy.cpp` |
| `src/game/scripted_jump_policy.hpp` | `src/game/actors/scripted_jump_policy.hpp` |
| `src/game/source_random.hpp` | `src/game/compatibility/source_random.hpp` |
| `src/game/spell_presentation_policy.cpp` | `src/game/actors/spell_presentation_policy.cpp` |
| `src/game/spell_presentation_policy.hpp` | `src/game/actors/spell_presentation_policy.hpp` |
| `src/game/spell_selection.cpp` | `src/game/actors/spell_selection.cpp` |
| `src/game/spell_selection.hpp` | `src/game/actors/spell_selection.hpp` |
| `src/game/sprite_color.hpp` | `src/game/ui/shared/sprite_color.hpp` |
| `src/game/sprite_text.cpp` | `src/game/ui/shared/sprite_text.cpp` |
| `src/game/sprite_text.hpp` | `src/game/ui/shared/sprite_text.hpp` |
| `src/game/stage_animation_review.cpp` | `src/game/developer/stage_animation_review.cpp` |
| `src/game/stage_environment.cpp` | `src/game/presentation/stage_environment.cpp` |
| `src/game/stage_environment.hpp` | `src/game/presentation/stage_environment.hpp` |
| `src/game/stage_inspection.cpp` | `src/game/mission/stage_inspection.cpp` |
| `src/game/stage_inspection.hpp` | `src/game/mission/stage_inspection.hpp` |
| `src/game/stage_presentation.cpp` | `src/game/presentation/stage_presentation.cpp` |
| `src/game/stage_presentation.hpp` | `src/game/presentation/stage_presentation.hpp` |
| `src/game/stage_shadow.cpp` | `src/game/presentation/stage_shadow.cpp` |
| `src/game/stage_shadow.hpp` | `src/game/presentation/stage_shadow.hpp` |
| `src/game/stage_source.hpp` | `src/game/mission/stage_source.hpp` |
| `src/game/startup_flows.cpp` | `src/game/session/startup_flows.cpp` |
| `src/game/startup_flows.hpp` | `src/game/session/startup_flows.hpp` |
| `src/game/startup_loading_surface.cpp` | `src/game/ui/frontend/loading_surface.cpp` |
| `src/game/startup_loading_surface.hpp` | `src/game/ui/frontend/loading_surface.hpp` |
| `src/game/startup_menu_surface.cpp` | `src/game/ui/frontend/frontend_menu_surface.cpp` |
| `src/game/startup_menu_surface.hpp` | `src/game/ui/frontend/frontend_menu_surface.hpp` |
| `src/game/startup_playback.cpp` | `src/game/ui/frontend/startup_playback.cpp` |
| `src/game/startup_playback.hpp` | `src/game/ui/frontend/startup_playback.hpp` |
| `src/game/startup_services.cpp` | `src/game/session/session_services.cpp` |
| `src/game/startup_services.hpp` | `src/game/session/session_services.hpp` |
| `src/game/startup_title_surface.cpp` | `src/game/ui/frontend/frontend_title_surface.cpp` |
| `src/game/startup_title_surface.hpp` | `src/game/ui/frontend/frontend_title_surface.hpp` |
| `src/game/title_flows.cpp` | `src/game/ui/frontend/title_flows.cpp` |
| `src/game/title_flows.hpp` | `src/game/ui/frontend/title_flows.hpp` |
| `src/game/title_surface.hpp` | `src/game/ui/frontend/title_surface.hpp` |
| `src/framework/startup_application.cpp` | `src/framework/application.cpp` |
| `src/framework/startup_application.hpp` | `src/framework/application.hpp` |
| `src/framework/startup_options.cpp` | `src/framework/application_options.cpp` |

## Verification

- Mechanical verification compares all 446 authored C++ source/header/include
  files against the accepted baseline, preserving string literals and operators.
  After the approved identifier renames, include relocation and formatter sorting
  of adjacent `using` declarations, token streams match. Include dependencies
  resolve to the same original targets. All 184 moves are present; no game file
  remains at the ungrouped root. This is a one-time migration audit, not a new
  integration-test scenario.
- CMake comparison confirms only source paths changed: existing target names,
  memberships, link relationships and test identities are preserved for slice 4.
- Final `python3 scripts/check.py all` exits 0: native Debug build, all six CTest
  groups (including existing actual offscreen Metal checks), 242 fatal clang-tidy
  compilation-command checks, pinned formatting, five policy tests and repository
  content guard. No test or lint suppression was added.
- Purchased-data `startup-corpus` exits 0: original root/menu routing, One Shot
  Kill admission, all four weapon/action families, pause/costume/reentry and
  zero migrated live payload after teardown. Its renderer/audio are fixtures;
  this establishes integrated data/state behavior, not native visual/audio parity.
- Current documentation links and whitespace checks pass. Test identities and
  their existing scenarios remain unchanged; no new path-only tests were added.
- No visible game launch, native walkthrough, commit or push has been performed.
  The costume pink block and frontend A/B prompts remain separate UI backlog.

Commands: `python3 scripts/check.py all`, then
`build/host-debug/startup-corpus "$BULLET_WITCH_GAME_DIR"` using the read-only
purchased installation. Generated gate/corpus logs and mechanical verification
live under ignored `research/evidence/reconstruction/game-grouping/`.
