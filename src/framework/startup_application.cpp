// Front-end extension: mount the supplied costume catalogs, own Profile and
// menu presentation, and inject borrowed SaveStorage into FrontEnd. Original
// Lua drives screen transitions and mission routing. On a confirmed campaign
// request, print owned launch metadata and return at the world-loader boundary;
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
#include <charconv>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <thread>
#include <utility>

#include "game/startup_menu_surface.hpp"
#include "game/startup_playback.hpp"
#include "game/startup_services.hpp"
#include "game/startup_title_surface.hpp"

namespace bw::framework {
StartupOptions parse_startup_options(std::span<char*> arguments) {
  StartupOptions result;
  for (std::size_t index = 1; index < arguments.size(); ++index) {
    const std::string_view argument(arguments[index]);
    if (argument == "--windowed") {
      result.window.fullscreen = false;
    } else if (argument == "--verify-startup") {
      result.verify = true;
    } else if ((argument == "--game" || argument == "--profile" ||
                argument == "--max-seconds" || argument == "--window-width" ||
                argument == "--window-height") &&
               index + 1 < arguments.size()) {
      const std::string_view value(arguments[++index]);
      if (argument == "--game") {
        result.game = value;
      } else if (argument == "--profile") {
        result.profile = value;
      } else {
        int parsed_value{};
        const auto parsed =
            std::from_chars(value.begin(), value.end(), parsed_value);
        if (parsed.ec != std::errc{} || parsed.ptr != value.end() ||
            parsed_value <= 0) {
          throw std::runtime_error(std::string(argument) +
                                   " requires a positive integer");
        }
        if (argument == "--max-seconds") {
          result.maximum_seconds = parsed_value;
        } else if (argument == "--window-width") {
          result.window.width = static_cast<std::uint32_t>(parsed_value);
        } else {
          result.window.height = static_cast<std::uint32_t>(parsed_value);
        }
      }
    } else {
      throw std::runtime_error(
          "Usage: bw-startup --game GAME_DIRECTORY [--profile SAVE_DIRECTORY] "
          "[--verify-startup] "
          "[--max-seconds N] [--windowed] [--window-width N] [--window-height "
          "N]");
    }
  }
  if (result.game.empty()) {
    throw std::runtime_error("--game GAME_DIRECTORY is required");
  }
  return result;
}

int run_startup(const StartupOptions& configuration,
                platform::StartupHost& host, platform::Files& files,
                platform::SaveStorage& saves,
                video::Player::OpenDecoder open_decoder) {
  bw::resources::CatalogSet catalogs;
  const auto catalog_path = configuration.game / "data/data/stable/stable.rat";
  constexpr int kStableCatalogPriority = 30;
  catalogs.mount(kStableCatalogPriority, catalog_path,
                 bw::content::open_catalog(files, catalog_path));
  constexpr unsigned kSteamCostumePackages = 5;
  for (unsigned index = 1; index <= kSteamCostumePackages; ++index) {
    const auto name = std::string("PW") + std::to_string(index);
    const auto path =
        configuration.game / "data/concept" / name / (name + ".rat");
    if (std::filesystem::exists(path)) {
      catalogs.mount(static_cast<int>(index), path,
                     bw::content::open_catalog(files, path));
    }
  }
  bw::resources::ResourceService resources(std::move(catalogs), files);
  bw::game::StartupPlayback playback(host, configuration.game,
                                     std::move(open_decoder));
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
  bw::game::StartupSession session(playback, &title_surface, &frontend);
  session.start_root(resources, 1);
  const auto started = std::chrono::steady_clock::now();
  const auto deadline =
      started + std::chrono::seconds(configuration.maximum_seconds);
  auto previous_tick = started;
  auto next_tick = started;
  constexpr auto target_period = std::chrono::nanoseconds(16'666'667);
  constexpr float maximum_game_delta = 0.1F;
  std::size_t reported_movies = 0;
  float longest_tick = 0;
  double longest_pump = 0;
  while (std::chrono::steady_clock::now() < deadline) {
    std::this_thread::sleep_until(next_tick);
    const auto tick = std::chrono::steady_clock::now();
    const float wall_delta =
        std::chrono::duration<float>(tick - previous_tick).count();
    const float game_delta = std::min(wall_delta, maximum_game_delta);
    longest_tick = std::max(longest_tick, wall_delta);
    previous_tick = tick;
    next_tick += target_period;
    if (next_tick <= tick) {
      next_tick = tick + target_period;
    }
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
    playback.advance({.wall_delta = wall_delta, .game_delta = game_delta});
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
    const auto actions = configuration.verify ? 0U : input.pressed_actions;
    const auto result =
        session.advance({game_delta, actions},
                        configuration.verify ? 0U : input.repeated_actions);
    playback.draw();
    if (result.state == bw::script::RunState::failed) {
      throw std::runtime_error(result.diagnostic);
    }
    if (session.quit_requested()) return 0;
    if (session.gameplay_request()) {
      const auto& request = *session.gameplay_request();
      std::cout << "{\"status\":\"gameplay_request_ready\",\"mission\":\""
                << request.mission << "\",\"stage\":" << request.selection.stage
                << ",\"difficulty\":" << request.selection.difficulty
                << ",\"costume\":" << request.selection.costume
                << ",\"checkpoint_bytes\":" << request.checkpoint.size()
                << "}\n";
      return 0;
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
