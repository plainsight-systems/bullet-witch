// One source weapon's retained contextual sound owner. Immutable prepared cue
// identities outlive this instance; the session output outlives destruction.
// The world supplies admitted delta and copied emitter positions; no second
// clock, bank loading or per-shot allocation here. A shot refreshes the source
// eighth-second tail. Only a source-retained matching request is deduplicated
// or stopped at tail expiry. Nonretained one-shots finish independently in the
// session output; mission teardown retires its world domain. Source token bit31
// controls this ownership, independently of the bank's loop metadata.
// A completed backend voice does not complete its retained source request.
// Only replacement/release/retirement clears that identity (6179c0/6157d0).
// Design I.1/E.4 and MEM.9 keep backend observations separate from owner state.
// Pause passes no admitted delta and the session pauses the output domain.
// Invalid input/backend failure throws; destructor performs best-effort cleanup
// after explicit retire, with no exception escape. Fixed O(1) state and work.
// Source615fc0/617a30/618410/46e710; Core R.1 (#rr-raii), performance MEM.9
// (guidelines/memory/MEM.9-embedded-allocate-at-init.md), design retrieved.
#pragma once
#include "bw/audio/output.hpp"
#include "weapon_definition.hpp"
namespace bw::game {
struct WeaponSoundCue {
  audio::CueRequest request;
  bool retained{};
};
struct WeaponSoundProfile {
  std::optional<WeaponSoundCue> fire, release, recharge;
  // Original contextual action12, indexed by copied game surface identity.
  std::array<std::uint16_t, WeaponEffects::kSurfaceCount> impact{};
  static WeaponSoundProfile original(std::string_view weapon);
};
class WeaponAudio {
 public:
  WeaponAudio(WeaponSoundProfile profile, audio::CueOutput& output)
      : profile_(profile), output_(output) {}
  ~WeaponAudio();
  WeaponAudio(const WeaponAudio&) = delete;
  WeaponAudio& operator=(const WeaponAudio&) = delete;
  WeaponAudio(WeaponAudio&&) = delete;
  WeaponAudio& operator=(WeaponAudio&&) = delete;
  void shot(const math::Vector3<float>& position);
  void advance(float seconds, const math::Vector3<float>& position);
  void recharge(const math::Vector3<float>& position);
  void retire();

 private:
  enum class Action : std::uint8_t { fire, release, recharge };
  void request(Action action, const std::optional<WeaponSoundCue>& cue,
               const math::Vector3<float>& position, bool force);
  WeaponSoundProfile profile_;
  audio::CueOutput& output_;
  std::optional<audio::CueHandle> handle_;
  Action action_{Action::release};
  float tail_seconds_{};
  bool retained_{};
};
}  // namespace bw::game
