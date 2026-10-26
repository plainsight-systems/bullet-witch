// Menu audio contracts use an observing playback sink, not a second runtime.
// Verify source requests, supplied-time ordering, explicit pause-domain routing
// and rejection before partial publication. Backend audibility/pause requires
// the existing original-content audio corpus. Core I.1 (#ri-explicit) and
// MEM.9 (guidelines/memory/MEM.9-embedded-allocate-at-init.md), both phases
// read.
#include "game/menu_audio.hpp"

#include <array>
#include <limits>
#include <stdexcept>
#include <type_traits>

namespace {
using bw::game::MenuSound;
using bw::game::MenuSoundRequests;
void require(bool value, const char* reason) {
  if (!value) throw std::runtime_error(reason);
}
class Output final : public bw::audio::CueOutput {
 public:
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
  constexpr std::array sounds{MenuSound::confirm,     MenuSound::back,
                              MenuSound::cursor,      MenuSound::error,
                              MenuSound::dialog_open, MenuSound::dialog_close};
  constexpr std::array<std::uint16_t, 6> cues{362, 0, 363, 5, 2, 4};
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
}  // namespace
void menu_audio_contracts() {
  mapping_and_timing();
  invalid_requests_are_atomic();
}
