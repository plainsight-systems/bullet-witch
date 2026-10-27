// Primary mission goal presentation reconstructed from the original goal
// receiver/update/draw, not a gameplay objective owner. The script adapter
// retains one target; clear withdraws it and allows the original fade to
// finish. Each admitted world frame supplies const camera/player observations
// and its interval. No retained world pointers, clock, input, allocation or
// authority. Commands borrow fixed storage until advance/reset. Source
// geometry, phase sequencing and tile selection: mission-goal-hud.md. Core F.16
// (#rf-in) and performance MEM.9
// (guidelines/memory/MEM.9-embedded-allocate-at-init.md), consulted in both
// phases, bound observation borrowing and steady-state work.
#pragma once
#include <array>
#include <span>

#include "bw/render/scene/view.hpp"
#include "hud_sprite.hpp"

namespace bw::game {
struct MissionGoalObservation {
  render::ViewPose camera;
  render::PerspectiveView perspective;
  std::array<float, 3> player_position{};
};
class MissionGoalHud {
 public:
  void set_target(std::array<float, 3> position);
  void clear_target() noexcept { supplied_ = false; }
  void reset() noexcept;
  void advance(const MissionGoalObservation& observed, float seconds);
  std::span<const HudSprite> commands() const noexcept;

 private:
  enum class Phase : std::uint8_t {
    hidden,
    initial,
    begin,
    live,
    fade,
    fading,
    retire
  };
  std::array<float, 3> target_{};
  bool supplied_{};
  Phase phase_{Phase::hidden};
  float opacity_{}, rotation_elapsed_{}, pulse_elapsed_{};
  static constexpr std::size_t kCommandCapacity = 14;
  std::array<HudSprite, kCommandCapacity> commands_{};
  std::size_t count_{};
};
}  // namespace bw::game
