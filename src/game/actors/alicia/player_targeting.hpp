// Own ordinary player targeting selection independently from weapon posture.
// The installed, unenchanted weapons expose modes0/1. A press cycles once;
// release/held filtering belongs to the native input adapter. Mode0 stops
// target selection immediately. Its camera request returns to ordinary on the
// following owner update, matching62ebe0 before62ed80; raised-animation
// lifetime remains with PlayerController. Explicit preview commands use this
// same owner, never a second latch. Single game thread, fixed state, no
// allocation. Finite nonnegative simulation seconds advance only the source
// short camera grace, never weapon posture. I.1/E.4 make ownership and phase
// explicit; MEM.9 keeps fixed frame state. Both MCP corpora queried/read for
// header design and implementation. See player-targeting.md for installed
// camera flag0 and excluded enchant modes.
#pragma once
namespace bw::game {
class PlayerTargeting {
 public:
  void advance(float seconds, bool cycle_pressed, bool fire_targeting,
               bool automatic_camera);
  // An explicit review selection is not hardware input. It publishes the
  // selected context together; stopping a preview selects ordinary mode.
  void select_for_review(bool targeted) noexcept;
  // A forced message clears targeting. Ordinary action entries also request
  // camera2; spell camera scopes remain external and must not be overwritten.
  void enter_forced_action(bool ordinary_camera) noexcept;
  bool selected() const noexcept { return selected_; }
  bool active() const noexcept { return active_; }
  bool camera_targeted() const noexcept { return camera_targeted_; }

 private:
  bool selected_{}, active_{}, camera_targeted_{};
  float camera_grace_{};
};
}  // namespace bw::game
