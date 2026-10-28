// Immutable graph + per-actor bounded A* scratch, followed by retained segment
// progress. Core SL.con.3 (#rsl-bounds) bounds graph indices; CDSA.14
// (guidelines/cpu-dsa/CDSA.14-precompile-bounded-automata-cache-the-rest.md)
// prepares shared adjacency while keeping actor search state private. MEM.9
// confines allocation to construction. Cover predicates/scoring are native;
// graph search/segment ownership is the documented pinned-Unreal inference.
// Collision-route ownership follows 5cfcb0/5d0aa0, with its debounced sensor,
// alternating admission suppression and source side selection. It does not
// infer worker readiness from archive presence or from completed search.
#include "scene_navigation.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace bw::game {
namespace {
using Vec3 = NpcNavigationVector;
constexpr auto kNoParent = std::numeric_limits<std::size_t>::max();
constexpr float kTinySquared = 1.0000001111620804e-6F;
constexpr float kCoverMaximumSquared = 1600.F, kCoverDistanceScale = 4.F;
constexpr float kCoverNearFraction = 0.5F, kCoverFarSquared = 16.F;
constexpr float kCoverFacingPenalty = 10.F, kCoverBlockedPenalty = 100.F;
constexpr float kObstacleRadiusScale = 1.5F, kGroundAbsent = -100000.F;
constexpr std::size_t kMaximumObstacles = 100;
float distance(const Vec3& a, const Vec3& b) {
  return std::sqrt(math::squared_distance(a, b));
}
bool finite(const Vec3& value) {
  return std::ranges::all_of(value, [](float v) { return std::isfinite(v); });
}
Vec3 horizontal(Vec3 v) {
  v[1] = 0;
  return v;
}
// Native cover score keeps travel cost and threat-relative directional bias
// separate. An obstructed approach adds a penalty; it does not invent a route.
float cover_score(const NpcCoverRequest& request, const Vec3& point,
                  const NpcRouteQueries& queries) {
  const auto threat_delta = math::subtract(request.threat, point);
  const auto actor_delta = math::subtract(request.position, point);
  const auto threat_squared = math::dot(threat_delta, threat_delta);
  const auto actor_squared = math::dot(actor_delta, actor_delta);
  auto score = math::squared_distance(request.position, point);
  if (request.mode == 1) {
    score = score * kCoverNearFraction +
            math::squared_distance(request.threat, request.position);
  }
  if (threat_squared >= kTinySquared && actor_squared >= kTinySquared) {
    auto cosine = math::dot(threat_delta, actor_delta) /
                  std::sqrt(threat_squared * actor_squared);
    if (cosine > 0 && actor_squared > threat_squared * kCoverFarSquared) {
      if (request.mode == 0) cosine *= kCoverFacingPenalty;
      score *= 1 + cosine;
    }
  }
  if (!queries.traversable(request.position, point)) {
    score += kCoverBlockedPenalty;
  }
  return score;
}
}  // namespace

NpcSceneRouteFollower::NpcSceneRouteFollower(
    const formats::NavigationArchive& archive, std::size_t group,
    const NpcRouteQueries& queries, NpcRouteContext context,
    const std::shared_ptr<memory::Domain>& domain)
    : archive_(archive),
      queries_(queries),
      context_(context),
      group_(group),
      costs_(memory::Allocator<float>(domain)),
      parents_(memory::Allocator<std::size_t>(domain)),
      route_(memory::Allocator<std::size_t>(domain)),
      closed_(memory::Allocator<std::uint8_t>(domain)),
      open_(memory::Allocator<OpenNode>(domain)) {
  constexpr float kMinimumRadius = .1F;
  if (!std::isfinite(context.body_radius) ||
      context.body_radius < kMinimumRadius) {
    throw std::invalid_argument("Invalid collision route body radius");
  }
  const auto count = archive.vertices(group).size();
  costs_.resize(count);
  parents_.resize(count);
  closed_.resize(count);
  route_.reserve(count);
  open_.reserve(count + archive.edges(group).size() * 2);
}
void NpcSceneRouteFollower::reset() {
  route_.clear();
  open_.clear();
  cursor_ = 0;
  active_ = false;
  blocked_ = false;
  collision_active_ = false;
  pair_.cancel();
}
void NpcSceneRouteFollower::set_side(std::int32_t side) {
  if (side != -2 && side != 2 && side != 0) {
    throw std::invalid_argument("Invalid navigation side");
  }
  if (side_ != side) {
    side_ = side;
    reset();
  }
}
std::int32_t NpcSceneRouteFollower::choose_side(const Endpoints& endpoints) {
  if (side_ != 0) return side_ > 0 ? 1 : -1;
  const auto face = context_.collision.get().route_face(endpoints.position,
                                                        endpoints.destination);
  if (face) {
    auto direction = math::subtract(endpoints.destination, endpoints.position);
    direction[1] = 0;
    auto normal = *face;
    normal[1] = 0;
    if (math::dot(direction, direction) >= kTinySquared &&
        math::dot(normal, normal) >= kTinySquared) {
      direction = math::normalize_or(direction, Vec3{});
      normal = math::normalize_or(normal, Vec3{});
      const auto cross = math::cross(normal, direction)[1];
      constexpr float kSideThreshold = .1F;
      if (cross > kSideThreshold) return -1;
      if (cross < -kSideThreshold) return 1;
    }
  }
  return context_.random.get().next(2) != 0 ? -1 : 1;
}
NpcRouteResult NpcSceneRouteFollower::collision_route(
    const Endpoints& endpoints, float seconds) {
  // Source failure debounce belongs to this retained route owner, independently
  // of the movement sensor's work cadence and of search-job readiness.
  if (sensor_permitted_) {
    retained_sensor_permitted_ = true;
    sensor_failure_delay_ = 0;
  } else if (!retained_sensor_permitted_ || sensor_failure_delay_ > 0) {
    if (sensor_failure_delay_ >= 0) sensor_failure_delay_ -= seconds;
  } else {
    sensor_failure_delay_ = 1;
    retained_sensor_permitted_ = false;
  }
  const bool clear =
      queries_.traversable(endpoints.position, endpoints.destination);
  if (!collision_active_) {
    destination_ = endpoints.destination;
    if (context_.collision_search_enabled &&
        (!clear || !retained_sensor_permitted_)) {
      collision_active_ = true;
      pair_.cancel();
    }
  } else {
    if (!pair_.sample().active) chosen_side_ = choose_side(endpoints);
    destination_ =
        pair_
            .advance_actor(
                {endpoints.position, endpoints.destination,
                 context_.body_radius, chosen_side_, !sensor_permitted_},
                context_.collision)
            .waypoint;
    if (context_.collision_search_enabled && clear &&
        retained_sensor_permitted_) {
      collision_active_ = false;
      destination_ = endpoints.destination;
      pair_.cancel();
    }
  }
  return {destination_, false, false};
}
bool NpcSceneRouteFollower::search(const Endpoints& endpoints) {
  const auto& position = endpoints.position;
  const auto& destination = endpoints.destination;
  const auto vertices = archive_.vertices(group_);
  std::ranges::fill(costs_, std::numeric_limits<float>::infinity());
  std::ranges::fill(parents_, kNoParent);
  std::ranges::fill(closed_, 0);
  open_.clear();
  route_.clear();
  cursor_ = 0;
  const auto heading = math::subtract(destination, position);
  const auto order = [&](const OpenNode& a, const OpenNode& b) {
    if (a.score != b.score) return a.score > b.score;
    // Inferred A* tie policy preserves the requested geometric side. Source
    // indices provide only deterministic order when lateral preference ties.
    const auto lateral = [&](std::size_t index) {
      return math::cross(heading, math::subtract(vertices[index].position,
                                                 position))[1] *
             static_cast<float>(side_);
    };
    const auto left = lateral(a.index), right = lateral(b.index);
    return left == right ? a.index > b.index : left < right;
  };
  // Endpoint visibility admits source graph vertices without a guessed nearest
  // point through walls. Heap storage is bounded by vertices + directed edges.
  for (std::size_t i = 0; i < vertices.size(); ++i) {
    if (!queries_.traversable(position, vertices[i].position)) continue;
    costs_.at(i) = distance(position, vertices[i].position);
    open_.push_back(
        {i, costs_.at(i) + distance(vertices[i].position, destination)});
    std::push_heap(open_.begin(), open_.end(), order);
  }
  while (!open_.empty()) {
    std::pop_heap(open_.begin(), open_.end(), order);
    const auto selected = open_.back().index;
    open_.pop_back();
    if (closed_.at(selected) != 0) continue;
    closed_.at(selected) = 1;
    const auto& point = vertices[selected].position;
    if (queries_.traversable(point, destination)) {
      for (auto index = selected; index != kNoParent;
           index = parents_.at(index)) {
        route_.push_back(index);
      }
      std::ranges::reverse(route_);
      return true;
    }
    for (const auto neighbor : archive_.neighbors(group_, selected)) {
      if (closed_.at(neighbor) != 0 ||
          !queries_.traversable(point, vertices[neighbor].position)) {
        continue;
      }
      const auto candidate =
          costs_.at(selected) + distance(point, vertices[neighbor].position);
      if (candidate >= costs_.at(neighbor)) continue;
      costs_.at(neighbor) = candidate;
      parents_.at(neighbor) = selected;
      if (open_.size() == open_.capacity()) {
        throw std::logic_error(
            "Navigation search exceeded admitted graph bound");
      }
      open_.push_back(
          {neighbor,
           candidate + distance(vertices[neighbor].position, destination)});
      std::push_heap(open_.begin(), open_.end(), order);
    }
  }
  return false;
}
NpcRouteResult NpcSceneRouteFollower::advance(const Vec3& position,
                                              const Vec3& destination,
                                              float arrival_squared,
                                              float delta_seconds,
                                              bool trace_path) {
  if (!finite(position) || !finite(destination) ||
      !std::isfinite(arrival_squared) || arrival_squared < 0 ||
      !std::isfinite(delta_seconds) || delta_seconds < 0) {
    throw std::invalid_argument("Invalid navigation frame");
  }
  if (delta_seconds == 0) return {position, false, blocked_};
  if (math::squared_distance(position, destination) <= arrival_squared) {
    reset();
    return {destination, true, false};
  }
  // Direct local steering is valid without a route archive. Physical movement
  // still resolves contacts; a floor-level whole-route sweep is not admission.
  if (trace_path && archive_.vertices(group_).empty()) {
    active_ = true;
    blocked_ = false;
    return collision_route({position, destination}, delta_seconds);
  }
  if (!trace_path || queries_.traversable(position, destination)) {
    collision_active_ = false;
    pair_.cancel();
    route_.clear();
    active_ = true;
    blocked_ = false;
    destination_ = destination;
    return {destination, false, false};
  }
  if (!trace_path) {
    reset();
    blocked_ = true;
    return {position, false, true};
  }
  if (!active_ || destination_ != destination || route_.empty()) {
    destination_ = destination;
    active_ = search({position, destination});
    blocked_ = !active_;
  }
  const auto vertices = archive_.vertices(group_);
  while (
      cursor_ < route_.size() &&
      math::squared_distance(position, vertices[route_.at(cursor_)].position) <=
          arrival_squared) {
    ++cursor_;
  }
  if (cursor_ == route_.size()) return {position, false, true};
  const auto waypoint = vertices[route_.at(cursor_)].position;
  if (!queries_.traversable(position, waypoint)) {
    active_ = false;
    blocked_ = true;
    return {position, false, true};
  }
  return {waypoint, false, false};
}
void NpcCoverSearch::request(const NpcCoverRequest& value) {
  if (!finite(value.position) || !finite(value.threat) ||
      !std::isfinite(value.body_radius) || value.body_radius < 0 ||
      !std::isfinite(value.body_height) || value.body_height < 0 ||
      value.mode < 0 || value.mode > 1) {
    throw std::invalid_argument("Invalid cover search request");
  }
  request_ = value;
  pending_ = true;
  result_ = {};
}
void NpcCoverSearch::reset() {
  pending_ = false;
  result_ = {};
}
void NpcCoverSearch::advance(float delta_seconds) {
  if (!std::isfinite(delta_seconds) || delta_seconds < 0) {
    throw std::invalid_argument("Invalid cover search interval");
  }
  if (!pending_ || delta_seconds == 0) return;
  pending_ = false;
  result_.ready = true;
  auto best = std::numeric_limits<float>::max();
  const auto actor = horizontal(request_.position);
  const auto threat = horizontal(request_.threat);
  const auto direction = math::subtract(threat, actor);
  const auto threat_squared = math::dot(direction, direction);
  const auto limit =
      std::min(kCoverMaximumSquared, threat_squared * kCoverDistanceScale);
  const auto near_threat_half = [&](const Vec3& point) {
    const auto offset = math::subtract(horizontal(point), actor);
    return threat_squared > kTinySquared &&
           math::dot(offset, direction) / threat_squared <= kCoverNearFraction;
  };
  const auto consider = [&](const Vec3& point, const Vec3& facing) {
    if (!queries_->occluded(request_.threat, point)) return;
    const auto score = cover_score(request_, point, *queries_);
    if (score < best) {
      best = score;
      result_ = {point, facing, true, true};
    }
  };
  for (const auto& point : queries_->cover_points()) {
    // Native authored candidate range retains point height against ground Y=0.
    if (math::squared_distance(actor, point.position) < limit &&
        near_threat_half(point.position)) {
      consider(point.position, point.direction);
    }
  }
  const auto obstacles = queries_->cover_obstacles();
  for (const auto& obstacle :
       obstacles.first(std::min(obstacles.size(), kMaximumObstacles))) {
    const auto delta = math::subtract(horizontal(obstacle.position), threat);
    const auto squared = math::dot(delta, delta);
    if (squared < kTinySquared ||
        math::squared_distance(actor, horizontal(obstacle.position)) >= limit ||
        !near_threat_half(obstacle.position)) {
      continue;
    }
    const auto length = std::sqrt(squared);
    const auto distance_to_threat =
        distance(obstacle.position, request_.threat);
    auto candidate = math::add(
        threat, math::scale(delta, (distance_to_threat +
                                    obstacle.radius * kObstacleRadiusScale) /
                                       length));
    candidate[1] = request_.threat[1] + request_.body_height;
    const auto grounded = queries_->ground(candidate);
    if (!grounded || (*grounded)[1] < kGroundAbsent) continue;
    candidate = *grounded;
    candidate[1] += request_.body_radius + request_.body_height;
    consider(candidate, {});
  }
}
}  // namespace bw::game
