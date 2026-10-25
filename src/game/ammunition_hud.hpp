// Original ammunition cockpit presentation: two weapon-transition children,
// each retaining three banks of twenty cartridge sprites. Source observations
// and explicit recharge receipts drive bank replacement/ejection; live ammo is
// never owned here. Ejection borrows the session RNG during advance, not a
// hidden seeded stream. Commands borrow inline storage until next advance/reset
// or destruction. Single owner thread; fixed bounded storage/work, no heap.
// Algorithm: validate -> select child -> reconcile cartridge row -> update
// source phases -> publish old/new child commands. Source: ammunition-hud.md.
// Design R.4 (#rr-ref), implementation E.4 (#re-design-invariants), both-phase
// MEM.9 (guidelines/memory/MEM.9-embedded-allocate-at-init.md) retain explicit
// borrows and bounded state. The presentation owner cannot mutate resources.
#pragma once
#include <array>
#include <span>

#include "game/hud_sprite.hpp"
#include "game/source_random.hpp"

namespace bw::game {
// Native cockpit stores the ammunition count in a nine-bit field.
inline constexpr unsigned ammunition_hud_maximum = 511;
struct AmmunitionObservation {
  unsigned weapon{};
  unsigned ammunition{};
  bool recharged{};
  bool enchanted{};
};
class AmmunitionHud {
 public:
  AmmunitionHud() = default;
  AmmunitionHud(const AmmunitionHud&) = delete;
  AmmunitionHud& operator=(const AmmunitionHud&) = delete;
  AmmunitionHud(AmmunitionHud&&) = delete;
  AmmunitionHud& operator=(AmmunitionHud&&) = delete;
  ~AmmunitionHud() = default;
  void reset() noexcept;
  void advance(AmmunitionObservation observed, float seconds,
               SourceRandom& random);
  std::span<const HudSprite> commands() const noexcept;

 private:
  static constexpr std::size_t kCartridgeCount = 20, kBankCount = 3;
  enum class CartridgePhase : std::uint8_t { idle, ejecting, retiring, hidden };
  struct Cartridge {
    float x{}, y{}, direction_x{}, direction_y{}, speed{}, rotation_speed{};
    float rotation{}, gravity{}, alpha{1}, elapsed{};
    CartridgePhase phase{CartridgePhase::idle};
    bool active{}, ejected{};
  };
  enum class ChildPhase : std::uint8_t {
    begin,
    visible,
    depart,
    departing,
    finish
  };
  struct Child {
    std::array<std::array<Cartridge, kCartridgeCount>, kBankCount> banks{};
    unsigned weapon{}, ammunition{}, row{}, bank{};
    bool active{}, depart_requested{}, enchanted{}, flash{}, flash_finished{};
    bool burst{};
    ChildPhase phase{ChildPhase::begin};
    float elapsed{}, arrival{}, departure{};
    float tint{}, tint_velocity{}, flash_elapsed{}, flash_value{};
    float burst_alpha{}, burst_scale{};
  };
  void initialize(Child& child, AmmunitionObservation observed);
  void set_count(Child& child, AmmunitionObservation observed,
                 SourceRandom& random);
  void eject(Cartridge& cartridge, unsigned weapon, SourceRandom& random);
  void update(Child& child, float seconds);
  void draw(const Child& child);
  void append(HudSprite sprite);
  static constexpr std::size_t kCommandCapacity = 192;
  std::array<HudSprite, kCommandCapacity> commands_{};
  std::size_t count_{};
  std::array<Child, 2> children_{};
  unsigned selected_{};
  bool initialized_{}, damping_initialized_{};
  float damping_{};
};
}  // namespace bw::game
