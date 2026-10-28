// Native cockpit presentation owns transient gauge/weapon UI state; gameplay
// resources remain authoritative elsewhere. Inputs are copied scalar snapshots
// plus an externally supplied update interval. Outputs are ordered source-art
// commands borrowed until the next update/reset or destruction. Main-thread
// only, no retained player/clock pointers and no resource mutation capability.
// Validate observations before mutation; invalid ranges throw. Algorithm:
// sample four resource lanes -> advance native gauge trails/warnings and weapon
// notice -> build bounded source sprite commands -> surface resolves textures.
// Fixed storage bounds work independently of mission entity count; no frame
// allocations. Source mapping: gameplay-hud.md. Core F.16 (#rf-in), R.4
// (#rr-ref), implementation SL.con.3 (#rsl-bounds), performance MEM.9
// (guidelines/memory/MEM.9-embedded-allocate-at-init.md), consulted in both
// phases, enforce const observations and fixed capacity. This owner is stable
// and noncopyable/nonmovable; commands never outlive it.
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string_view>

#include "ammunition_hud.hpp"
#include "damage_direction_hud.hpp"
#include "hud_sprite.hpp"
#include "mission_goal_hud.hpp"
#include "mission_system_message.hpp"
#include "spell_selection.hpp"

namespace bw::game {
struct HudResourceMeter {
  float current{};
  float recoverable{};
  float maximum{1};
};
enum class HudTargetKind : std::uint8_t { neutral, target, hit };
struct GameplayHudSnapshot {
  HudResourceMeter health;
  HudResourceMeter magic;
  std::uint32_t ammunition{};
  std::uint32_t ammunition_capacity{};
  std::uint8_t weapon{};
  bool recharged{};
  bool enchanted{};
  bool visible{true};
  bool aiming{};
  bool magic_aim{};
  bool reticle_hidden{};
  HudTargetKind target{HudTargetKind::neutral};
  SpellRingPresentation magic_ring;
  const MissionGoalObservation* goal_observation{};
};
class GameplayHud {
 public:
  GameplayHud() = default;
  ~GameplayHud() = default;
  GameplayHud(const GameplayHud&) = delete;
  GameplayHud& operator=(const GameplayHud&) = delete;
  GameplayHud(GameplayHud&&) = delete;
  GameplayHud& operator=(GameplayHud&&) = delete;
  void set_goal_target(std::array<float, 3> position) {
    goal_.set_target(position);
  }
  void clear_goal_target() noexcept { goal_.clear_target(); }

  bool notify_damage(DamageDirection direction) {
    return damage_.request(direction);
  }
  void reset() noexcept;
  void advance(const GameplayHudSnapshot& observed, float seconds,
               SourceRandom& random);
  std::span<const HudSprite> commands() const noexcept;

 private:
  struct Gauge {
    float previous{};
    float trail{};
    float velocity{};
    bool draining{};
    bool gaining{};
    float gain_progress{};
    float gain_velocity{};
    float gain_amount{};
  };
  void append(HudSprite sprite);
  void meter(const HudResourceMeter& observed, Gauge& gauge, float y,
             render::Color color, float seconds);

  void weapon_notice(const GameplayHudSnapshot& observed, float seconds);
  void ring(const SpellRingPresentation& observed);
  static constexpr std::size_t kCommandCapacity = 256;
  std::array<HudSprite, kCommandCapacity> commands_{};
  std::size_t count_{};
  AmmunitionHud ammunition_;
  DamageDirectionHud damage_;
  MissionGoalHud goal_;
  Gauge health_;
  Gauge magic_;
  bool initialized_{};
  std::uint8_t weapon_{};
  enum class NoticePhase : std::uint8_t {
    begin,
    arriving,
    bounce,
    hold,
    hold_end,
    settle,
    settling,
    departing,
    finish,
    hide
  };
  NoticePhase notice_phase_{NoticePhase::begin};
  bool notice_visible_{};
  float notice_y_{};
  float notice_y_velocity_{};
  float notice_x_target_{};
  float notice_y_target_{};
  float notice_elapsed_{};
  float notice_x_{};
  float notice_velocity_{};
  float warning_elapsed_{};
  float warning_strength_{};
  bool warning_active_{};
  float reticle_hit_remaining_{};
};
// Narrow cockpit drawing boundary. Rendering receives const presentation;
// hide explicitly replaces any retained sprite list. No timing/input/resource
// authority crosses it. I.1/C.9 and MEM.9 apply to this borrowed synchronous
// output just as to bounded HUD commands.
class GameplayHudOutput {
 public:
  GameplayHudOutput() = default;
  GameplayHudOutput(const GameplayHudOutput&) = delete;
  GameplayHudOutput& operator=(const GameplayHudOutput&) = delete;
  GameplayHudOutput(GameplayHudOutput&&) = delete;
  GameplayHudOutput& operator=(GameplayHudOutput&&) = delete;
  virtual ~GameplayHudOutput() = default;
  virtual void show(const GameplayHud& hud, std::string_view diagnostic) = 0;
  // Independent text follows cockpit, pause and modal submissions; it never
  // inherits cockpit visibility. Immediate view borrow, no timing authority.
  virtual void show_system_message(SystemMessageView) {
    throw std::logic_error("HUD output does not support system text");
  }
  virtual void hide() noexcept = 0;
};
}  // namespace bw::game
