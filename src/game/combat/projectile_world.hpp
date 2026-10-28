// Mission-owned original straight-projectile policy. Controllers emit copied
// shots; only the world owner advances them on its issuing WorldClock. Engine
// physics supplies copied geometry hits, never damage decisions. Immutable gun
// profiles and clock outlive this owner; the weak query capability cannot keep
// the physics world alive. Single owner thread, no mutable state escapes.
// Admission reserves fixed slots/order/events in the engine domain. Each frame
// selects max(previous population/10, source query budget) oldest candidates,
// rotates their priority, consumes first-shot timing offset once, subtracts
// range before motion, then queries the accumulated segment only when selected.
// Source scenery admission uses its supplied reference plane; actor queries
// shorten the scenery endpoint. Impact carries copied contact/damage distance
// for game receivers. Retirement never means successful damage. Events remain
// valid until the next admitted advance; pause neither moves nor drops them.
// Invalid/replayed frames and invalid shots reject; exhaustion throws before
// admission. No allocator calls during valid spawn/update; work O(capacity plus
// selected provider queries), retained order rotation O(active count).
// Source contracts: first-combat-exchange.md, 61b120/61c7a0/61ba90/61b1d0.
// Design Core I.1 (#ri-explicit) separates queries from response authority;
// performance MEM.9 (guidelines/memory/MEM.9-embedded-allocate-at-init.md)
// bounds mission storage. Both corpora queried/read before this design.
#pragma once
#include "bw/physics/world.hpp"
#include "weapon_definition.hpp"

namespace bw::game {
struct ProjectileId {
  std::uint32_t slot{}, generation{};
  friend bool operator==(const ProjectileId&, const ProjectileId&) = default;
};
struct StraightShot {
  const StraightWeaponDefinition* definition{};
  math::Vector3<float> muzzle{}, direction{}, scenery_reference{};
  float initial_age_seconds{};
  std::uint64_t owner{};
  unsigned level{};
  // Explicit original query-bank mapping supplied by the world combat owner.
  std::uint16_t scenery_layers{}, actor_layers{};
};
enum class ProjectileEventKind : std::uint8_t { moved, impact, expired };
struct ProjectileLimits {
  std::size_t capacity{}, source_query_budget{};
};
struct ProjectileEvent {
  ProjectileId projectile;
  ProjectileEventKind kind{};
  std::uint64_t owner{};
  const StraightWeaponDefinition* definition{};
  math::Vector3<float> position{}, direction{};
  std::optional<physics::Hit> hit;
  // Original falloff input adds this query segment to already-spent range.
  float damage_distance{};
  unsigned level{};
  // Crossing the listener plane within the authored three-metre radius emits
  // the profile's near-pass cue at this closest position. Impact takes
  // priority.
  std::optional<math::Vector3<float>> near_pass;
};
class ProjectileWorld {
 public:
  ProjectileWorld(const world::WorldClock&, physics::Queries, ProjectileLimits,
                  const std::shared_ptr<memory::Domain>&);
  ProjectileWorld(const ProjectileWorld&) = delete;
  ProjectileWorld& operator=(const ProjectileWorld&) = delete;
  ProjectileWorld(ProjectileWorld&&) = delete;
  ProjectileWorld& operator=(ProjectileWorld&&) = delete;
  ~ProjectileWorld() = default;
  ProjectileId emit(const StraightShot&);
  void advance(const world::WorldFrame&, const math::Vector3<float>& camera);
  std::span<const ProjectileEvent> events() const { return events_; }
  std::size_t active_count() const { return order_.size(); }
  // Explicit world teardown: clear live shots/events without resetting frame
  // or slot generation authority. Previously returned IDs cannot revive.
  void retire();

 private:
  struct Slot {
    StraightShot shot;
    math::Vector3<float> position{}, query_start{};
    float remaining{};
    std::uint32_t generation{};
    bool active{}, query_selected{};
  };
  const world::WorldClock& clock_;
  physics::Queries queries_;
  memory::Vector<Slot> slots_;
  memory::Vector<std::uint32_t> order_, next_order_, update_order_;
  memory::Vector<ProjectileEvent> events_;
  std::size_t query_budget_, previous_population_{};
  std::uint64_t last_frame_{};
  void update(ProjectileId, float seconds, const math::Vector3<float>& camera);
};
}  // namespace bw::game
