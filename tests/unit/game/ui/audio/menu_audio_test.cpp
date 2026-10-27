// Menu audio contracts use an observing playback sink, not a second runtime.
// Verify source requests, supplied-time ordering, explicit pause-domain routing
// and rejection before partial publication. Backend audibility/pause requires
// the existing original-content audio corpus. Core I.1 (#ri-explicit) and
// MEM.9 (guidelines/memory/MEM.9-embedded-allocate-at-init.md), both phases
// read.
#include "game/ui/audio/menu_audio.hpp"

#include <array>
#include <limits>
#include <stdexcept>
#include <type_traits>

#include "game/ui/audio/frontend_audio.hpp"

namespace {
using bw::game::MenuSound;
using bw::game::MenuSoundRequests;
void require(bool value, const char* reason) {
  if (!value) throw std::runtime_error(reason);
}
class Output final : public bw::audio::CueOutput,
                     public bw::audio::MusicOutput {
 public:
  bw::audio::MusicOutput& music() override { return *this; }
  void prepare_music(bw::audio::MusicSamples) override { ++preparations; }
  void start_music() override {
    ++starts;
    playing = true;
  }
  void stop_music() noexcept override {
    ++stops;
    playing = false;
  }
  void set_music_envelope(float value) override { envelope = value; }
  bool music_active() const override { return playing; }
  std::size_t preparations{}, starts{}, stops{};
  bool playing{};
  float envelope{1};
  void register_bank(std::uint16_t, std::span<const std::uint8_t>,
                     std::span<const std::uint8_t>,
                     std::span<const std::uint16_t>) override {
    ++owner_operations;
  }
  void seal() override { ++owner_operations; }
  std::optional<bw::audio::CueHandle> play(
      const bw::audio::CueRequest& request) override {
    requests.at(count++) = request;
    return {};
  }
  bool set_position(bw::audio::CueHandle,
                    const bw::math::Vector3<float>&) override {
    ++owner_operations;
    return false;
  }
  void set_listener(const bw::audio::Listener&) override { ++owner_operations; }
  bool stop(bw::audio::CueHandle) override {
    ++owner_operations;
    return false;
  }
  void pause(bool) override { ++owner_operations; }
  void set_mix_gains(const bw::audio::MixGains&) override {
    ++owner_operations;
  }
  void update() override { ++owner_operations; }
  bool active(bw::audio::CueHandle) const override { return false; }
  std::array<bw::audio::CueRequest, 128> requests{};
  std::size_t count{}, owner_operations{};
};
static_assert(!std::is_copy_constructible_v<bw::game::MenuAudio>);
static_assert(!std::is_move_constructible_v<bw::game::MenuAudio>);
static_assert(!std::is_default_constructible_v<bw::game::MusicCommands>);
static_assert(!std::is_constructible_v<bw::game::MusicCommands,
                                       bw::game::MusicPlayback&>);
template <class T>
concept CanAdvanceMusic = requires(T& value) { value.advance(0.F); };
static_assert(!CanAdvanceMusic<bw::game::MusicCommands>);
static_assert(!std::is_copy_constructible_v<bw::game::MusicPlayback>);
template <class Exception, class Operation>
void rejects(Operation operation, const char* reason) {
  bool rejected = false;
  try {
    operation();
  } catch (const Exception&) {
    rejected = true;
  }
  require(rejected, reason);
}
void mapping_and_timing() {
  Output output;
  bw::game::MenuAudio audio(output);
  // Existing delayed work advances first; a newly opened panel retains its
  // entire source delay, independent of the previous presentation delta.
  audio.advance(.5F);
  MenuSoundRequests opening;
  opening.add(MenuSound::pause);
  opening.add(MenuSound::panel, .4F);
  audio.submit(opening);
  require(output.count == 1 && output.requests[0].cue == 12,
          "pause immediate, panel remains scheduled");
  audio.advance(.25F);
  require(output.count == 1, "panel delay has not elapsed");
  audio.advance(.25F);
  require(output.count == 2 && output.requests[1].cue == 1,
          "delayed panel emits once after its source deadline");
  audio.advance(1);
  require(output.count == 2, "due request is retired even if playback rejects");
  constexpr std::array sounds{
      MenuSound::confirm,         MenuSound::back,
      MenuSound::cursor,          MenuSound::error,
      MenuSound::dialog_open,     MenuSound::dialog_close,
      MenuSound::title_start,     MenuSound::mission_start,
      MenuSound::upgrade_purchase};
  constexpr std::array<std::uint16_t, 9> cues{362, 0, 363, 5, 2, 4, 8, 10, 17};
  for (std::size_t i = 0; i < sounds.size(); ++i) {
    MenuSoundRequests request;
    request.add(sounds.at(i));
    audio.submit(request);
    require(output.requests.at(i + 2).cue == cues.at(i),
            "original common cue mapping");
  }
  for (std::size_t i = 0; i < output.count; ++i) {
    const auto& request = output.requests.at(i);
    require(request.bank == 0 && !request.position &&
                request.pause_domain == bw::audio::PauseDomain::presentation,
            "menu requests are nonspatial shared-bank presentation cues");
  }
  require(bw::audio::CueRequest{}.pause_domain == bw::audio::PauseDomain::world,
          "ordinary action requests default to world pause");
  require(output.owner_operations == 0,
          "menu scheduler cannot pump, pause, register or control listener");
}
void invalid_requests_are_atomic() {
  Output output;
  bw::game::MenuAudio audio(output);
  MenuSoundRequests invalid;
  invalid.add(MenuSound::confirm);
  invalid.requests[1] = {MenuSound::panel, -1};
  invalid.size = 2;
  rejects<std::invalid_argument>([&] { audio.submit(invalid); },
                                 "invalid delay rejects entire batch");
  require(output.count == 0, "invalid later entry cannot publish first cue");
  invalid.requests[1] = {static_cast<MenuSound>(255), 0};
  rejects<std::invalid_argument>([&] { audio.submit(invalid); },
                                 "invalid semantic event rejected");
  invalid.size = 5;
  rejects<std::invalid_argument>([&] { audio.submit(invalid); },
                                 "malformed batch extent rejected");
  for (const float value : {-1.F, std::numeric_limits<float>::infinity(),
                            std::numeric_limits<float>::quiet_NaN()}) {
    MenuSoundRequests request;
    rejects<std::invalid_argument>(
        [&] { request.add(MenuSound::panel, value); },
        "batch add validates timing");
    require(request.size == 0, "bad add preserves batch size");
    rejects<std::invalid_argument>([&] { audio.advance(value); },
                                   "nonfinite or negative delta rejected");
  }
  MenuSoundRequests batch;
  for (unsigned i = 0; i < 4; ++i) batch.add(MenuSound::panel, 1);
  rejects<std::length_error>([&] { batch.add(MenuSound::panel); },
                             "batch capacity is fixed");
  require(batch.size == 4, "failed add preserves existing batch");
  for (unsigned i = 0; i < 8; ++i) audio.submit(batch);
  MenuSoundRequests overflowing;
  overflowing.add(MenuSound::confirm);
  overflowing.add(MenuSound::panel, 1);
  rejects<std::length_error>([&] { audio.submit(overflowing); },
                             "delayed queue capacity explicitly rejected");
  require(output.count == 0, "overflow rejects before immediate cue plays");
  audio.advance(0);
  require(output.count == 0, "zero delta preserves pending delays");
  audio.advance(1);
  require(output.count == 32, "rejections preserve accepted queue contents");
  audio.submit(batch);
  audio.advance(1);
  require(output.count == 36, "completed requests release queue capacity");
}
void frontend_lifetime_and_fade() {
  Output output;
  unsigned preparations = 0;
  bw::game::MusicPlayback music(output, [&](std::uint32_t) { ++preparations; });
  bw::game::FrontEndAudio frontend(output, music.commands());
  MenuSoundRequests title;
  title.add(MenuSound::title_start);
  frontend.submit(title);
  require(output.count == 1 && output.requests[0].cue == 8 &&
              preparations == 0 && output.starts == 0,
          "title cue does not prepare or start main music");
  frontend.start_menu_music();
  music.advance(0);
  require(preparations == 1 && output.starts == 1 && output.playing,
          "first main entry prepares and starts music");
  frontend.advance(10);
  music.advance(10);
  require(output.playing && output.stops == 0 && output.owner_operations == 0,
          "frontend updates preserve music and do not pump shared device");
  frontend.stop_menu_music(1);
  frontend.advance(.25F);
  music.advance(.25F);
  require(output.playing && output.envelope == .75F,
          "title return fades existing music over original one second");
  frontend.advance(.75F);
  music.advance(.75F);
  require(!output.playing && output.stops == 1 && output.envelope == 0,
          "fade completion stops only music");
  frontend.start_menu_music();
  music.advance(0);
  require(preparations == 2 && output.starts == 2 && output.envelope == 1,
          "reentry reselects menu track after shared device use");
  frontend.stop_menu_music(3);
  frontend.advance(1.5F);
  music.advance(1.5F);
  require(output.envelope == .5F && output.playing,
          "mission transition uses original three second fade");
  const auto before = output.envelope;
  rejects<std::invalid_argument>([&] { frontend.advance(-1); },
                                 "invalid delta rejects");
  rejects<std::invalid_argument>(
      [&] {
        frontend.stop_menu_music(std::numeric_limits<float>::quiet_NaN());
      },
      "invalid fade rejects");
  require(output.envelope == before && output.playing,
          "invalid input preserves native state");
  frontend.advance(2);
  music.advance(2);
  require(output.stops == 2 && !output.playing,
          "oversized frame completes fade once");
  frontend.advance(2);
  music.advance(2);
  require(output.stops == 2, "completed fade is retired");
  Output rejected;
  unsigned attempts = 0;
  bw::game::MusicPlayback retry(rejected, [&](std::uint32_t) {
    if (++attempts == 1) throw std::runtime_error("preparation failure");
  });
  bw::game::FrontEndAudio failing(rejected, retry.commands());
  failing.start_menu_music();
  rejects<std::runtime_error>([&] { retry.advance(0); },
                              "preparation failure is surfaced");
  require(rejected.starts == 0, "failed preparation does not start playback");
  failing.start_menu_music();
  retry.advance(0);
  require(attempts == 2 && rejected.starts == 1,
          "failed preparation does not publish readiness");
}
// Regression: unfinished frontend departure cannot fade the mission track.
// Both policies publish into the same native source state machine. Clock and
// preparation authority are deliberately unavailable on either command handle.
void shared_music_handoff() {
  Output output;
  std::array<std::uint32_t, 4> prepared{};
  std::size_t count = 0;
  bw::game::MusicPlayback music(
      output, [&](std::uint32_t track) { prepared.at(count++) = track; });
  bw::game::FrontEndAudio frontend(output, music.commands());
  const auto mission = music.commands();
  frontend.start_menu_music();
  music.advance(0);
  frontend.stop_menu_music(3);
  music.advance(.5F);
  mission.play({7, 1});
  require(count == 1 && output.starts == 1,
          "mission request cannot replace PCM during old frontend fade");
  frontend.advance(10);
  require(output.playing && output.envelope > 0,
          "frontend has no retained envelope authority after request");
  music.advance(2.5F);
  require(!output.playing && output.stops == 1 && count == 1,
          "old fade finishes before new track admission");
  music.advance(0);
  require(output.playing && output.starts == 2 && count == 2 &&
              prepared[0] == 16 && prepared[1] == 7 && output.envelope == 0,
          "next update admits original mission track and its own fade");
  music.advance(.5F);
  require(output.envelope == .5F,
          "mission fade uses source one-second duration");
  frontend.advance(20);
  music.advance(.5F);
  require(output.playing && output.envelope == 1 && output.stops == 1,
          "expired frontend policy cannot stop mission music");
  mission.play({3, 1});
  music.advance(1);
  require(count == 2, "original playing service ignores play without a stop");
  mission.stop(3);
  music.advance(3);
  require(!output.playing, "checkpoint stop applies original fade");
  music.advance(2);
  mission.play({3, 1});
  music.advance(0);
  music.advance(1);
  require(prepared[2] == 3 && output.playing && output.envelope == 1,
          "checkpoint timer may start its next track after source silence");
  music.retire();
  require(!output.playing,
          "composition retirement stops current session track");
}

}  // namespace
void menu_audio_contracts() {
  shared_music_handoff();
  frontend_lifetime_and_fade();
  mapping_and_timing();
  invalid_requests_are_atomic();
}
