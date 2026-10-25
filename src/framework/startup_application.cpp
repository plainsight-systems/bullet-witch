// Timing composition: native pacing waits on engine FrameClock; one engine
// WorldClock publishes mission frames after game input/pause admission. Root
// control work continues while normal world work is withheld; mission coroutine
// pause is independent. No framework delta clamp, pause mirror or input rearm
// state. Design C.9/I.1; implementation E.4 (#re-design-invariants); both-phase
// MEM.9 keeps sampled frame/control storage fixed. See world-timing.md.
// Concept progression selection is an explicit development policy. Saved
// mode inherits the owned handoff; development mode supplies a nonpersistent
// completed-game capability profile at entry. Neither edits permanent saves.
// Design C.9/I.30, implementation E.4 (#re-design-invariants); both-phase MEM.9
// (guidelines/memory/MEM.9-embedded-allocate-at-init.md) bounds entry-only
// work. Capture suppresses developer presentation at composition boundaries
// while retaining ordinary input, simulation, media and loading. Optional
// launch budgets bound only explicitly timed/verification runs. Both MCP
// phases: Core I.1 (#ri-explicit) supplies immutable launch policy; performance
// MEM.9 (guidelines/memory/MEM.9-embedded-allocate-at-init.md) adds no frame
// storage. Interactive presentation review forwards captured intent to Stage
// and keeps external review observations explicit. Paused transactions rebind
// catalog borrowers after publication, report failures without advancing the
// game, and rebase wall deadlines. Ordinary next-weapon activates
// mission-resident assets without pausing audio/world or consuming a UI-only
// frame; source model instance recreation is separate from immutable resource
// acquisition (R.20/I.11/CDSA.32). Both MCP phases I.1 (#ri-explicit) preserve
// admission/composition boundaries; MEM.9
// (guidelines/memory/MEM.9-embedded-allocate-at-init.md) keeps notices and
// per-frame orchestration fixed values; resource work occurs only on requests.
// Pause composition routes UI input before game clocks, preserving a sampled
// wall clock while simulation is frozen. A selected costume loads/publishes as
// an explicit paused transaction; failure keeps the current appearance. Menu
// resources are prepared at mission admission, not each pause. Both phases Core
// I.1 (#ri-explicit), E.6 (#re-raii), performance MEM.9
// (guidelines/memory/MEM.9-embedded-allocate-at-init.md). See
// research/engine/subsystems/pause-and-costume-switching.md.
// Loading presentation uses explicit asset-boundary callbacks (design Core I.1
// #ri-explicit, implementation E.6 #re-raii). Performance GPU.1 retains
// uploads; callbacks pump only completed loading UI and never inspect partial
// owners. An optional inspection pose is passed once to stage-view
// construction. This development setting never replaces the original player
// spawn or starts actor simulation. Both MCP phases: I.1 (#ri-explicit) keeps
// the override in owned configuration; MEM.9
// (guidelines/memory/MEM.9-embedded-allocate-at-init.md) keeps parsing and view
// construction outside repeated frame work. Memory composition: require
// resource/runtime/media capabilities, construct catalogs and their service
// with one resource identity, then inject runtime and media owners. Failure
// unwinds without a default-heap retry. R.1
// (#rr-raii) and MEM.6 (guidelines/memory/
// MEM.6-inject-allocators-behind-one-interface.md), read in both phases,
// govern ownership and startup-only capability propagation.
// Front-end extension: mount the supplied costume catalogs, own Profile and
// menu presentation, and inject borrowed SaveStorage into FrontEnd. Original
// Lua drives screen transitions and mission routing. One Shot Kill owns its
// original mission/load context and a separate static inspection after its
// resource constructors complete. World setup remains a separate gate. Other
// requests retain the handoff boundary;
// never report a completed mission. Profile I/O occurs only at load/change,
// and menu images allocate once per front-end activation. Design I.30
// (#ri-encapsulate), implementation E.6 (#re-raii) and performance COPY.4 below
// preserve explicit game composition and retirement order. See
// research/engine/subsystems/frontend-integration.md.
// Startup route extension: game policies borrow capabilities; composition owns
// them through cleanup. Design MCP R.20 (#rr-owner) retains explicit lifetime;
// implementation E.6 (#re-raii) unwinds on failures. cpp-performance COPY.4
// (guidelines/copy-move/COPY.4-rule-of-five-rule-of-zero.md) preserves
// container ownership without per-frame payload copies. Shared presentation
// contracts are described in
// research/engine/subsystems/startup-title-integration.md. Shared startup
// composition for this game/engine product; target entry points select and own
// native services. See startup_application.hpp for the borrowed lifetime
// contract and docs/native-startup-host.md for unchanged loop semantics.
// Algorithm: parse owned configuration; mount read-only resources; own session
// and playback; pump -> media/fade -> game tasks -> presentation; retire in
// reverse order. Unsupported routes and verification failures throw to the
// target entry. A monotonic 60 Hz deadline paces the loop; game delta caps at
// recovered 0.1 s, media wall time is uncapped. Costs remain the composed
// resource/media work; moving the selected factory does not copy decoder or
// native owners. Design MCP: C.129 (#rh-kind) keeps codec choice outside shared
// composition; I.27 (#ri-pimpl) hides the selected host implementation.
// Implementation MCP: E.6 (#re-raii) preserves exception cleanup;
// cpp-performance COPY.5
// (guidelines/copy-move/COPY.5-resource-owning-types-are-move-only.md)
// preserves unique ownership while transferring the decoder factory.
#include "startup_application.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <utility>

#include "bw/timing/frame_clock.hpp"
#include "bw/world/world_clock.hpp"
#include "game/initial_placement_shim.hpp"
#include "game/mission_control.hpp"
#include "game/mission_loading.hpp"
#include "game/pause_flow.hpp"
#include "game/source_random.hpp"
#include "game/stage_inspection.hpp"
#include "game/startup_loading_surface.hpp"
#include "game/startup_menu_surface.hpp"
#include "game/startup_playback.hpp"
#include "game/startup_services.hpp"
#include "game/startup_title_surface.hpp"

namespace bw::framework {

int run_startup(const StartupOptions& configuration,
                platform::StartupHost& host, platform::Files& files,
                platform::SaveStorage& saves,
                video::Player::OpenDecoder open_decoder,
                const StartupMemory& memory,
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
  bw::resources::ResourceService resources(std::move(catalogs), files,
                                           memory.resources);
  // Declaration order keeps services alive through reverse-order session
  // teardown.
  bw::game::StartupPlayback playback(host, configuration.game,
                                     std::move(open_decoder), memory.media);
  // Original selector seeds MSVCRT rand with platform milliseconds per title
  // activation, then chooses rand()%10. Preserve the algorithm with a portable
  // unsigned seed; exact boot epoch is host-specific.
  bw::game::StartupTitleSurface title_surface(resources, host, [] {
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
  bw::game::Profile profile;
  bw::game::StartupMenuSurface menu_surface(resources, host);
  bw::game::FrontEnd frontend(profile, saves, playback, menu_surface, {});
  bw::game::StartupSession session(playback, &title_surface, &frontend,
                                   memory.runtime);
  session.start_root(resources, 1);
  game::StartupLoadingSurface loading_surface(resources, host);
  // Loading callbacks retain this presentation state. Declare it before all
  // mission holders so it remains alive until their callbacks are retired.
  const auto started = std::chrono::steady_clock::now();
  const auto deadline =
      configuration.maximum_seconds
          ? started + std::chrono::seconds(*configuration.maximum_seconds)
          : std::chrono::steady_clock::time_point::max();
  constexpr auto target_period = std::chrono::nanoseconds(16'666'667);
  timing::FrameClock frame_clock(started, target_period);
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
  std::optional<game::MissionAudio> mission_audio;
  std::optional<game::StageInspection> stage_inspection;
  std::optional<game::PauseFlow> pause;
  std::string_view live_notice;
  const auto bind_review_catalogs = [&] {
    const auto* body = stage_inspection.value().review_catalog(false);
    const auto* weapon = stage_inspection.value().review_catalog(true);
    if (body != nullptr && weapon != nullptr) {
      pause.value().set_available_weapons(mission_player->levels().weapons);
      pause.value().bind_review_catalogs(
          *body, *weapon, mission_loading.value().current_weapon_class(),
          stage_inspection.value().review_pose_semantics(false),
          stage_inspection.value().review_pose_semantics(true));
    }
  };
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
    host.set_frame_timing(frame_clock.sample(now));
    loading_close_requested |= host.pump(true).close_requested;
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
    const auto input = host.pump(session.window_close_enabled());
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
    bool ui_frame = false;
    game::GameplayInput gameplay;
    if (input.pause_pressed || input.reload_pressed || input.attack_pressed ||
        input.aim_mode_pressed || input.fire_trigger > 0) {
      live_notice = {};
    }
    if (pause) {
      ui_frame = pause->update(input.pause_pressed,
                               {input.pressed_actions, input.repeated_actions});
      gameplay = mission_control->route(
          {.movement = input.movement,
           .look = input.look,
           .fire_trigger = input.fire_trigger,
           .evade_held = input.evade_held,
           .crouch_pressed = input.crouch_pressed,
           .reload_pressed = input.reload_pressed,
           .attack_pressed = input.attack_pressed,
           .weapon_cycle_pressed = input.weapon_cycle_pressed,
           .aim_mode_pressed = input.aim_mode_pressed},
          ui_frame);
      stage_inspection->synchronize_pause();
      if (const auto costume = pause->requested_costume()) {
        bool success = false;
        try {
          stage_inspection->change_costume(*mission_loading, *costume);
          success = true;
        } catch (const std::exception& error) {
          mission_loading->discard_player_costume();
          std::cerr << "Costume change: " << error.what() << '\n';
        }
        pause->complete_costume(success);
        if (success) bind_review_catalogs();
        // A bounded resource transaction is not simulation time on resume.
        frame_clock.rebase(std::chrono::steady_clock::now());
      }
      if (const auto weapon_class = pause->requested_weapon()) {
        bool success = false;
        try {
          if (!mission_player->weapon_available(*weapon_class)) {
            throw std::invalid_argument("Weapon has not been acquired");
          }
          stage_inspection->change_weapon(*mission_loading, *weapon_class);
          mission_player->select_weapon(*weapon_class);
          success = true;
        } catch (const std::exception& error) {
          mission_loading->discard_weapon_class();
          std::cerr << "Weapon preview change: " << error.what() << '\n';
        }
        pause->complete_weapon(success);
        if (success) bind_review_catalogs();
        frame_clock.rebase(std::chrono::steady_clock::now());
      }
      if (const auto review = pause->requested_review()) {
        pause->complete_review(stage_inspection->review(*review));
        // Successful preview resumes the scene next frame; this confirmation
        // remains entirely owned by the UI and cannot also fire an action.
        stage_inspection->synchronize_pause();
        live_notice = {};
      }
      if (gameplay.weapon_cycle_pressed) {
        const auto* player = stage_inspection->player_state();
        if (player == nullptr ||
            player->phase != game::PlayerMovementPhase::grounded) {
          live_notice = "Weapon preview change needs grounded Alicia.";
        } else {
          // Resident selection is an ordinary action. This frame still
          // advances world/audio and retains elapsed host time.
          bool success = false;
          try {
            if (mission_player->selected_weapon() !=
                mission_loading->current_weapon_class()) {
              throw std::logic_error("Player and resident weapon disagree");
            }
            const auto next_weapon = mission_player->next_weapon();
            if (next_weapon != mission_player->selected_weapon()) {
              stage_inspection->switch_weapon(*mission_loading, next_weapon);
              // Publish authoritative equipment only after preparation and
              // action admission succeed; rejection retains the old selection.
              mission_player->select_weapon(next_weapon);
              success = true;
            }
          } catch (const std::exception& error) {
            mission_loading->discard_weapon_class();
            std::cerr << "Weapon preview change: " << error.what() << '\n';
          }
          if (success) {
            bind_review_catalogs();
            live_notice =
                "Acquired weapon selected; combat resources remain pending.";
          } else {
            live_notice =
                "Weapon preview unavailable; current weapon retained.";
          }
        }
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
    // Normal world work precedes root/mission control as in the base game path.
    // A control-only frame remains published, but Stage does not simulate it.
    if (stage_inspection) {
      const auto frame = world_clock->publish(
          presentation_delta, mission_control->admission(ui_frame));
      stage_inspection->advance(
          frame,
          {.movement = {gameplay.movement, gameplay.evade_held,
                        gameplay.crouch_pressed},
           .look = {gameplay.look[0], gameplay.look[1]},
           .presentation = {.fire_trigger = gameplay.fire_trigger,
                            .reload_pressed = gameplay.reload_pressed,
                            .attack_pressed = gameplay.attack_pressed,
                            .aim_mode_pressed = gameplay.aim_mode_pressed}});
    }
    const auto actions =
        configuration.verify || stage_inspection ? 0U : input.pressed_actions;
    const auto result = session.advance(
        {presentation_delta, actions},
        configuration.verify || stage_inspection ? 0U : input.repeated_actions);
    if (stage_inspection) {
      if (pause && pause->paused()) {
        menu_surface.show(pause->presentation());
      } else {
        // Clear the developer overlay without altering the world submission.
        menu_surface.show_status(configuration.capture
                                     ? std::string_view{}
                                     : (live_notice.empty()
                                            ? stage_inspection->review_status()
                                            : live_notice));
      }
      // Inspection presents independently while the original mission load and
      // its black fade remain pending; this never advances script completion.
      (void)host.draw(1.0F);
    } else if (mission_loading && mission_loading->loading_scene().active) {
      loading_pulse();
    } else {
      playback.draw();
    }
    if (result.state == bw::script::RunState::failed) {
      throw std::runtime_error(result.diagnostic);
    }
    if (session.quit_requested()) return 0;
    if (session.gameplay_request() && !mission) {
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
                                   true});
      constexpr resources::ResourceTag kMissionScriptTag = 100;
      mission.emplace(mission_resources.value(), mission_loading.value(),
                      kMissionScriptTag, request.script_name, memory.runtime,
                      *mission_control, &mission_player.value());
    }
    // The pause/resume UI owns this transition frame. Root/control tasks above
    // still run; the mission coroutine begins observing resume next frame.
    if (mission && !ui_frame) {
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
        // Original common/voice banks prepare once under loading progress;
        // the target chooses the native cue backend through its factory.
        constexpr audio::Limits kMissionAudioLimits{
            .banks = 3, .cues = 128, .decoded_samples = 50'000'000};
        mission_audio.emplace(files, mission_resources->catalogs(),
                              configuration.game / "data/sound", memory.media,
                              audio_factory, kMissionAudioLimits,
                              loading_pulse);
        stage_inspection.emplace(
            *world_clock, *mission_control, host,
            game::StageViewSource{stage.value(),
                                  mission_loading->resident_models()},
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
        stage_inspection->bind_audio(mission_audio->prepared(),
                                     *mission_audio->output(), random);
        menu_surface.begin();
        while (!menu_surface.ready()) loading_pulse();
        pause.emplace(mission_control->user_pause_control(),
                      mission_loading->current_player_costume(),
                      !configuration.capture);
        bind_review_catalogs();
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
}  // namespace bw::framework
