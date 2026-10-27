// In-process authored-tree modal contracts. Synthetic text proves namespace,
// page/language selection, acknowledgment gates, final close and cancellation;
// an observing audio sink verifies real cue policy without a second runtime.
// Core SL.con.3 (#rsl-bounds), performance MEM.9 (guidelines/memory/
// MEM.9-embedded-allocate-at-init.md) keep fixtures bounded and deterministic.
#include "game/ui/mission/mission_message.hpp"

#include <limits>
#include <type_traits>

#include "../../../../fixtures/camera_fixture.hpp"
namespace {
using camera_fixture::require;
using camera_fixture::word;
class Audio final : public bw::audio::CueOutput {
 public:
  void register_bank(std::uint16_t, std::span<const std::uint8_t>,
                     std::span<const std::uint8_t>,
                     std::span<const std::uint16_t>) override {}
  void seal() override {}
  std::optional<bw::audio::CueHandle> play(
      const bw::audio::CueRequest& request) override {
    requests.at(count++) = request;
    return {};
  }
  bool set_position(bw::audio::CueHandle,
                    const bw::math::Vector3<float>&) override {
    return false;
  }
  void set_listener(const bw::audio::Listener&) override {}
  bool stop(bw::audio::CueHandle) override { return false; }
  void set_mix_gains(const bw::audio::MixGains&) override {}
  void pause(bool) override {}
  void update() override {}
  bool active(bw::audio::CueHandle) const override { return false; }
  std::array<bw::audio::CueRequest, 16> requests{};
  std::size_t count{};
};
class Output final : public bw::game::MissionMessageOutput {
 public:
  void show(std::string_view value,
            const bw::game::MenuDialogPresentation& state) override {
    text = value;
    presentation = state;
    visible = true;
  }
  void hide() noexcept override { visible = false; }
  std::string_view text;
  bw::game::MenuDialogPresentation presentation;
  bool visible{};
};
bw::resources::Bytes fixture() {
  bw::resources::Bytes bytes(320);
  const auto text = [&](std::size_t at, std::string_view value) {
    std::copy(value.begin(), value.end(),
              bytes.begin() + static_cast<std::ptrdiff_t>(at));
  };
  text(0, "STMANAGE");
  constexpr std::array<std::uint32_t, 6> offsets{32, 60, 80, 140, 204, 204};
  for (std::size_t i = 0; i < offsets.size(); ++i) {
    word(bytes, 8 + i * 4, offsets.at(i));
  }
  word(bytes, 32, 1);
  word(bytes, 44, 1);
  word(bytes, 60, 9);
  word(bytes, 64, 3);
  for (std::size_t i = 0; i < 3; ++i) {
    word(bytes, 80 + i * 20 + 4, i == 0 ? 2 : 1);
    word(bytes, 80 + i * 20 + 8,
         static_cast<std::uint32_t>((i == 0 ? 0 : i + 1) * 16));
  }
  word(bytes, 140, 100);
  word(bytes, 144, 20);
  for (std::size_t i = 1; i < 4; ++i) {
    word(bytes, 140 + i * 16, 200);
    word(bytes, 144 + i * 16, static_cast<std::uint32_t>(20 + i * 8));
  }
  text(205, "mission");
  text(213, "0000");
  text(224, "foreign");
  text(232, "first");
  text(240, "second");
  text(248, "third");
  return bytes;
}
static_assert(!std::is_copy_constructible_v<bw::game::MissionMessage>);
static_assert(!std::is_move_constructible_v<bw::game::MissionMessage>);
}  // namespace
void mission_message_tests() {
  const auto bytes = fixture();
  const auto table = bw::formats::TextTable::parse(bytes);
  Output output;
  Audio audio;
  bw::game::MissionMessage flow(table, "mission", output, audio);
  flow.start(0);
  flow.present();
  require(flow.pending() && output.text == "first",
          "mission namespace and English first page");
  require(audio.count == 1 && audio.requests[0].cue == 2,
          "original dialog opening cue");
  const bw::game::MenuInput confirm{.pressed = 0x1010};
  flow.update(confirm, .1F);
  flow.present();
  require(output.text == "first", "opening gate rejects early acknowledgment");
  flow.update({}, .3F);
  flow.update(confirm, 0);
  flow.present();
  require(
      output.text == "second" && flow.pending() && !output.presentation.closing,
      "page acknowledgment retains open dialog");
  require(audio.requests[1].cue == 6,
          "native page event resolves bank cue six");
  flow.update(confirm, .1F);
  flow.present();
  require(output.text == "second", "page input gate rejects fast repeat");
  flow.update(confirm, .2F);
  flow.present();
  require(output.text == "third", "all authored pages preserved");
  flow.update({}, 10);
  require(flow.pending(), "elapsed time never acknowledges instruction");
  flow.update(confirm, 0);
  require(flow.pending(), "final acknowledgment waits for closing transition");
  flow.update({}, .3F);
  require(!flow.pending() && !output.visible,
          "final close retires publication");
  bool rejected = false;
  try {
    flow.start(1);
  } catch (const std::runtime_error&) {
    rejected = true;
  }
  require(rejected && !flow.pending(),
          "missing message cannot publish modal state");
  flow.start(0);
  flow.present();
  rejected = false;
  try {
    flow.update({}, std::numeric_limits<float>::quiet_NaN());
  } catch (const std::invalid_argument&) {
    rejected = true;
  }
  require(rejected && flow.pending(),
          "invalid interval does not retire instruction");
  flow.cancel();
  require(!flow.pending() && !output.visible,
          "mission teardown clears instruction");
}
