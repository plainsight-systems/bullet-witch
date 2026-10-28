// Observing output verifies request ownership, not audible original playback.
#include "game/combat/weapon_audio.hpp"

#include <array>

#include "../../../fixtures/camera_fixture.hpp"
namespace {
class Output final : public bw::audio::CueOutput {
 public:
  void register_bank(std::uint16_t, std::span<const std::uint8_t>,
                     std::span<const std::uint8_t>,
                     std::span<const std::uint16_t>) override {}
  void seal() override {}
  std::optional<bw::audio::CueHandle> play(
      const bw::audio::CueRequest& value) override {
    request = value;
    live.at(plays) = true;
    ++plays;
    return bw::audio::CueHandle{0, plays};
  }
  bool set_position(bw::audio::CueHandle handle,
                    const bw::math::Vector3<float>& value) override {
    if (!active(handle)) return false;
    request.position = value;
    return true;
  }
  bool stop(bw::audio::CueHandle handle) override {
    if (!active(handle)) return false;
    live.at(handle.generation - 1) = false;
    ++stops;
    return true;
  }
  bool active(bw::audio::CueHandle handle) const override {
    return handle.generation > 0 && handle.generation <= plays &&
           live.at(handle.generation - 1);
  }
  void set_listener(const bw::audio::Listener&) override {}
  void set_mix_gains(const bw::audio::MixGains&) override {}
  void pause(bool) override {}
  void update() override {}
  bw::audio::CueRequest request;
  std::uint32_t plays{}, stops{};
  std::array<bool, 16> live{};
};
}  // namespace
void weapon_audio_tests() {
  using camera_fixture::require;
  Output output;
  const auto player = bw::game::WeaponSoundProfile::original("wa0");
  const auto soldier = bw::game::WeaponSoundProfile::original("wg1");
  require(player.fire && player.fire->request.cue == 0x1b0 && soldier.fire &&
              soldier.fire->request.cue == 0x116 && !player.fire->retained &&
              soldier.fire->retained,
          "Source contextual weapon classes resolve distinct original cues");
  {
    bw::game::WeaponAudio weapon(soldier, output);
    weapon.shot({1, 2, 3});
    weapon.advance(.1F, {1, 2, 3});
    weapon.shot({2, 2, 3});
    require(output.plays == 1 && output.active({0, output.plays}) &&
                output.request.position == bw::math::Vector3<float>{2, 2, 3},
            "Repeated source shot retains active request and follows emitter");
    weapon.advance(0, {3, 2, 3});
    require(output.active({0, output.plays}),
            "Paused interval cannot expire weapon sound tail");
    weapon.advance(.126F, {3, 2, 3});
    require(!output.active({0, output.plays}) && output.stops == 1,
            "Absent release cue still retires firing sound");
    weapon.shot({});
    output.live.at(output.plays - 1) = false;
    weapon.shot({});
    require(
        output.plays == 2,
        "Natural backend completion cannot replace a retained source request");
    weapon.advance(.126F, {});
    weapon.shot({});
    require(
        output.plays == 3,
        "Source tail release clears identity even after backend completion");
    weapon.retire();
    require(!output.active({0, output.plays}),
            "Explicit world retirement stops retained request");
    weapon.shot({});
  }
  require(!output.active({0, output.plays}),
          "Destruction cannot leave a retained weapon request active");
  Output shots;
  {
    bw::game::WeaponAudio weapon(player, shots);
    weapon.shot({1, 2, 3});
    weapon.shot({2, 2, 3});
    require(shots.plays == 2 && shots.active({0, 1}) && shots.active({0, 2}) &&
                shots.stops == 0,
            "Nonretained source shots overlap instead of being deduplicated");
    weapon.advance(.126F, {});
    require(shots.active({0, 1}) && shots.active({0, 2}) && shots.stops == 0,
            "The retained firing tail cannot truncate independent one-shots");
    weapon.recharge({});
    require(shots.plays == 3 && shots.active({0, 2}) && shots.stops == 0,
            "Recharge detaches the previous one-shot without stopping it");
    weapon.retire();
    require(shots.active({0, 3}) && shots.stops == 0,
            "Owner retirement leaves nonretained completion to session output");
    weapon.shot({});
  }
  require(shots.active({0, 4}) && shots.stops == 0,
          "Destruction preserves independent playback until output cleanup");
}
