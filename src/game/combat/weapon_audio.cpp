// Preserve retained request replacement and the source firing tail. Cue loop
// behavior belongs to the original audio bank, not a guessed fire-rate loop.
// Both implementation corpora retrieved: R.1 (#rr-raii), MEM.9
// (guidelines/memory/MEM.9-embedded-allocate-at-init.md).
#include "weapon_audio.hpp"

#include <cmath>
#include <cstdio>
#include <stdexcept>

namespace bw::game {
namespace {
WeaponSoundCue cue(std::uint16_t index, bool retained = false) {
  return {audio::CueRequest{.bank = 0, .cue = index}, retained};
}
}  // namespace
WeaponSoundProfile WeaponSoundProfile::original(std::string_view weapon) {
  // Original action table20/21/22, contextual weapon classes0 and5. The
  // selected ordinary guns have no release cue. Token bit31 retains wg1 fire
  // (0x80000116); wa0 fire (0x000001b0) is an independent one-shot. Other
  // weapon classes need their own admission.
  constexpr std::uint16_t kPlayerFire = 0x1b0, kPlayerRecharge = 0x139;
  constexpr std::uint16_t kSoldierFire = 0x116, kSoldierRecharge = 0x13c;
  if (weapon != "wa0" && weapon != "wg1") {
    throw std::invalid_argument(
        "Weapon sound profile is outside admitted combat");
  }
  const bool player = weapon == "wa0";
  WeaponSoundProfile result{
      player ? cue(kPlayerFire) : cue(kSoldierFire, true),
      {},
      player ? cue(kPlayerRecharge) : cue(kSoldierRecharge),
      {}};
  constexpr std::uint16_t kDefaultImpact = 0x1e4;
  result.impact.fill(kDefaultImpact);
  struct SurfaceCue {
    std::size_t surface;
    std::uint16_t cue;
  };
  constexpr std::array overrides{
      SurfaceCue{2, 0xc2},  SurfaceCue{3, 0xc7},  SurfaceCue{4, 0xc6},
      SurfaceCue{5, 0xc3},  SurfaceCue{6, 0xc2},  SurfaceCue{7, 0xc2},
      SurfaceCue{8, 0xf6},  SurfaceCue{11, 0xc5}, SurfaceCue{13, 0xc2},
      SurfaceCue{14, 0xc5}, SurfaceCue{16, 0x90}, SurfaceCue{30, 0xc8},
      SurfaceCue{31, 0xca}, SurfaceCue{32, 0xcb}, SurfaceCue{33, 0xc5},
      SurfaceCue{34, 0xc6}, SurfaceCue{35, 0xc9}};
  for (const auto& entry : overrides) {
    result.impact.at(entry.surface) = entry.cue;
  }
  constexpr std::size_t kPlayerSurface = 18;
  constexpr std::uint16_t kPlayerSurfaceCue = 0x90;
  if (player) result.impact.at(kPlayerSurface) = kPlayerSurfaceCue;
  return result;
}
WeaponAudio::~WeaponAudio() {
  try {
    retire();
  } catch (...) {
    // Explicit world retirement reports failures. Destruction must still allow
    // session output teardown to retire remaining native handles.
    std::fputs(
        "Weapon audio retirement failed; session cleanup remains required\n",
        stderr);
  }
}
void WeaponAudio::request(Action action,
                          const std::optional<WeaponSoundCue>& cue_request,
                          const math::Vector3<float>& position, bool force) {
  if (!math::all_finite(position)) {
    throw std::invalid_argument("Invalid weapon audio emitter");
  }
  // Source wrapper identity outlives natural backend completion (6179c0).
  // I.1/E.4 and MEM.9 separate logical lifetime from backend observations.
  if (!force && retained_ && action == action_ && handle_) {
    if (output_.active(*handle_) && !output_.set_position(*handle_, position)) {
      throw std::runtime_error("Weapon sound emitter update failed");
    }
    return;
  }
  if (retained_ && handle_ && output_.active(*handle_) &&
      !output_.stop(*handle_)) {
    throw std::runtime_error("Weapon sound replacement failed");
  }
  // Destroying the original nonretained wrapper detaches its emitter but does
  // not stop playback. The session output owns natural completion and cleanup.
  handle_.reset();
  retained_ = false;
  action_ = action;
  if (cue_request) {
    auto request = cue_request->request;
    request.position = position;
    handle_ = output_.play(request);
    if (!handle_) throw std::runtime_error("Weapon sound admission failed");
    retained_ = cue_request->retained;
  }
}
void WeaponAudio::shot(const math::Vector3<float>& position) {
  request(Action::fire, profile_.fire, position, false);
  constexpr float kSourceTail = .125F;
  tail_seconds_ = kSourceTail;
}
void WeaponAudio::advance(float seconds, const math::Vector3<float>& position) {
  if (!std::isfinite(seconds) || seconds < 0 || !math::all_finite(position)) {
    throw std::invalid_argument("Invalid weapon audio update");
  }
  if (seconds == 0) return;
  if (handle_ && output_.active(*handle_) &&
      !output_.set_position(*handle_, position)) {
    throw std::runtime_error("Weapon sound emitter update failed");
  }
  if (tail_seconds_ > 0) {
    tail_seconds_ = std::max(0.F, tail_seconds_ - seconds);
    if (tail_seconds_ == 0 && retained_ && handle_) {
      request(Action::release, profile_.release, position, true);
    }
  }
}
void WeaponAudio::recharge(const math::Vector3<float>& position) {
  request(Action::recharge, profile_.recharge, position, true);
  tail_seconds_ = 0;
}
void WeaponAudio::retire() {
  if (retained_ && handle_ && output_.active(*handle_) &&
      !output_.stop(*handle_)) {
    throw std::runtime_error("Weapon sound retirement failed");
  }
  handle_.reset();
  retained_ = false;
  tail_seconds_ = 0;
}
}  // namespace bw::game
