// Four source damage channels retain admission/visibility and normalized age.
// Combat publishes copied directions; this UI owner never receives an actor,
// world clock or physics capability. A visible channel rejects retriggers.
// Admitted visual intervals advance one source task phase, then publish at most
// twelve ordered source-art commands. Paused owners are not advanced by the
// cockpit. Commands borrow inline storage until advance/reset/destruction.
// Source4cfb30/4cfb70/4cee70; Core I.1 (#ri-explicit) and performance MEM.9
// (guidelines/memory/MEM.9-embedded-allocate-at-init.md), both phases
// consulted, keep authority narrow and frame storage fixed.
#pragma once
#include <array>
#include <span>

#include "game/combat/damage_feedback.hpp"
#include "hud_sprite.hpp"
namespace bw::game {
class DamageDirectionHud {
 public:
  bool request(DamageDirection direction);
  void advance(float seconds);
  void reset() noexcept;
  std::span<const HudSprite> commands() const noexcept;

 private:
  enum class Phase : std::uint8_t {
    idle,
    requested,
    begin,
    live,
    finish,
    retire
  };
  struct Channel {
    Phase phase{};
    float elapsed{}, progress{};
    bool requested{}, visible{};
  };
  void draw(std::size_t direction, float progress);
  std::array<Channel, 4> channels_{};
  static constexpr std::size_t kCommandCapacity = 12;
  std::array<HudSprite, kCommandCapacity> commands_{};
  std::size_t count_{};
};
}  // namespace bw::game
