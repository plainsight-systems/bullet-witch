// Retained scene lifetime over one borrowed WorldPresentation backend. The
// backend remains the sole draw/resource registry; this owner retains only
// admission state and an embedded update-only actor capability. Main-thread,
// exclusive access; backend and resident mission assets outlive the scene and
// publishers. No publisher reference may outlive this stable owner.
// Algorithm: claim empty output -> configure -> register draws transactionally
// -> publish completed updates/replacements -> retire retained submissions ->
// release mission resources. Registration failure remains preparing; retirement
// is idempotent and constructor unwinding retires a successfully registered
// scene. Reusing retired publishers rejects before touching a later scene.
// Inputs are borrowed only for each call; backend copies palettes/shape bytes
// into its existing storage. No facade allocations or full-scene copies.
// Core I.1 (#ri-explicit), F.7 (#rf-smart), E.6 (#re-raii), performance MEM.9
// (guidelines/memory/MEM.9-embedded-allocate-at-init.md) and COPY.7
// (guidelines/ copy-move/COPY.7-eliminate-hidden-copies.md) inform header
// design; E.6 and MEM.9 were reread for implementation. These rules enforce
// explicit borrows, single retirement authority and allocation-free updates.
// See research/engine/subsystems/render-scene-lifetime.md.
#pragma once
#include "bw/render/world.hpp"

namespace bw::render {
class RenderScene;
// Scene lighting borrows only coefficient publication. It cannot register,
// remove or retire draws, alter poses, or access backend resources. F.7/C.32
// make the stable owner borrow explicit; COPY.4 deletes identity transfer.
class LightingPublisher {
 public:
  ~LightingPublisher() = default;
  LightingPublisher(const LightingPublisher&) = delete;
  LightingPublisher& operator=(const LightingPublisher&) = delete;
  LightingPublisher(LightingPublisher&&) = delete;
  LightingPublisher& operator=(LightingPublisher&&) = delete;
  void publish(std::span<const WorldLightingUpdate> updates);

 private:
  friend class RenderScene;
  explicit LightingPublisher(RenderScene& scene) : scene_(scene) {}
  RenderScene& scene_;
};
// Actor-originated publication is deliberately narrower than scene ownership.
// Callers identify their own submitted model/draw destinations; the backend
// validates every destination. Visibility cannot alter shadow/opacity policy.
class ActorPublisher {
 public:
  ~ActorPublisher() = default;
  ActorPublisher(const ActorPublisher&) = delete;
  ActorPublisher& operator=(const ActorPublisher&) = delete;
  ActorPublisher(ActorPublisher&&) = delete;
  ActorPublisher& operator=(ActorPublisher&&) = delete;
  void update_poses(std::span<const WorldPoseUpdate> updates);
  void update_shapes(std::span<const WorldShapePositionUpdate> updates);
  void update_visibility(std::span<const WorldVisibilityUpdate> updates);

 private:
  friend class RenderScene;
  explicit ActorPublisher(RenderScene& scene) : scene_(scene) {}
  RenderScene& scene_;
};
class RenderScene {
 public:
  // Empty output is a caller precondition (legacy direct submissions are not
  // inspected). Rejects a second scene owner; claims admission only.
  explicit RenderScene(WorldPresentation& output);
  ~RenderScene();
  RenderScene(const RenderScene&) = delete;
  RenderScene& operator=(const RenderScene&) = delete;
  RenderScene(RenderScene&&) = delete;
  RenderScene& operator=(RenderScene&&) = delete;
  ActorPublisher& actor_publisher() { return actor_; }
  LightingPublisher& lighting_publisher() { return lighting_; }
  WorldGeometryStatus geometry_status(WorldModelHandle model) const;
  // Preparing or active; initial authored output configuration precedes draws.
  void set_world_output_parameters(const WorldOutputParameters& parameters);
  // One successful registration; backend validates/copies before activation.
  void register_draws(std::span<const WorldColorDraw> draws);
  // Active-only; replacements require nonempty new-model draws. Explicit
  // removal retains resident model/textures and unrelated draw/view state.
  void replace_model_draws(WorldModelHandle previous,
                           std::span<const WorldColorDraw> replacement);
  void remove_model_draws(WorldModelHandle model);
  void update_world_texture_offsets(
      std::span<const WorldTextureOffsetUpdate> updates);
  void update_world_visibility(std::span<const WorldVisibilityUpdate> updates);
  void update_world_view(const WorldViewUpdate& update);
  // Existing host synchronously completes submissions before returning. No
  // asynchronous fence or new GPU wait is introduced here. Assets remain owned
  // by WorldResources until the mission's later residency teardown.
  void retire() noexcept;

 private:
  friend class ActorPublisher;
  friend class LightingPublisher;
  enum class State : std::uint8_t { preparing, active, retired };
  void require_active() const;
  void require_live() const;
  WorldPresentation& output_;
  State state_{State::preparing};
  ActorPublisher actor_{*this};
  LightingPublisher lighting_{*this};
};
}  // namespace bw::render
