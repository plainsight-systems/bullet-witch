// Stateful CPU secondary-bone chains, separate from world rigid-body physics.
// Caller prepares immutable topology and supplies clean animated control-point
// and bone frames. Construction validates all indices/links and owns copied
// definitions, state and scratch in the injected domain. reset establishes
// source bind lengths; advance integrates finite nonnegative time, constrains
// segments to authored capsules/cones, and publishes declared bone overrides.
// Callers serialize update/evaluation and apply overrides before
// skin/attachments. Inputs are borrowed only for a call; output spans last
// until next update. Invalid data throws before publication. Nodes retain
// source traversal order; no worker, GPU API, resource name, tuning table or
// allocator is hidden. Algorithm: prepare anchors/intermediate frames/rest
// distances; each update skins body capsules, updates anchored chains,
// integrates velocity/wind and neighbor forces, applies segment/contact/angular
// constraints, reconstructs frames, then publishes world-space bone overrides.
// O(nodes*capsules) worst case, fixed memory after construction; no
// steady-state allocation. Both phases: Core I.1 (#ri-explicit), SL.con.3
// (#rsl-bounds); performance MEM.9
// (guidelines/memory/MEM.9-embedded-allocate-at-init.md) fixes storage. Source
// equations and Unreal ownership reference: costume-secondary-motion.md.
#pragma once
#include "bw/animation/pose.hpp"
#include "bw/formats/control_points.hpp"
namespace bw::animation {
using SecondaryVector = math::Vector3<float>;
enum class SecondaryNodeKind : std::uint8_t {
  frame,
  midpoint,
  adjacent_midpoint
};
struct SecondaryNodeDefinition {
  SecondaryNodeKind kind{};
  std::int32_t control_point{-1}, bone{-1}, parent{-1}, child{-1};
  std::int32_t neighbor_a{-1}, neighbor_b{-1};
  std::uint32_t collider_mask{};
  float neighbor_stiffness{};
};
struct SecondaryCapsuleDefinition {
  std::size_t bone{};
  SecondaryVector origin{}, axis{};
  float half_length{}, radius{};
};
struct SecondaryCapsule {
  SecondaryVector origin{}, axis{};
  float half_length{}, radius{};
};
// Three weighted authored points form an orthonormal world frame. Absent slots
// produce identity; callers must validate every requested slot is present.
void evaluate_control_points(const formats::ControlPointDefinition& definition,
                             std::span<const Matrix> skin_world,
                             std::span<Matrix> frames);
class SecondaryMotion {
 public:
  SecondaryMotion(std::span<const SecondaryNodeDefinition> nodes,
                  std::span<const SecondaryCapsuleDefinition> capsules,
                  std::size_t bones, std::size_t control_points,
                  const std::shared_ptr<memory::Domain>& domain);
  void reset(std::span<const Matrix> control_frames,
             std::span<const Matrix> bone_world);
  void advance(float delta_seconds, float elapsed_seconds, float ground_height,
               std::span<const Matrix> control_frames,
               std::span<const Matrix> bone_world, bool first_update = false);
  std::span<const BoneWorldOverride> overrides() const { return overrides_; }
  std::span<const SecondaryCapsule> capsules() const { return capsules_; }
  // Borrowed diagnostic positions, matching immutable node order.
  std::span<const SecondaryVector> positions() const { return positions_; }

 private:
  struct Node {
    SecondaryVector position{}, velocity{}, segment_force{}, neighbor_force{};
    SecondaryVector rest_child{};
    float length{}, neighbor_length{};
    Matrix frame{kIdentityMatrix};
  };
  void validate_inputs(std::span<const Matrix> controls,
                       std::span<const Matrix> bones) const;
  struct ChainUpdate {
    float delta_seconds{}, ground_height{};
  };
  void solve_chain(std::size_t root, const ChainUpdate& update,
                   const SecondaryVector& wind, float movement_response,
                   std::span<const Matrix> controls, bool first_update);
  void publish();
  memory::Vector<SecondaryNodeDefinition> definitions_;
  memory::Vector<SecondaryCapsuleDefinition> capsule_definitions_;
  memory::Vector<SecondaryCapsule> capsules_;
  memory::Vector<Node> nodes_, pending_;
  memory::Vector<BoneWorldOverride> overrides_;
  memory::Vector<SecondaryVector> positions_;
  std::size_t bone_count_{}, control_count_{};
  bool initialized_{};
};
}  // namespace bw::animation
