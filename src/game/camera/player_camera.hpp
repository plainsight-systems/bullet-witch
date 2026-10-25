// Own the live behind-player camera: independent look orientation,
// actor-position following and retained collision-shortened offsets. Settings
// are copied; collision is borrowed and must outlive this single-game-thread
// owner. Finite poses, normalized look axes and finite nonnegative seconds are
// required; invalid input throws without changing published state. Reset from
// a scripted pose; advance look, restore offsets, resolve
// vertical/shoulder/rear clearance, then publish a complete view. Actor facing
// does not overwrite look. Pause belongs to the caller withholding advance.
// Fixed storage, no successful path allocation; three static segment queries
// dominate update cost. Both MCP phases: I.1 (ri-explicit) makes input,
// lifetime and failure contracts explicit; MEM.9
// (guidelines/memory/MEM.9-embedded-allocate-at-init.md) retains fixed state.
// Source and bounded angular reconstruction: player-camera-follow.md.
#pragma once
#include "initial_camera.hpp"

namespace bw::game {
enum class PlayerCameraContext : std::uint8_t { ordinary, targeting };
struct PlayerCameraLook {
  float right{}, up{};
};
class PlayerCamera {
 public:
  PlayerCamera(const formats::PlayerCameraSettings& settings,
               const ScriptedPlayerPose& player,
               const scene::StaticCollision& collision);
  const InitialPlayerCamera& advance(const ScriptedPlayerPose& player,
                                     PlayerCameraLook look,
                                     double delta_seconds);
  // Select an authored preset without consuming time or look. Repeated requests
  // are no-ops. A change snapshots the visible camera and starts the source
  // quarter-second rational transition toward the live target. Invalid context
  // throws without mutation; pause withholds advance. Independent of raised
  // animation lifetime. Both-phase I.1/MEM.9: explicit mode, fixed state.
  void set_context(PlayerCameraContext context, bool crouched);
  // Publish the current region's far clip without changing pose/look or
  // consuming a simulation interval. Finite distance greater than near is
  // required; failure preserves the view. I.1/MEM.9, both redesign phases.
  void set_far_distance(float distance);
  void teleport(const ScriptedPlayerPose& player);
  const InitialPlayerCamera& view() const { return view_; }
  float yaw_radians() const { return yaw_; }
  float pitch_radians() const { return pitch_; }

 private:
  void blend_transition(double seconds,
                        const std::array<float, 3>& previous_eye);
  void resolve(const std::array<float, 3>& position);
  formats::PlayerCameraSettings settings_;
  const scene::StaticCollision* collision_;
  InitialPlayerCamera view_{};
  PlayerCameraContext context_{PlayerCameraContext::ordinary};
  bool crouched_{};
  formats::PlayerCameraPreset preset_;
  InitialPlayerCamera transition_from_{};
  std::array<float, 4> rotation_{0, 0, 0, 1}, transition_rotation_{0, 0, 0, 1};
  double transition_elapsed_{}, transition_duration_{};
  float yaw_{}, pitch_{}, height_{};
  std::array<float, 3> offset_{};
};
}  // namespace bw::game
