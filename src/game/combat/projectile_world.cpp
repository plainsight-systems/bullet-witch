// Original persistent bullet stepping and query admission; engine physics owns
// all intersections. Core I.1 keeps damage/effects out of geometric results;
// MEM.9 reserves all mission storage before emission. Both implementation
// corpora queried/read. See matching header for source/order/lifetime contract.
#include "projectile_world.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

#include "shot_target.hpp"

namespace bw::game {
ProjectileWorld::ProjectileWorld(const world::WorldClock& clock,
                                 physics::Queries queries,
                                 ProjectileLimits limits,
                                 const std::shared_ptr<memory::Domain>& domain)
    : clock_(clock),
      queries_(std::move(queries)),
      slots_(memory::Allocator<Slot>(domain)),
      order_(memory::Allocator<std::uint32_t>(domain)),
      next_order_(memory::Allocator<std::uint32_t>(domain)),
      update_order_(memory::Allocator<std::uint32_t>(domain)),
      events_(memory::Allocator<ProjectileEvent>(domain)),
      query_budget_(limits.source_query_budget) {
  const auto capacity = limits.capacity;
  if (capacity == 0 || capacity > std::numeric_limits<std::uint32_t>::max()) {
    throw std::invalid_argument("Invalid projectile capacity");
  }
  slots_.resize(capacity);
  order_.reserve(capacity);
  next_order_.reserve(capacity);
  update_order_.reserve(capacity);
  events_.reserve(capacity);
}
void ProjectileWorld::retire() {
  for (auto& slot : slots_) slot.active = false;
  order_.clear();
  next_order_.clear();
  update_order_.clear();
  events_.clear();
}
ProjectileId ProjectileWorld::emit(const StraightShot& shot) {
  if (!shot.definition || !math::all_finite(shot.muzzle) ||
      !math::all_finite(shot.direction) ||
      !math::all_finite(shot.scenery_reference) ||
      !std::isfinite(shot.initial_age_seconds) ||
      shot.initial_age_seconds < 0 || shot.level > 3 || shot.owner == 0 ||
      !std::isfinite(shot.definition->range) || shot.definition->range <= 0 ||
      !std::isfinite(shot.definition->speed) || shot.definition->speed <= 0 ||
      !std::isfinite(math::length(shot.direction)) ||
      math::length(shot.direction) == 0) {
    throw std::invalid_argument("Invalid ordinary projectile emission");
  }
  const auto found = std::ranges::find(slots_, false, &Slot::active);
  if (found == slots_.end()) {
    throw std::length_error("Projectile pool exhausted");
  }
  if (found->generation == std::numeric_limits<std::uint32_t>::max()) {
    throw std::overflow_error("Projectile generation exhausted");
  }
  const auto slot = static_cast<std::uint32_t>(found - slots_.begin());
  found->shot = shot;
  found->shot.direction = math::normalize_or(shot.direction, shot.direction);
  found->position = shot.muzzle;
  found->query_start = shot.muzzle;
  found->remaining = shot.definition->range;
  ++found->generation;
  found->active = true;
  order_.push_back(slot);
  update_order_.push_back(slot);
  return {slot, found->generation};
}
void ProjectileWorld::update(ProjectileId id, float seconds,
                             const math::Vector3<float>& camera) {
  auto& slot = slots_.at(id.slot);
  const auto previous_position = slot.position;
  const auto& definition = *slot.shot.definition;
  const auto interval =
      seconds + std::exchange(slot.shot.initial_age_seconds, 0);
  const auto distance = definition.speed * interval;
  slot.remaining -= distance;
  const bool expired = slot.remaining <= 0;
  if (expired) {
    // The original retires at range before moving its final segment.
    slot.remaining = 0;
  } else {
    slot.position =
        math::add(slot.position, math::scale(slot.shot.direction, distance));
  }
  ProjectileEvent event{
      id,
      expired ? ProjectileEventKind::expired : ProjectileEventKind::moved,
      slot.shot.owner,
      slot.shot.definition,
      slot.position,
      slot.shot.direction,
      {},
      0,
      slot.shot.level,
      {}};
  const auto segment = math::subtract(slot.position, slot.query_start);
  if (slot.query_selected && math::dot(segment, segment) > 0) {
    auto endpoint = slot.position;
    // Scenery is clipped first, after crossing the source reference plane.
    // Actor eligibility belongs to the caller's original query-bank mapping.
    if (math::dot(math::subtract(slot.shot.scenery_reference, endpoint),
                  segment) <= 0) {
      event.hit = queries_.ray({.start = slot.query_start,
                                .end = endpoint,
                                .layers = slot.shot.scenery_layers,
                                .exclude_flags = kShotDisabledReceiverMask});
      if (event.hit) {
        // Original scenery query backs up along the segment by the bullet's
        // authored one-centimetre clearance before testing actor receivers.
        constexpr float kSceneryClearance = .01F;
        endpoint =
            math::subtract(event.hit->position,
                           math::scale(slot.shot.direction, kSceneryClearance));
      }
    }
    if (const auto actor =
            queries_.ray({.start = slot.query_start,
                          .end = endpoint,
                          .layers = slot.shot.actor_layers,
                          .exclude_flags = kShotActorExclusionMask})) {
      event.hit = actor;
      endpoint = actor->position;
    }
    if (event.hit) {
      event.kind = ProjectileEventKind::impact;
      event.position = endpoint;
      event.damage_distance =
          definition.range - slot.remaining +
          math::length(math::subtract(endpoint, slot.query_start));
    }
    slot.query_start = endpoint;
  }
  if (event.kind != ProjectileEventKind::impact) {
    const auto before = math::subtract(camera, previous_position);
    const auto before_projection = math::dot(slot.shot.direction, before);
    const auto after_projection =
        math::dot(slot.shot.direction, math::subtract(camera, slot.position));
    if (before_projection * after_projection < 0) {
      const auto closest =
          math::add(previous_position,
                    math::scale(slot.shot.direction, before_projection));
      const auto radial = math::subtract(camera, closest);
      constexpr float kNearPassRadiusSquared = 9;
      if (math::dot(radial, radial) < kNearPassRadiusSquared) {
        event.near_pass = closest;
      }
    }
  }
  events_.push_back(event);
  slot.active = event.kind == ProjectileEventKind::moved;
}
void ProjectileWorld::advance(const world::WorldFrame& frame,
                              const math::Vector3<float>& camera) {
  if (!frame.belongs_to(clock_) || frame.sequence() <= last_frame_ ||
      !math::all_finite(camera)) {
    throw std::invalid_argument("Foreign or repeated projectile world frame");
  }
  last_frame_ = frame.sequence();
  if (!frame.admission().simulation) return;
  events_.clear();
  next_order_.clear();
  constexpr std::size_t kSourcePopulationDivisor = 10;
  const auto selected = std::min(
      order_.size(),
      std::max(query_budget_, previous_population_ / kSourcePopulationDivisor));
  previous_population_ = order_.size();
  for (std::size_t i = 0; i < order_.size(); ++i) {
    slots_[order_[i]].query_selected = i < selected;
  }
  // Query priority is a different source list from task update order.
  for (const auto index : update_order_) {
    update({index, slots_[index].generation}, frame.delta_seconds(), camera);
  }
  // Rotate queried survivors behind unqueried survivors without changing the
  // actual update/impact order or admitting new heap storage.
  for (std::size_t i = selected; i < order_.size(); ++i) {
    if (slots_[order_[i]].active) next_order_.push_back(order_[i]);
  }
  for (std::size_t i = 0; i < selected; ++i) {
    if (slots_[order_[i]].active) next_order_.push_back(order_[i]);
  }
  order_.swap(next_order_);
  std::erase_if(update_order_,
                [&](std::uint32_t index) { return !slots_[index].active; });
}
}  // namespace bw::game
