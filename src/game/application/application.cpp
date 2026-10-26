// Responsibility grouping preserves the existing contract and owner policies.
// Design Core SF.11 (#rs-contained), implementation SF.13
// (#rs-portable-header-id), and both-phase performance COPY.4
// (guidelines/copy-move/COPY.4-rule-of-five-rule-of-zero.md) keep direct
// includes portable and preserve special members without extra payload copies.
// Application composition owns platform services and stable game lifetimes.
// Sample/publish engine presentation timing, adapt neutral platform input,
// advance media, delegate game phases and present native output.
// MissionGameplay owns pause/action ordering and PlayerCockpit owns
// resource-to-HUD policy; this layer never chooses ring actions, masks gameplay
// commands or interprets resource receipts. A completed bounded resource
// transaction requests deadline rebasing without exposing its game policy.
// Resources precede Stage, which precedes its gameplay coordinator, preserving
// reverse-order retirement. Confirmed retirement destroys those borrowers and
// resource owners before notifying the root script; no victory is fabricated.
// Build configuration alone enables developer views. Existing loading callbacks
// pump presentation and the existing audio owner without admitting world ticks.
// CP.2 (#rconc-races) keeps loading service calls on the same owner thread.
// Core I.1 (#ri-explicit), C.9
// (#rc-private), implementation E.4
// (#re-design-invariants), and both-phase performance MEM.9 (guidelines/memory/
// MEM.9-embedded-allocate-at-init.md) retain explicit phases and fixed
// per-frame handoffs. See gameplay-integration-boundaries.md and
// world-timing.md.
#include "application.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <utility>

#include "bw/timing/frame_clock.hpp"
#include "bw/timing/world_clock.hpp"
#include "game/actors/alicia/player_resources.hpp"
#include "game/mission/loading/mission_loading.hpp"
#include "game/mission/mission_control.hpp"
#include "game/mission/mission_gameplay.hpp"
#include "game/player/input/controller_input.hpp"
#include "game/session/compatibility/source_random.hpp"
#include "game/session/session_services.hpp"
#include "game/ui/audio/frontend_audio.hpp"
#include "game/ui/frontend/frontend_menu_surface.hpp"
#include "game/ui/frontend/frontend_title_surface.hpp"
#include "game/ui/frontend/ranking_surface.hpp"
#include "game/ui/hud/gameplay_hud_surface.hpp"
#include "game/ui/loading/loading_surface.hpp"
#include "game/ui/options/frontend_options_surface.hpp"
#include "game/ui/pause/mission_pause_surface.hpp"
#include "game/ui/power_up/power_up_surface.hpp"
#include "game/ui/startup/startup_playback.hpp"
#include "game/world/initial_placement_shim.hpp"
#include "game/world/world_runtime.hpp"

namespace bw::game {

int run_application(const ApplicationOptions& configuration,
                    platform::WindowHost& host, platform::Files& files,
                    platform::SaveStorage& saves,
                    video::Player::OpenDecoder open_decoder,
                    const ApplicationMemory& memory,
                    game::GameAudioFactory& audio_factory) {
  if (!memory.resources || !memory.runtime || !memory.media) {
    throw std::invalid_argument(
        "Startup requires resource, runtime and media domains");
  }
  // Mount read-only catalogs before constructing the owners that request from
  // them.
  bw::resources::CatalogSet catalogs(memory.resources);
  const auto catalog_path = configuration.game / "data/data/stable/stable.rat";
  constexpr int kStableCatalogPriority = 30;
  catalogs.mount(
      kStableCatalogPriority, catalog_path,
      bw::content::open_catalog(files, catalog_path, memory.resources));
  constexpr unsigned kSteamCostumePackages = 5;
  for (unsigned index = 1; index <= kSteamCostumePackages; ++index) {
    const auto name = std::string("PW") + std::to_string(index);
    const auto path =
        configuration.game / "data/concept" / name / (name + ".rat");
    if (std::filesystem::exists(path)) {
      catalogs.mount(static_cast<int>(index), path,
                     bw::content::open_catalog(files, path, memory.resources));
    }
  }
  // Original concept-menu text lives in each installed concept catalog.
  // Expose only that text identity; scene aliases remain mission-owned.
  constexpr std::array<unsigned, 3> kConceptBases{0, 16, 32};
  constexpr unsigned kConceptsPerGroup = 6;
  constexpr int kConceptTextPriority = 40;
  for (const auto base : kConceptBases) {
    for (unsigned offset = 0; offset < kConceptsPerGroup; ++offset) {
      const auto id = base + offset;
      const auto digits = (id < 10 ? "0" : "") + std::to_string(id);
      const auto name = "cm" + digits + "_00";
      const auto path =
          configuration.game / "data/concept" / name / (name + ".rat");
      const auto text_name = "sprite.text.cmtxt" + digits;
      const auto image_name = "sprite.cm" + digits;
      catalogs.mount_resources(
          kConceptTextPriority, path,
          bw::content::open_catalog(files, path, memory.resources),
          std::array<std::string_view, 2>{text_name, image_name});
    }
  }
  bw::resources::ResourceService resources(std::move(catalogs), files,
                                           memory.resources);
  // Declaration order keeps services alive through reverse-order session
  // teardown.
  bw::game::StartupPlayback playback(host, configuration.game,
                                     std::move(open_decoder), memory.media);
  // Original selector seeds MSVCRT rand with platform milliseconds per title
  // activation, then chooses rand()%10. Preserve the algorithm with a portable
  // unsigned seed; exact boot epoch is host-specific.
  bw::game::FrontendTitleSurface title_surface(resources, host, [] {
    const auto ticks = std::chrono::duration_cast<std::chrono::milliseconds>(
                           std::chrono::steady_clock::now().time_since_epoch())
                           .count();
    constexpr std::uint32_t multiplier = 214013;
    constexpr std::uint32_t increment = 2531011;
    constexpr std::uint32_t random_mask = 0x7fff;
    constexpr unsigned random_shift = 16;
    constexpr unsigned loading_count = 10;
    const auto seed =
        static_cast<std::uint32_t>(ticks) * multiplier + increment;
    return static_cast<unsigned>((seed >> random_shift) & random_mask) %
           loading_count;
  });
  // One native game mixer survives screen and mission tasks. Only menu cues
  // prepare here; voice/common expansion remains at mission loading.
  constexpr audio::Limits kGameAudioLimits{
      .banks = 3, .cues = 128, .decoded_samples = 60'000'000};
  game::GameAudio game_audio(files, resources.catalogs(), memory.media,
                             audio_factory, kGameAudioLimits);
  game::FrontEndAudio frontend_audio(*game_audio.output(), [&] {
    game_audio.prepare_frontend_music(files, configuration.game / "data/sound",
                                      memory.media);
  });
  bw::game::Profile profile;
  game::DeviceOptions device_options(saves);
  const auto device_status = device_options.load();
  if (device_status != platform::SaveStatus::ok &&
      device_status != platform::SaveStatus::missing) {
    throw std::runtime_error("Native device options could not be loaded");
  }
  bw::game::FrontendMenuSurface menu_surface(
      resources, host,
      files.read(configuration.game / "data/menu/9952", memory.resources),
      files.read(configuration.game / "data/menu/9900", memory.resources));
  game::PowerUpSurface power_up_surface(
      resources, host,
      files.read(configuration.game / "data/menu/9900", memory.resources));
  game::FrontendOptionsSurface frontend_options_surface(
      resources, host,
      files.read(configuration.game / "data/menu/9900", memory.resources),
      files.read(configuration.game / "data/menu/9952", memory.resources));
  game::RankingSurface ranking_surface(
      resources, host,
      files.read(configuration.game / "data/menu/9900", memory.resources));
  bw::game::FrontEnd frontend(
      profile, saves, playback, menu_surface, {}, &power_up_surface,
      &frontend_audio,
      game::FrontendOptionsServices{
          {profile.settings_control(), profile.settings_save_control(saves),
           device_options.control(), device_options.save_control(),
           profile.record_save_control(saves)},
          frontend_options_surface},
      &ranking_surface);
  bw::game::GameSession session(playback, &title_surface, &frontend,
                                memory.runtime, &frontend_audio);
  session.start_root(resources, 1);
  game::LoadingSurface loading_surface(resources, host);
  // Loading callbacks retain this presentation state. Declare it before all
  // mission holders so it remains alive until their callbacks are retired.
  const auto started = std::chrono::steady_clock::now();
  const auto deadline =
      configuration.maximum_seconds
          ? started + std::chrono::seconds(*configuration.maximum_seconds)
          : std::chrono::steady_clock::time_point::max();
  constexpr auto target_period = std::chrono::nanoseconds(16'666'667);
  timing::FrameClock frame_clock(started, target_period);
  game::ControllerInput controller_input;
  std::optional<std::chrono::steady_clock::time_point> loading_started;
  auto loading_presented = started;
  bool loading_close_requested = false;
  bool inspected_stage = false;
  // Mission owners retire in reverse order: Lua polls, load data, then
  // catalogs.
  std::optional<resources::ResourceService> mission_resources;
  std::optional<game::MissionLoading> mission_loading;
  std::optional<game::MissionPlayerState> mission_player;
  std::optional<game::MissionControl> mission_control;
  std::optional<world::WorldClock> world_clock;
  std::optional<game::MissionDriver> mission;
  game::SourceRandom random;
  std::optional<game::PlayerResources> player_resources;
  game::GameplayHudSurface hud_surface(resources, host);
  game::MissionPauseSurface pause_surface(
      resources, host, menu_surface,
      files.read(configuration.game / "data/menu/9900", memory.resources),
      files.read(configuration.game / "data/menu/9952", memory.resources));
  std::optional<game::WorldRuntime> stage_inspection;
  std::optional<game::MissionGameplay> mission_gameplay;
  std::optional<game::MissionPhase> reported_mission_phase;
  // Completed asset operations may pump only loading presentation. They never
  // reenter Lua, inspect a partial world or transfer native ownership threads.
  // This is Doom 3's bounded loading-screen update contract; see research.
  const auto loading_pulse = [&] {
    if (!mission_loading || !mission_loading->loading_scene().active ||
        inspected_stage) {
      return;
    }
    const auto now = std::chrono::steady_clock::now();
    const bool first = !loading_started;
    if (first) loading_started = now;
    if (!first && now - loading_presented < target_period) return;
    // Loading presents through the same frame clock but admits no world tick.
    // Input repeat and renderer adaptation consume this presentation snapshot.
    const auto loading_timing = frame_clock.sample(now);
    host.set_frame_timing(loading_timing);
    // Departure fades and native segment handoff continue through loading.
    // This consumes the existing presentation sample, never a world tick.
    frontend_audio.advance(loading_timing.presentation_seconds);
    game_audio.apply_mix(
        {static_cast<std::uint16_t>(profile.music_volume()),
         static_cast<std::uint16_t>(profile.effects_volume())});
    game_audio.update();
    // Loading consumes the same game mapping history without delivering
    // actions.
    loading_close_requested |=
        controller_input
            .map(host.pump(true),
                 profile.settings_control().controller_settings(),
                 device_options.snapshot(), loading_timing.wall_seconds,
                 loading_timing.presentation_seconds)
            .close_requested;
    const auto loading_opacity = loading_surface.show(
        mission_loading->loading_scene().item,
        std::chrono::duration<double>(now - *loading_started).count());
    (void)host.draw(loading_opacity);
    loading_presented = now;
  };
  std::size_t reported_movies = 0;
  float longest_tick = 0;
  double longest_pump = 0;
  while (std::chrono::steady_clock::now() < deadline) {
    std::this_thread::sleep_until(frame_clock.deadline());
    const auto timing = frame_clock.sample(std::chrono::steady_clock::now());
    host.set_frame_timing(timing);
    const float wall_delta = timing.wall_seconds;
    const float presentation_delta = timing.presentation_seconds;
    longest_tick = std::max(longest_tick, wall_delta);
    const auto pump_started = std::chrono::steady_clock::now();
    const auto controller_settings =
        profile.settings_control().controller_settings();
    const auto input = controller_input.map(
        host.pump(session.window_close_enabled()), controller_settings,
        device_options.snapshot(), wall_delta, presentation_delta);
    longest_pump = std::max(longest_pump,
                            std::chrono::duration<double>(
                                std::chrono::steady_clock::now() - pump_started)
                                .count());
    if (input.close_requested && session.window_close_enabled()) {
      if (configuration.verify) {
        throw std::runtime_error(
            "Startup verification interrupted by window close");
      }
      return 0;
    }
    game::MissionFrameDisposition mission_frame;
    if (mission_gameplay) {
      mission_frame = mission_gameplay->begin(
          presentation_delta,
          {.player = {.movement = input.movement,
                      .look = input.look,
                      .mouse_look = input.mouse_look,
                      .turn_speed = input.turn_speed,
                      .mouse_bias = input.mouse_bias,
                      .keyboard_magic_pressed = input.keyboard_magic_pressed,
                      .keyboard_cycle_forward_pressed =
                          input.keyboard_cycle_forward_pressed,
                      .weapon_cycle_backward_pressed =
                          input.weapon_cycle_backward_pressed,
                      .fire_trigger = input.fire_trigger,
                      .evade_held = input.evade_held,
                      .crouch_pressed = input.crouch_pressed,
                      .reload_pressed = input.reload_pressed,
                      .attack_pressed = input.attack_pressed,
                      .weapon_cycle_pressed = input.weapon_cycle_pressed,
                      .aim_mode_pressed = input.aim_mode_pressed,
                      .shoulder_left_pressed = input.shoulder_left_pressed,
                      .shoulder_right_pressed = input.shoulder_right_pressed,
                      .shoulder_left_held = input.shoulder_left_held,
                      .shoulder_right_held = input.shoulder_right_held,
                      .face_north_pressed = input.face_north_pressed,
                      .face_east_pressed = input.face_east_pressed,
                      .face_south_pressed = input.face_south_pressed,
                      .face_west_pressed = input.face_west_pressed},
           .pause_pressed = input.pause_pressed,
           .menu = {input.pressed_actions, input.repeated_actions,
                    input.menu_vertical_axis},
           .physical = input.physical,
           .help_icons = game::help_input_icons(controller_settings,
                                                input.controller_connected)});
      if (mission_frame.resource_transaction) {
        frame_clock.rebase(std::chrono::steady_clock::now());
      }
    }
    // Advance transport before game policy observes completion and selects
    // draws.
    playback.advance(
        {.wall_delta = wall_delta, .game_delta = presentation_delta});
    if (configuration.verify &&
        playback.completed_movies() != reported_movies) {
      reported_movies = playback.completed_movies();
      const auto& stats = playback.statistics();
      std::cerr << "Movie " << reported_movies
                << " wall_seconds=" << stats.wall_seconds
                << " media_seconds=" << stats.media_seconds
                << " video_end=" << stats.video_end_seconds
                << " audio_end=" << stats.audio_end_seconds
                << " decoded_frames=" << stats.decoded_frames
                << " uploaded_frames=" << stats.uploaded_frames
                << " dropped_frames="
                << (stats.decoded_frames - stats.uploaded_frames)
                << " source_audio_frames=" << stats.source_audio_frames
                << " queued_audio_frames=" << stats.queued_audio_frames
                << " played_audio_frames=" << stats.played_audio_frames
                << " underruns=" << stats.underruns
                << " final_frame_presented=" << stats.final_frame_presented
                << " silence_frames=" << stats.silence_frames
                << " max_clock_step=" << stats.largest_clock_step
                << " audio_starvation=" << stats.audio_starvation_seconds
                << " max_wall_tick=" << longest_tick
                << " max_pump=" << longest_pump
                << " prebuffer_seconds=" << stats.prebuffer_seconds
                << " max_pts_gap=" << stats.largest_audio_timestamp_gap
                << " max_draw=" << stats.largest_draw_seconds << '\n';
      if (stats.source_audio_frames != stats.played_audio_frames ||
          stats.queued_audio_frames != stats.played_audio_frames ||
          stats.underruns != 0 || !stats.final_frame_presented) {
        throw std::runtime_error(
            "Startup movie did not satisfy PCM/presentation acceptance");
      }
      longest_tick = 0;
      longest_pump = 0;
    }
    // Game owns the admitted action/resource/presentation ordering; media
    // transport remains a separate service between input and world phases.
    if (mission_gameplay) mission_gameplay->advance();
    // Service the one device even outside missions and during world pause.
    game_audio.apply_mix(
        {static_cast<std::uint16_t>(profile.music_volume()),
         static_cast<std::uint16_t>(profile.effects_volume())});
    game_audio.update();
    playback.set_music_volume(profile.music_volume());
    if (mission_frame.transition != game::MissionTransition::none) {
      if (!mission_loading) {
        throw std::logic_error("Mission transition has no committed resources");
      }
      // Capture the live appearance while its owner exists. Reentry carries
      // this value across teardown without changing Profile or save data.
      const game::MissionTransitionRequest transition{
          mission_frame.transition, mission_loading->current_player_costume()};
      session.validate_mission_transition(transition);
      // A confirmed game request retires borrowers before their authorities.
      // Retirement releases the root wait; restart/checkpoint preserve it and
      // let the existing entry path build all owners again. No actor reset.
      mission_gameplay.reset();
      pause_surface.end();
      stage_inspection.reset();
      hud_surface.end();
      player_resources.reset();
      game_audio.retire_mission();
      mission.reset();
      world_clock.reset();
      mission_control.reset();
      mission_player.reset();
      mission_loading.reset();
      mission_resources.reset();
      loading_surface.end();
      loading_started.reset();
      reported_mission_phase.reset();
      inspected_stage = false;
      session.complete_mission_transition(transition);
      frame_clock.rebase(std::chrono::steady_clock::now());
    }
    const auto actions =
        configuration.verify || stage_inspection || mission_frame.ui_consumed
            ? 0U
            : input.pressed_actions;
    const auto result = session.advance(
        {presentation_delta, actions, input.controller_connected},
        configuration.verify || stage_inspection || mission_frame.ui_consumed
            ? 0U
            : input.repeated_actions,
        input.physical, input.menu_vertical_axis);
    if (mission_gameplay) {
      mission_gameplay->present();
      // Inspection presents independently while the original mission load and
      // its black fade remain pending; this never advances script completion.
      (void)host.draw(
          mission_gameplay ? mission_gameplay->presentation_opacity() : 1.F);
    } else if (mission_loading && mission_loading->loading_scene().active) {
      loading_pulse();
    } else {
      playback.draw();
    }
    if (result.state == bw::script::RunState::failed) {
      throw std::runtime_error(result.diagnostic);
    }
    if (session.quit_requested()) return 0;
    if (session.gameplay_request() && !mission && session.mission_pending()) {
      const auto& request = *session.gameplay_request();
      std::cout << "{\"status\":\"gameplay_request_ready\",\"mission\":\""
                << request.mission << "\",\"stage\":" << request.selection.stage
                << ",\"difficulty\":" << request.selection.difficulty
                << ",\"costume\":" << request.selection.costume
                << ",\"checkpoint_bytes\":" << request.checkpoint.size()
                << "}\n";
      if (request.selection.action != game::MenuAction::concept_mission ||
          request.script_name != "cm01_00") {
        return 0;
      }
      mission_control.emplace();
      world_clock.emplace();
      mission_player.emplace(
          game::MissionPlayerEntry{request.progression, std::nullopt, true,
                                   configuration.concept_profile});
      std::cerr << "Concept progression: "
                << (mission_player->origin() ==
                            game::ProgressionOrigin::development_completed
                        ? "development completed-game profile (not saved)"
                        : "inherited saved profile")
                << '\n';
      // Mount the selected concept catalog with its original alias priority.
      resources::CatalogSet selected_catalogs(memory.resources);
      selected_catalogs.mount(
          kStableCatalogPriority, catalog_path,
          content::open_catalog(files, catalog_path, memory.resources));
      const auto selected_path =
          configuration.game / "data/concept/cm01_00/cm01_00.rat";
      constexpr int kConceptCatalogPriority = 40;
      selected_catalogs.mount(
          kConceptCatalogPriority, selected_path,
          content::open_catalog(files, selected_path, memory.resources));
      for (unsigned costume_id = 1; costume_id <= kSteamCostumePackages;
           ++costume_id) {
        const auto number = std::to_string(costume_id);
        const auto costume =
            configuration.game / "data/concept" / ("PW" + number) /
            (costume_id == 1 ? "PW1.rat" : "pw" + number + ".rat");
        selected_catalogs.mount(
            static_cast<int>(costume_id), costume,
            content::open_catalog(files, costume, memory.resources));
      }
      mission_resources.emplace(std::move(selected_catalogs), files,
                                memory.resources);
      const auto controller_icons =
          files.read(configuration.game / "data/menu/9900", memory.resources);
      loading_surface.prepare(
          title_surface.selected_loading_background(),
          files.read(configuration.game / "data/menu/9951", memory.resources),
          controller_icons);
      mission_loading.emplace(
          files, mission_resources->catalogs(), memory.resources, &host,
          game::MissionLoadOptions{configuration.game,
                                   profile.current_difficulty(), loading_pulse,
                                   true, configuration.texture_mips});
      constexpr resources::ResourceTag kMissionScriptTag = 100;
      mission.emplace(mission_resources.value(), mission_loading.value(),
                      kMissionScriptTag, request.script_name, memory.runtime,
                      *mission_control, &mission_player.value());
    }
    // The pause/resume UI owns this transition frame. Root/control tasks above
    // still run; the mission coroutine begins observing resume next frame.
    if (mission && !mission_frame.ui_consumed) {
      const auto& progress = mission->advance();
      if (loading_close_requested) return 0;
      if (progress.phase == game::MissionPhase::failed) {
        throw std::runtime_error(progress.diagnostic);
      }
      if (reported_mission_phase != progress.phase) {
        reported_mission_phase = progress.phase;
        std::cerr << "Mission phase=" << static_cast<unsigned>(progress.phase)
                  << " " << progress.diagnostic << '\n';
      }
      if (!inspected_stage &&
          progress.phase == game::MissionPhase::awaiting_world_setup &&
          !mission->stage_name().empty()) {
        // Publish a static view only after the original load has completed.
        // It borrows those owners while actor/gameplay setup remains separate.
        if (!mission_loading) {
          throw std::logic_error("Mission loading owner is missing");
        }
        mission_loading->prepare_stage(mission->stage_name());
        const auto& stage = mission_loading->stage();
        const auto& sky = mission_loading->sky();
        if (!stage || !sky) {
          throw std::logic_error(
              "Inspection stage/sky definitions are missing");
        }
        const auto background_models = mission_loading->resident_dependencies();
        const auto& background = mission_loading->background();
        mission_loading->prepare_player(
            session.gameplay_request()->selection.costume,
            mission_player->selected_weapon());
        // Expand the retained mixer only at mission loading. Existing menu
        // callbacks keep their immutable lookup until this publication seals.
        game_audio.prepare_mission(files, mission_resources->catalogs(),
                                   configuration.game / "data/sound",
                                   memory.media, loading_pulse);
        const auto customization = formats::CustomizationDefinition::parse(
            stage->customization_bytes(), memory.resources);
        player_resources.emplace(
            game::PlayerResourceSettings::parse(customization), *mission_player,
            *world_clock);
        hud_surface.prepare();
        stage_inspection.emplace(
            *world_clock, *mission_control, host,
            game::StageViewSource{stage.value(),
                                  mission_loading->resident_models(),
                                  mission_loading->resident_textures()},
            game::StageViewSource{sky.value(),
                                  mission_loading->sky_resident_models()},
            memory.resources, configuration.inspection_camera,
            game::StageBackgroundSource{background ? &*background : nullptr,
                                        background_models},
            game::PlayerActorSource{
                mission_loading->resident_player(),
                mission_loading->resident_weapon(),
                mission_loading->player_support(),
                mission_loading->weapon_support(),
                game::initial_placement_shim(
                    session.gameplay_request()->script_name,
                    mission->stage_name(),
                    !session.gameplay_request()->checkpoint.empty())},
            loading_pulse,
            game::StageCharacterLightSource{mission_loading->character_light(),
                                            mission_loading->enemy_light()});
        stage_inspection->bind_audio(game_audio.prepared(),
                                     *game_audio.output(), random);
        mission_gameplay.emplace(
            *world_clock, *mission_control, *mission_player, *player_resources,
            *mission_loading, *stage_inspection, hud_surface, pause_surface,
            random, *game_audio.output(), profile.help_control(),
            game::OptionsServices{profile.settings_control(),
                                  profile.settings_save_control(saves),
                                  device_options.control(),
                                  device_options.save_control()},
            game::CockpitActivation::inspection);
        inspected_stage = true;
        loading_surface.end();
        // Loading wall time is not an initial simulation step.
        frame_clock.rebase(std::chrono::steady_clock::now());
        std::cerr << "Static inspection: " << stage_inspection->selected_draws()
                  << " ordinary draws, " << stage_inspection->background_draws()
                  << " placed-prop draws, " << stage_inspection->omitted_draws()
                  << " omitted draws, " << stage_inspection->player_draws()
                  << " player/weapon draws; authored poses, fog and shadows; "
                     "actor simulation remains separate\n";
      }
    }
    if (configuration.verify &&
        session.progress(bw::game::Screen::title).status ==
            bw::game::FlowStatus::complete) {
      if (playback.completed_movies() != 4 ||
          session.status(bw::game::Screen::title) != 2 ||
          session.progress(bw::game::Screen::advertise).status !=
              bw::game::FlowStatus::complete) {
        throw std::runtime_error(
            "Startup title route did not complete naturally");
      }
      std::cout
          << "{\"status\":\"passed\",\"route\":[\"company_logo\",\"memory_"
             "card\","
             "\"advertise\",\"title\"],\"title_result\":2,\"completed_movies\":"
          << playback.completed_movies()
          << ",\"uploaded_frames\":" << playback.presented_frames()
          << ",\"wall_seconds\":"
          << std::chrono::duration<double>(std::chrono::steady_clock::now() -
                                           started)
                 .count()
          << "}\n";
      return 0;
    }
  }
  throw std::runtime_error("Startup exceeded its time budget");
}
}  // namespace bw::game
