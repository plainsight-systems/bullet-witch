// Source-initialized NPC presentation instances. Mission preparation owns the
// immutable model, selected draw policy and decoded initial motion; each actor
// borrows them while owning its playback, local pose and skin palettes. The
// world alone supplies elapsed simulation time and placement. This is the
// original grounded Runner initialization, not a behavior-tree simulation.
// Prepare palettes once, advance the selected original controller, suppress
// its consumed root, compose the model and completed hand attachment, then
// publish only this instance identity. No archive reads/uploads or allocations
// during advance. Failure during construction publishes nothing. Core I.11
// (#ri-raw), E.6 (#re-raii), and performance MEM.9
// (guidelines/memory/MEM.9-embedded-allocate-at-init.md), read for both phases,
// preserve immutable shared inputs and distinct mutable instance state.
#pragma once
#include "bw/animation/playback_ring.hpp"
#include "bw/render/scene/render_scene.hpp"
#include "game/world/mission_population.hpp"
#include "npc_appearance.hpp"
#include "npc_initial_motion.hpp"
namespace bw::game {
struct PreparedNpcModel {
  ResidentModelView resident;
  NpcInitialMotion motion;
  PreparedNpcAppearance appearance;
};
class NpcModelPose {
 public:
  NpcModelPose(const PreparedNpcModel&, const std::shared_ptr<memory::Domain>&);
  NpcModelPose(const NpcModelPose&) = delete;
  NpcModelPose& operator=(const NpcModelPose&) = delete;
  NpcModelPose(NpcModelPose&&) = delete;
  NpcModelPose& operator=(NpcModelPose&&) = delete;
  ~NpcModelPose() = default;
  void advance(double seconds, const animation::Matrix& transform);
  void publish(std::uint64_t instance, render::ActorPublisher&) const;
  const PreparedNpcModel& prepared() const { return prepared_; }
  std::span<const render::WorldJointMatrix> palette(std::size_t draw) const;
  std::span<const animation::Matrix> bones() const {
    return pose_.bone_transforms();
  }

 private:
  static constexpr std::size_t kPaletteCapacity = 32;
  struct Palette {
    std::array<std::size_t, kPaletteCapacity> bones{};
    std::array<render::WorldJointMatrix, kPaletteCapacity> matrices{};
    std::size_t draw{}, count{};
  };
  const PreparedNpcModel& prepared_;
  animation::PlaybackRing playback_;
  animation::SkeletonPose pose_;
  memory::Vector<formats::SkeletonTransform> locals_;
  memory::Vector<Palette> palettes_;
  render::WorldJointMatrix model_transform_{};
  std::size_t root_{};
};
class NpcActor {
 public:
  NpcActor(PopulationIdentity, PopulationActorState,
           const PreparedNpcModel& body, const PreparedNpcModel* weapon,
           float body_radius, const std::shared_ptr<memory::Domain>&);
  NpcActor(const NpcActor&) = delete;
  NpcActor& operator=(const NpcActor&) = delete;
  NpcActor(NpcActor&&) = delete;
  NpcActor& operator=(NpcActor&&) = delete;
  ~NpcActor() = default;
  void advance(double seconds, render::ActorPublisher&);
  PopulationIdentity identity() const { return identity_; }
  const PopulationActorState& state() const { return state_; }
  const NpcModelPose& body() const { return body_; }
  const NpcModelPose* weapon() const { return weapon_ ? &*weapon_ : nullptr; }
  const animation::Matrix& model_transform(bool weapon) const {
    return weapon ? weapon_transform_ : body_transform_;
  }

 private:
  void evaluate(double seconds);
  PopulationIdentity identity_;
  PopulationActorState state_;
  NpcModelPose body_;
  std::optional<NpcModelPose> weapon_;
  std::optional<std::size_t> attachment_;
  animation::Matrix body_transform_, weapon_transform_;
};
}  // namespace bw::game
