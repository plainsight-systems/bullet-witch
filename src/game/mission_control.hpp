// Bullet Witch mission pause and gameplay-input admission policy. This is the
// sole mutable pause authority. Stage borrows const observations;
// DeveloperReviewFlow receives only a user-pause command handle. The engine
// WorldClock owns time; this owner selects normal/control work and real versus
// neutral commands from the original independent flags. Fixed source stack
// saves menu admission, script pause and pad restriction, not user pause.
// Algorithm: apply requested control transition -> consume UI frame -> snapshot
// tick admission -> neutralize excluded input; evade requires release after UI.
// Other held axes resume normally. No device sampling, animation clock, I/O or
// allocation. All calls are on the mission owner thread. Checked stack errors
// preserve state; rejecting native overflow is an explicit safety departure.
// Full native script callbacks also own audio/player side effects; this policy
// does not claim those unreachable bindings implemented. See world-timing.md.
// Design Core C.9 (#rc-private) retains one authority and I.1 (#ri-explicit)
// exposes admission; MEM.9
// (guidelines/memory/MEM.9-embedded-allocate-at-init.md) keeps frame and stack
// storage bounded. Implementation: Core E.4 (#re-design-invariants) validates
// candidates before publication; MEM.9 was reread for allocation-free bounded
// update state. Ownership correction design: Core I.4 (#ri-typed) and Con.2
// (#rconst-fct) restrict each borrower to its real operations. COPY.5
// (guidelines/copy-move/COPY.5-resource-owning-types-are-move-only.md) excludes
// duplicated authority; moves are also forbidden while consumers borrow
// identity.
#pragma once
#include <array>
#include <cstddef>

#include "bw/world/world_clock.hpp"

namespace bw::game {
struct GameplayInput {
  std::array<float, 2> movement{}, look{}, mouse_look{};
  static constexpr unsigned kDefaultSensitivity = 60;
  unsigned turn_speed{kDefaultSensitivity}, mouse_bias{kDefaultSensitivity};
  bool keyboard_magic_pressed{}, keyboard_cycle_forward_pressed{},
      weapon_cycle_backward_pressed{};
  float fire_trigger{};
  bool evade_held{}, crouch_pressed{}, reload_pressed{}, attack_pressed{},
      weapon_cycle_pressed{}, aim_mode_pressed{};
  bool shoulder_left_pressed{}, shoulder_right_pressed{};
  bool shoulder_left_held{}, shoulder_right_held{};
  bool face_north_pressed{}, face_east_pressed{}, face_south_pressed{},
      face_west_pressed{};
};
enum class ScriptPause : std::uint8_t { simulation, input };
class MissionControl;
// The native pause task may observe/request user pause, but cannot mutate
// script nesting, input restriction or routing. A copied handle aliases that
// one owner; it has no independent flags and cannot expose the owner. It must
// retire before MissionControl. No default/nullable handle, allocation or
// virtual interface.
class UserPauseControl {
 public:
  bool user_paused() const noexcept;
  bool set_user_paused(bool paused);

 private:
  friend class MissionControl;
  explicit UserPauseControl(MissionControl& owner) noexcept;
  MissionControl* owner_;
};
class MissionControl {
 public:
  MissionControl() = default;
  MissionControl(const MissionControl&) = delete;
  MissionControl& operator=(const MissionControl&) = delete;
  MissionControl(MissionControl&&) = delete;
  MissionControl& operator=(MissionControl&&) = delete;
  ~MissionControl() = default;
  UserPauseControl user_pause_control() noexcept;
  bool user_paused() const noexcept { return user_paused_; }
  bool simulation_paused() const noexcept {
    return user_paused_ || script_.simulation;
  }
  bool audio_paused() const noexcept { return simulation_paused(); }
  bool pause_requests_allowed() const noexcept { return script_.menu; }
  bool mission_script_admitted() const noexcept { return !user_paused_; }
  // Return acceptance; repeated same-state requests succeed without mutation.
  bool set_user_paused(bool paused);
  void push_script_pause(ScriptPause pause);
  void pop_script_pause();
  void restrict_hardware_input(bool restricted) noexcept {
    input_restricted_ = restricted;
  }
  world::TickAdmission admission(bool ui_consumed = false) const noexcept;
  GameplayInput route(GameplayInput sampled, bool ui_consumed);

 private:
  struct ScriptState {
    bool menu{true}, simulation{}, input{};
  };
  ScriptState script_;
  static constexpr std::size_t kMaximumScriptPauses = 8;
  std::array<ScriptState, kMaximumScriptPauses> stack_{};
  std::size_t depth_{};
  bool user_paused_{}, input_restricted_{}, evade_rearmed_{true};
};
}  // namespace bw::game
