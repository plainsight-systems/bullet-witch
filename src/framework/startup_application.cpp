// Shared startup composition for this game/engine product; target entry points
// select and own native services. See startup_application.hpp for the borrowed
// lifetime contract and docs/native-startup-host.md for unchanged loop
// semantics. Algorithm: parse owned configuration; mount read-only resources;
// own session and playback; pump -> media/fade -> game tasks -> presentation;
// retire in reverse order. Unsupported routes and verification failures throw
// to the target entry. A monotonic 60 Hz deadline paces the loop; game delta
// caps at recovered 0.1 s, media wall time is uncapped. Costs remain the
// composed resource/media work; moving the selected factory does not copy
// decoder or native owners. Design MCP: C.129 (#rh-kind) keeps codec choice
// outside shared composition; I.27 (#ri-pimpl) hides the selected host
// implementation. Implementation MCP: E.6 (#re-raii) preserves exception
// cleanup; cpp-performance COPY.5
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

#include "game/startup_playback.hpp"
#include "game/startup_services.hpp"

namespace bw::framework {
StartupOptions parse_startup_options(std::span<char*> arguments) {
  StartupOptions result;
  for (std::size_t index = 1; index < arguments.size(); ++index) {
    const std::string_view argument(arguments[index]);
    if (argument == "--verify-startup") {
      result.verify = true;
    } else if ((argument == "--game" || argument == "--max-seconds") &&
               index + 1 < arguments.size()) {
      const std::string_view value(arguments[++index]);
      if (argument == "--game") {
        result.game = value;
      } else {
        const auto parsed =
            std::from_chars(value.begin(), value.end(), result.maximum_seconds);
        if (parsed.ec != std::errc{} || parsed.ptr != value.end() ||
            result.maximum_seconds <= 0) {
          throw std::runtime_error("--max-seconds requires a positive integer");
        }
      }
    } else {
      throw std::runtime_error(
          "Usage: bw-startup --game GAME_DIRECTORY [--verify-startup] "
          "[--max-seconds N]");
    }
  }
  if (result.game.empty()) {
    throw std::runtime_error("--game GAME_DIRECTORY is required");
  }
  return result;
}

int run_startup(const StartupOptions& configuration,
                platform::StartupHost& host, platform::Files& files,
                video::Player::OpenDecoder open_decoder) {
  bw::resources::CatalogSet catalogs;
  const auto catalog_path = configuration.game / "data/data/stable/stable.rat";
  constexpr int kStableCatalogPriority = 30;
  catalogs.mount(kStableCatalogPriority, catalog_path,
                 bw::content::open_catalog(files, catalog_path));
  bw::resources::ResourceService resources(std::move(catalogs), files);
  bw::game::StartupPlayback playback(host, configuration.game,
                                     std::move(open_decoder));
  bw::game::StartupSession session(playback);
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
    const auto result = session.advance({game_delta, actions});
    playback.draw();
    if (result.state == bw::script::RunState::failed) {
      const bool reached_boundary =
          result.diagnostic.find("Advertise screen is not implemented") !=
              std::string::npos &&
          session.progress(bw::game::Screen::company_logo).status ==
              bw::game::FlowStatus::complete &&
          session.progress(bw::game::Screen::memory_card).status ==
              bw::game::FlowStatus::complete;
      if (configuration.verify && reached_boundary &&
          playback.completed_movies() == 2 && playback.presented_frames() > 0) {
        std::cout
            << "{\"status\":\"passed\",\"route\":[\"company_logo\",\"memory_"
               "card\"],"
               "\"boundary\":\"advertise unsupported\",\"completed_movies\":"
            << playback.completed_movies()
            << ",\"uploaded_frames\":" << playback.presented_frames()
            << ",\"wall_seconds\":"
            << std::chrono::duration<double>(std::chrono::steady_clock::now() -
                                             started)
                   .count()
            << "}\n";
        return 0;
      }
      if (configuration.verify) {
        std::cerr
            << "Startup verification boundary=" << reached_boundary
            << " logo_status="
            << static_cast<unsigned>(
                   session.progress(bw::game::Screen::company_logo).status)
            << " check_status="
            << static_cast<unsigned>(
                   session.progress(bw::game::Screen::memory_card).status)
            << " completed_movies=" << playback.completed_movies()
            << " uploaded_frames=" << playback.presented_frames() << '\n';
      }
      throw std::runtime_error(result.diagnostic);
    }
  }
  throw std::runtime_error("Startup exceeded its time budget");
}
}  // namespace bw::framework
