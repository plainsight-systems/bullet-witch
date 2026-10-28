// Original bone-bound EHIT receiver geometry, separate from skeletal dynamics.
// Preparation copies mesh data into immutable engine shapes once per resident
// family. Instances retain that preparation, own selected affine query
// instances, and publish completed inverse-bind skin transforms through the
// world's bounded command queue. Source 5f9fd0/5fa290 selects flags group bits
// and rigid kind1; unsupported deforming kinds fail explicitly. Triangle
// metadata retains both body region and surface bytes. The caller supplies
// side-layer/actor identity; damage, solver advancement and animation ownership
// never enter this adapter. World capabilities are weak. Instances retire query
// instances before shared geometry releases shapes; expired worlds have already
// retired all resources. Invalid preparation rolls back. No copies/moves of
// live owners. Setup is O(vertices
// + faces + parts); updates O(selected parts), with fixed admission storage.
// Core R.1 (#rr-raii) owns provider resources; performance CDSA.32
// (guidelines/cpu-dsa/CDSA.32-transform-static-data-into-the-consumer-layout.md)
// prepares shared immutable consumer geometry once. Both corpora retrieved
// before design. Affine extension: both phases Core I.4 (#ri-typed)
// separates query instances from solver bodies; MEM.9 bounds update storage.
// Evidence: first-combat-exchange.md.
#pragma once
#include "bw/animation/pose.hpp"
#include "bw/content/formats/stage_definitions.hpp"
#include "bw/physics/world.hpp"
namespace bw::game {
class ActorHitGeometry {
 public:
  ActorHitGeometry(physics::Resources resources,
                   const formats::CollisionDefinition& definition,
                   const std::shared_ptr<memory::Domain>& domain);
  ~ActorHitGeometry();
  ActorHitGeometry(const ActorHitGeometry&) = delete;
  ActorHitGeometry& operator=(const ActorHitGeometry&) = delete;
  ActorHitGeometry(ActorHitGeometry&&) = delete;
  ActorHitGeometry& operator=(ActorHitGeometry&&) = delete;
  struct Part {
    physics::ShapeId shape;
    std::uint32_t bone{}, group{};
  };
  std::span<const Part> parts() const { return parts_; }

 private:
  void retire();
  physics::Resources resources_;
  memory::Vector<Part> parts_;
};
class ActorHitInstance {
 public:
  static constexpr std::size_t kGroupWordCount = 8;
  ActorHitInstance(physics::Resources resources, physics::Mutations mutations,
                   std::shared_ptr<const ActorHitGeometry> geometry,
                   std::span<const std::uint32_t, kGroupWordCount> groups,
                   const animation::SkeletonPose& pose, std::uint64_t actor,
                   std::uint8_t layer,
                   const std::shared_ptr<memory::Domain>& domain);
  ~ActorHitInstance();
  ActorHitInstance(const ActorHitInstance&) = delete;
  ActorHitInstance& operator=(const ActorHitInstance&) = delete;
  ActorHitInstance(ActorHitInstance&&) = delete;
  ActorHitInstance& operator=(ActorHitInstance&&) = delete;
  // Borrow the completed pose synchronously; select its skin palette here so
  // callers cannot substitute raw bone-world matrices for bind-space vertices.
  // Both-phase I.4/MEM.9 enforce this coordinate contract without copying.
  void publish(const animation::SkeletonPose& pose);
  // Mask updates preserve other active source receiver flags.
  void query_flags(std::uint32_t mask, bool enabled);
  // Explicitly remove damage receivers on source retirement/death policy.
  void retire();

 private:
  struct Part {
    physics::QueryInstanceId instance;
    std::uint32_t bone{};
  };
  physics::Resources resources_;
  physics::Mutations mutations_;
  std::shared_ptr<const ActorHitGeometry> geometry_;
  memory::Vector<Part> parts_;
  memory::Vector<physics::QueryTransform> transforms_;
  memory::Vector<physics::QueryFlags> flags_;
  std::uint32_t query_flags_{};
};
}  // namespace bw::game
