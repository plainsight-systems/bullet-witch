// Native magic-ring selection owns only UI state and produces value requests.
// The caller supplies original learned/affordability/context observations and
// edge actions; no player, resource, world or clock mutation capability is
// borrowed. Main-thread value state; advance rejects invalid time/page/choice
// before mutation. Algorithm: open/refresh -> cycle source pages -> admit an
// available choice -> publish spell/cancel once -> expire inactivity and close.
// Presentation samples supplied intervals, never a wall clock. Fixed arrays
// bound nine spells/four choices and allocate nothing. Source mapping and
// remaining producer boundaries: gameplay-hud.md. Design Core F.16 (#rf-in)
// keeps observations const; implementation SL.con.3 (#rsl-bounds) bounds pages.
// Performance MEM.9 (guidelines/memory/MEM.9-embedded-allocate-at-init.md),
// queried in both phases, fixes frame storage rather than growing UI
// containers.
#pragma once
#include <array>
#include <cstdint>
#include <optional>

#include "progression.hpp"

namespace bw::game {
struct SpellAvailability {
  bool learned{};
  // Includes magic cost, script restrictions and the spell's real context.
  bool permitted{};
};
inline constexpr std::size_t kSpellCount = 9;
using SpellAvailabilitySet = std::array<SpellAvailability, kSpellCount>;
struct SpellSelectionInput {
  bool open{};
  bool previous_page{};
  bool next_page{};
  // Original face choice: 1=up/cancel, 2=right, 3=down, 4=left.
  std::uint8_t choice{};
  bool cancel{};
  // Native combined-shoulder command expires input ownership while the
  // opening/page/closing presentation finishes its existing phases.
  bool expire{};
};
struct SpellSelectionResult {
  std::optional<PlayerSpell> requested;
  bool cancelled{};
  bool rejected{};
};
struct SpellRingPresentation {
  bool visible{};
  std::uint8_t page{1};
  std::uint8_t selected_choice{};
  std::array<SpellAvailability, 4> choices{};
  float opacity{};
  float elapsed_seconds{};
  float rotation_radians{};
  float center_scale{1};
  float ring_scale{1};
  float name_opacity{};
  float flame_scale{1};
  float art_opacity{};
  float art_rotation{};
  float art_scale{1};
  float selection_progress{};
};
class SpellSelectionFlow {
 public:
  SpellSelectionResult advance(const SpellSelectionInput& input,
                               const SpellAvailabilitySet& availability,
                               float seconds);
  SpellRingPresentation presentation() const;
  bool active() const noexcept { return active_ && !closing_; }
  void reset() noexcept;
  static std::optional<PlayerSpell> spell(std::uint8_t page,
                                          std::uint8_t choice);

 private:
  enum class Phase : std::uint8_t {
    opening,
    idle,
    page_change,
    publish,
    selected,
    expire,
    release,
    close_begin,
    close_prepare,
    closing
  };
  enum class Response : std::uint8_t { linear, exponential, damped };
  struct Scalar {
    float value{}, target{}, parameter{1}, velocity{};
    Response response{Response::linear};
    void update(float seconds) noexcept;
  };
  void begin() noexcept;
  void close() noexcept;
  SpellAvailabilitySet availability_{};
  bool active_{};
  bool closing_{};
  // Source previous-frame keepalive permits immediate expiry recovery without
  // resetting the retained page or its ongoing visual phase.
  bool prior_keepalive_{};
  Phase phase_{Phase::opening};
  std::uint8_t phase_step_{};
  std::uint8_t page_{1};
  std::uint8_t selected_{};
  float phase_elapsed_{};
  float elapsed_{};
  float rotation_elapsed_{};
  float inactive_{};
  Scalar opacity_;
  Scalar center_scale_;
  Scalar ring_scale_;
  Scalar name_opacity_;
  Scalar flame_scale_;
  Scalar art_opacity_;
  Scalar art_rotation_;
  Scalar art_scale_;
  Scalar selection_progress_;
};
}  // namespace bw::game
