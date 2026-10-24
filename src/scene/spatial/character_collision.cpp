// Contact-mode implementation E.4 validates state policy before geometry;
// MEM.9 keeps grounded/airborne selection allocation-free. Both corpora read
// for design and implementation; source paths are in the collision research.
// Preserve authored triangle surface bytes through BVH reordering. I.1/E.4
// and CDSA.32/MEM.9 (both phases) keep the value optional for generated shapes
// and copy it only during preparation; no archive access enters hot queries.
// Shared math retains each caller's scalar/accumulator precision; tolerance
// and failure policy remain here. Both-phase I.9/MEM.9 contracts are in
// bw/math/vector.hpp and matrix.hpp.
// Prepare static character geometry, then resolve upright body and floor
// contact. Original policy uses previous/proposed segment protection, two body
// corrections and a qualified downward support probe; source provenance and the
// bounded geometric adaptations are in player-collision-boundary.md. Geometry
// distance follows the inspected Unreal GeometryCore segment/triangle feature
// tests; a balanced centroid BVH changes candidate cost, not authored object
// filtering. Preparation atomically publishes owned triangles/nodes through the
// domain. Frozen queries use fixed local scratch, have no scene side effects
// and return complete values; invalid/unprepared requests throw before
// publication. Optimization: decode once and reject BVH bounds before narrow
// phase. Build is O(n log n), storage O(n), query O(visited nodes + candidate
// triangles). Both MCP phases: SL.con.3 (#rsl-bounds) bounds scratch and source
// extents; MEM.9 (guidelines/memory/MEM.9-embedded-allocate-at-init.md)
// allocates at prepare.
#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <stdexcept>

#include "../../formats/reader.hpp"
#include "bw/math/vector.hpp"
#include "bw/scene/spatial/static_collision.hpp"

namespace bw::scene {
namespace {
using Vec = math::Vector3<float>;
using Triangle = std::array<Vec, 3>;
constexpr float kTolerance = 0.000001F;
constexpr float kClearance = 0.01F;
// The source floor probe uses 45 degrees, while world contact classification
// uses 46 degrees. Keep the tolerance band rather than collapsing the policies.
constexpr float kWalkableNormal = 0.70710677F;
constexpr float kContactNormal = 0.6946584F;
constexpr float kGroundReach = 0.2999F;
constexpr std::size_t kLeafSize = 8;
constexpr std::uint32_t kDisabled = 0x80000000;
bool overlaps(const Vec& amin, const Vec& amax, const Vec& bmin,
              const Vec& bmax) {
  for (std::size_t axis = 0; axis < 3; ++axis) {
    if (amin.at(axis) > bmax.at(axis) || amax.at(axis) < bmin.at(axis)) {
      return false;
    }
  }
  return true;
}
Vec closest_edge(const Vec& p, const Vec& a, const Vec& b) {
  const auto edge = math::subtract(b, a);
  const auto length = math::dot(edge, edge);
  return math::add(
      a,
      math::scale(
          edge, length > 0
                    ? std::clamp(math::dot(math::subtract(p, a), edge) / length,
                                 0.0F, 1.0F)
                    : 0));
}
bool inside(const Vec& p, const Triangle& triangle, const Vec& normal) {
  for (std::size_t i = 0; i < 3; ++i) {
    if (math::dot(math::cross(
                      math::subtract(triangle.at((i + 1) % 3), triangle.at(i)),
                      math::subtract(p, triangle.at(i))),
                  normal) < -kTolerance) {
      return false;
    }
  }
  return true;
}
// The closest triangle feature is either its plane interior or a bounded edge.
Vec closest_triangle(const Vec& p, const Triangle& triangle,
                     const Vec& normal) {
  const auto projected = math::subtract(
      p,
      math::scale(normal, math::dot(math::subtract(p, triangle[0]), normal)));
  if (inside(projected, triangle, normal)) {
    return projected;
  }
  auto closest = triangle[0];
  auto distance = std::numeric_limits<float>::max();
  for (std::size_t i = 0; i < 3; ++i) {
    const auto q = closest_edge(p, triangle.at(i), triangle.at((i + 1) % 3));
    const auto delta = math::subtract(p, q);
    const auto squared = math::dot(delta, delta);
    if (squared < distance) {
      distance = squared;
      closest = q;
    }
  }
  return closest;
}
struct Pair {
  Vec body{}, surface{};
  float squared{std::numeric_limits<float>::max()};
};
void consider(Pair& pair, const Vec& body, const Vec& surface) {
  const auto delta = math::subtract(body, surface);
  const auto squared = math::dot(delta, delta);
  if (squared < pair.squared) {
    pair = {body, surface, squared};
  }
}
// Minimize the quadratic between two finite segments, clamping both parameters.
Pair closest_segments(const std::array<Vec, 2>& first,
                      const std::array<Vec, 2>& second) {
  const auto& [p, q] = first;
  const auto& [a, b] = second;
  const auto u = math::subtract(q, p), v = math::subtract(b, a),
             w = math::subtract(p, a);
  const auto uu = math::dot(u, u), vv = math::dot(v, v), uv = math::dot(u, v);
  const auto uw = math::dot(u, w), vw = math::dot(v, w);
  float s = 0, t = 0;
  if (uu <= kTolerance) {
    t = vv > kTolerance ? std::clamp(vw / vv, 0.0F, 1.0F) : 0;
  } else if (vv <= kTolerance) {
    s = std::clamp(-uw / uu, 0.0F, 1.0F);
  } else {
    const auto denominator = uu * vv - uv * uv;
    if (denominator > kTolerance) {
      s = std::clamp((uv * vw - vv * uw) / denominator, 0.0F, 1.0F);
    }
    t = (uv * s + vw) / vv;
    if (t < 0) {
      t = 0;
      s = std::clamp(-uw / uu, 0.0F, 1.0F);
    } else if (t > 1) {
      t = 1;
      s = std::clamp((uv - uw) / uu, 0.0F, 1.0F);
    }
  }
  Pair result;
  consider(result, math::add(p, math::scale(u, s)),
           math::add(a, math::scale(v, t)));
  return result;
}
Pair closest_capsule_axis(const Vec& lower, float height,
                          const Triangle& triangle, const Vec& normal) {
  auto upper = lower;
  upper[1] += height;
  Pair pair;
  consider(pair, lower, closest_triangle(lower, triangle, normal));
  consider(pair, upper, closest_triangle(upper, triangle, normal));
  const auto lower_plane =
      math::dot(math::subtract(lower, triangle[0]), normal);
  const auto difference = height * normal[1];
  if (std::abs(difference) > kTolerance) {
    const auto t = -lower_plane / difference;
    auto intersection = lower;
    intersection[1] += t * height;
    if (t >= 0 && t <= 1 && inside(intersection, triangle, normal)) {
      return {intersection, intersection, 0};
    }
  }
  for (std::size_t i = 0; i < 3; ++i) {
    const auto candidate = closest_segments(
        {lower, upper}, {triangle.at(i), triangle.at((i + 1) % 3)});
    consider(pair, candidate.body, candidate.surface);
  }
  return pair;
}
// Entry root only: penetration recovery must remain able to leave a shape.
// Use a stable quadratic root pair rather than subtracting nearly equal values.
float entry_root(float a, float b, float c) {
  if (a <= kTolerance || c < 0) {
    return std::numeric_limits<float>::max();
  }
  const auto discriminant = b * b - a * c;
  if (discriminant < 0) {
    return std::numeric_limits<float>::max();
  }
  const auto q = -b - std::copysign(std::sqrt(discriminant), b);
  const auto first = q / a;
  const auto second = q != 0 ? c / q : first;
  const auto root = std::min(first, second);
  return root >= 0 ? root : std::numeric_limits<float>::max();
}
float capsule_entry(const std::array<Vec, 2>& ray,
                    const CollisionCapsule& capsule) {
  const auto& [origin, target] = ray;
  const auto initial_delta =
      math::subtract(origin, closest_edge(origin, capsule.start, capsule.end));
  if (math::dot(initial_delta, initial_delta) <
      capsule.radius * capsule.radius) {
    return std::numeric_limits<float>::max();
  }
  const auto direction = math::subtract(target, origin);
  const auto axis = math::subtract(capsule.end, capsule.start);
  const auto relative = math::subtract(origin, capsule.start);
  const auto axis_squared = math::dot(axis, axis);
  auto fraction = std::numeric_limits<float>::max();
  const auto ray_squared = math::dot(direction, direction);
  if (axis_squared > kTolerance) {
    const auto along = math::dot(axis, direction);
    const auto offset = math::dot(axis, relative);
    const auto a = axis_squared * ray_squared - along * along;
    const auto b =
        axis_squared * math::dot(relative, direction) - offset * along;
    const auto c = axis_squared * (math::dot(relative, relative) -
                                   capsule.radius * capsule.radius) -
                   offset * offset;
    const auto root = entry_root(a, b, c);
    const auto height = offset + root * along;
    if (root <= 1 && height >= 0 && height <= axis_squared) {
      fraction = root;
    }
  }
  for (const auto& center : {capsule.start, capsule.end}) {
    const auto delta = math::subtract(origin, center);
    fraction = std::min(
        fraction,
        entry_root(ray_squared, math::dot(delta, direction),
                   math::dot(delta, delta) - capsule.radius * capsule.radius));
  }
  return fraction;
}

Triangle decode_triangle(const formats::CollisionPart& part, std::size_t face) {
  const resources::detail::Reader reader{part.vertices};
  Triangle result{};
  constexpr std::size_t kFaceBytes = 8, kVertexBytes = 12;
  for (std::size_t corner = 0; corner < 3; ++corner) {
    const auto at = face * kFaceBytes + corner * 2;
    const auto index = std::uint32_t{part.triangles[at]} |
                       (std::uint32_t{part.triangles[at + 1]} << 8U);
    for (std::size_t axis = 0; axis < 3; ++axis) {
      result.at(corner).at(axis) = std::bit_cast<float>(
          reader.u32(index * kVertexBytes + axis * sizeof(float)));
    }
  }
  return result;
}
}  // namespace

void StaticCollision::add_triangles(
    std::span<const CollisionTriangle> triangles) {
  for (const auto& triangle : triangles) {
    if (!std::all_of(triangle.vertices.begin(), triangle.vertices.end(),
                     math::all_finite<float, 3>)) {
      throw std::invalid_argument("Nonfinite collision triangle");
    }
  }
  additional_triangles_.insert(additional_triangles_.end(), triangles.begin(),
                               triangles.end());
  character_ready_ = false;
}
void StaticCollision::add_capsules(std::span<const CollisionCapsule> capsules) {
  for (const auto& capsule : capsules) {
    if (!math::all_finite(capsule.start) || !math::all_finite(capsule.end) ||
        !std::isfinite(capsule.radius) || capsule.radius <= 0) {
      throw std::invalid_argument("Invalid collision capsule");
    }
  }
  capsules_.insert(capsules_.end(), capsules.begin(), capsules.end());
  character_ready_ = false;
}

void StaticCollision::prepare_character_queries() {
  auto triangles =
      memory::Vector<CharacterPrimitive>(character_primitives_.get_allocator());
  auto nodes = memory::Vector<CharacterNode>(character_nodes_.get_allocator());
  std::size_t count = additional_triangles_.size();
  const auto grow_count = [&](std::size_t extra) {
    if (extra > triangles.max_size() - count) {
      throw std::length_error("Character collision primitive count overflow");
    }
    count += extra;
  };
  grow_count(capsules_.size());
  for (const auto& part : parts_) {
    grow_count(part.definition.get().triangle_count);
  }
  triangles.reserve(count);
  std::size_t ordinal = 0;
  const auto append_triangle = [&](const Triangle& vertices,
                                   std::uint32_t flags,
                                   std::optional<std::uint8_t> surface) {
    CharacterPrimitive triangle;
    triangle.vertices = vertices;
    triangle.ordinal = ordinal++;
    const auto normal = math::cross(math::subtract(vertices[1], vertices[0]),
                                    math::subtract(vertices[2], vertices[0]));
    const auto length = math::length(normal);
    if (!std::isfinite(length)) {
      throw std::invalid_argument("Unrepresentable character collision face");
    }
    if (length <= kTolerance) {
      return;
    }
    triangle.normal = math::scale(normal, 1.0F / length);
    triangle.minimum = vertices[0];
    triangle.maximum = vertices[0];
    for (const auto& vertex : vertices) {
      for (std::size_t axis = 0; axis < 3; ++axis) {
        triangle.minimum.at(axis) =
            std::min(triangle.minimum.at(axis), vertex.at(axis));
        triangle.maximum.at(axis) =
            std::max(triangle.maximum.at(axis), vertex.at(axis));
      }
    }
    triangle.flags = flags;
    triangle.surface = surface;
    triangles.push_back(triangle);
  };
  for (const auto& part : parts_) {
    const auto& definition = part.definition.get();
    for (std::uint32_t face = 0; face < definition.triangle_count; ++face) {
      constexpr std::size_t kFaceBytes = 8, kSurfaceByte = 7;
      append_triangle(decode_triangle(definition, face), part.object_flags,
                      definition.triangles[face * kFaceBytes + kSurfaceByte]);
    }
  }
  for (const auto& triangle : additional_triangles_) {
    append_triangle(triangle.vertices, triangle.object_flags, std::nullopt);
  }
  for (const auto& capsule : capsules_) {
    CharacterPrimitive primitive;
    primitive.vertices[0] = capsule.start;
    primitive.vertices[1] = capsule.end;
    primitive.radius = capsule.radius;
    primitive.capsule = true;
    primitive.flags = capsule.object_flags;
    primitive.ordinal = ordinal++;
    for (std::size_t axis = 0; axis < 3; ++axis) {
      primitive.minimum.at(axis) =
          std::min(capsule.start.at(axis), capsule.end.at(axis)) -
          capsule.radius;
      primitive.maximum.at(axis) =
          std::max(capsule.start.at(axis), capsule.end.at(axis)) +
          capsule.radius;
    }
    if (!math::all_finite(primitive.minimum) ||
        !math::all_finite(primitive.maximum)) {
      throw std::invalid_argument("Unrepresentable collision capsule bounds");
    }
    triangles.push_back(primitive);
  }
  if (triangles.size() > nodes.max_size() / 2) {
    throw std::length_error("Character collision hierarchy exceeds capacity");
  }
  nodes.reserve(triangles.size() * 2);
  // Equal-count median splits bound traversal stack depth even for coincident
  // centers. Original ordinal breaks ties independently of tree visitation.
  const auto build = [&](auto&& self, std::size_t first,
                         std::size_t size) -> std::size_t {
    const auto index = nodes.size();
    CharacterNode node;
    node.minimum = triangles.at(first).minimum;
    node.maximum = triangles.at(first).maximum;
    for (std::size_t i = first; i < first + size; ++i) {
      for (std::size_t axis = 0; axis < 3; ++axis) {
        node.minimum.at(axis) =
            std::min(node.minimum.at(axis), triangles.at(i).minimum.at(axis));
        node.maximum.at(axis) =
            std::max(node.maximum.at(axis), triangles.at(i).maximum.at(axis));
      }
    }
    nodes.push_back(node);
    if (size <= kLeafSize) {
      nodes.at(index).first = first;
      nodes.at(index).count = size;
      return index;
    }
    const auto extent = math::subtract(node.maximum, node.minimum);
    const auto axis = static_cast<std::size_t>(
        std::max_element(extent.begin(), extent.end()) - extent.begin());
    const auto middle = first + size / 2;
    std::nth_element(
        triangles.begin() + static_cast<std::ptrdiff_t>(first),
        triangles.begin() + static_cast<std::ptrdiff_t>(middle),
        triangles.begin() + static_cast<std::ptrdiff_t>(first + size),
        [axis](const auto& a, const auto& b) {
          const auto ac = a.minimum.at(axis) + a.maximum.at(axis);
          const auto bc = b.minimum.at(axis) + b.maximum.at(axis);
          return ac == bc ? a.ordinal < b.ordinal : ac < bc;
        });
    nodes.at(index).first = self(self, first, size / 2);
    nodes.at(index).right = self(self, middle, size - size / 2);
    return index;
  };
  if (!triangles.empty()) {
    build(build, 0, triangles.size());
  }
  character_primitives_.swap(triangles);
  character_nodes_.swap(nodes);
  character_ready_ = true;
}

StaticCollision::CharacterContact StaticCollision::body_contact(
    const BodyQuery& query) const {
  const auto& [position, radius, cylinder_height, excluded_flags,
               minimum_normal_y] = query;
  CharacterContact contact;
  const Vec minimum{position[0] - radius, position[1] - radius,
                    position[2] - radius};
  const Vec maximum{position[0] + radius,
                    position[1] + cylinder_height + radius,
                    position[2] + radius};
  std::array<std::size_t, std::numeric_limits<std::size_t>::digits> stack{};
  std::size_t pending = character_nodes_.empty() ? 0 : 1;
  while (pending != 0) {
    const auto& node = character_nodes_.at(stack.at(--pending));
    if (!overlaps(minimum, maximum, node.minimum, node.maximum)) {
      continue;
    }
    if (node.count == 0) {
      stack.at(pending++) = node.first;
      stack.at(pending++) = node.right;
      continue;
    }
    for (std::size_t i = node.first; i < node.first + node.count; ++i) {
      const auto& triangle = character_primitives_.at(i);
      if ((triangle.flags & (excluded_flags | kDisabled)) != 0) {
        continue;
      }
      auto upper = position;
      upper[1] += cylinder_height;
      const auto pair =
          triangle.capsule
              ? closest_segments({position, upper},
                                 {triangle.vertices[0], triangle.vertices[1]})
              : closest_capsule_axis(position, cylinder_height,
                                     triangle.vertices, triangle.normal);
      const auto combined_radius = radius + triangle.radius;
      if (pair.squared >= combined_radius * combined_radius) {
        continue;
      }
      const auto distance = std::sqrt(pair.squared);
      const auto depth = combined_radius - distance;
      const auto normal =
          distance > kTolerance
              ? math::scale(math::subtract(pair.body, pair.surface),
                            1.0F / distance)
              : (triangle.capsule ? Vec{1, 0, 0} : triangle.normal);
      if (normal[1] < minimum_normal_y) {
        continue;
      }
      if (contact.found &&
          (depth < contact.depth ||
           (depth == contact.depth && triangle.ordinal > contact.ordinal))) {
        continue;
      }
      contact = {math::add(pair.surface, math::scale(normal, triangle.radius)),
                 normal,
                 depth,
                 triangle.ordinal,
                 true,
                 triangle.surface};
    }
  }
  return contact;
}

StaticCollision::CharacterContact StaticCollision::segment_contact(
    const Vec& origin, const Vec& target, std::uint32_t excluded_flags) const {
  CharacterContact contact;
  contact.depth = 1;
  const auto delta = math::subtract(target, origin);
  Vec minimum{}, maximum{};
  for (std::size_t axis = 0; axis < 3; ++axis) {
    minimum.at(axis) = std::min(origin.at(axis), target.at(axis));
    maximum.at(axis) = std::max(origin.at(axis), target.at(axis));
  }
  std::array<std::size_t, std::numeric_limits<std::size_t>::digits> stack{};
  std::size_t pending = character_nodes_.empty() ? 0 : 1;
  while (pending != 0) {
    const auto& node = character_nodes_.at(stack.at(--pending));
    if (!overlaps(minimum, maximum, node.minimum, node.maximum)) {
      continue;
    }
    if (node.count == 0) {
      stack.at(pending++) = node.first;
      stack.at(pending++) = node.right;
      continue;
    }
    for (std::size_t i = node.first; i < node.first + node.count; ++i) {
      const auto& triangle = character_primitives_.at(i);
      if ((triangle.flags & (excluded_flags | kDisabled)) != 0) {
        continue;
      }
      if (triangle.capsule) {
        const CollisionCapsule capsule{triangle.vertices[0],
                                       triangle.vertices[1], triangle.radius,
                                       triangle.flags};
        const auto fraction = capsule_entry({origin, target}, capsule);
        if (fraction <= contact.depth &&
            (!contact.found || fraction < contact.depth ||
             triangle.ordinal < contact.ordinal)) {
          const auto point = math::add(origin, math::scale(delta, fraction));
          const auto axis_point =
              closest_edge(point, capsule.start, capsule.end);
          const auto normal = math::scale(math::subtract(point, axis_point),
                                          1.0F / capsule.radius);
          contact = {point, normal, fraction, triangle.ordinal, true};
        }
        continue;
      }
      const auto incidence = math::dot(delta, triangle.normal);
      if (incidence >= -kTolerance) {
        continue;
      }
      const auto fraction =
          math::dot(math::subtract(triangle.vertices[0], origin),
                    triangle.normal) /
          incidence;
      if (fraction < 0 || fraction > contact.depth) {
        continue;
      }
      const auto point = math::add(origin, math::scale(delta, fraction));
      if (!inside(point, triangle.vertices, triangle.normal) ||
          (contact.found && fraction == contact.depth &&
           triangle.ordinal > contact.ordinal)) {
        continue;
      }
      contact = {point, triangle.normal, fraction, triangle.ordinal,
                 true,  triangle.surface};
    }
  }
  return contact;
}
CharacterCollisionResult StaticCollision::move_character(
    const CharacterCollisionRequest& request) const {
  if (!character_ready_) {
    throw std::logic_error("Character collision geometry is not prepared");
  }
  if (!math::all_finite(request.previous_position) ||
      !math::all_finite(request.proposed_position) ||
      !std::isfinite(request.radius) || request.radius <= 0 ||
      !std::isfinite(request.cylinder_height) || request.cylinder_height < 0 ||
      !std::isfinite(request.delta_seconds) || request.delta_seconds <= 0 ||
      !std::isfinite(request.vertical_velocity) ||
      !std::isfinite(request.ground_probe_extension) ||
      request.ground_probe_extension < 0 ||
      (request.mode != CharacterContactMode::grounded &&
       request.mode != CharacterContactMode::airborne)) {
    throw std::invalid_argument("Invalid character collision request");
  }
  CharacterCollisionResult result;
  result.position = request.proposed_position;
  const auto classify = [&](const Vec& normal) {
    if (normal[1] < -kContactNormal) {
      result.hit_ceiling = true;
    } else if (normal[1] < kContactNormal) {
      result.hit_wall = true;
    }
  };
  // Protect previous-to-proposed motion before correcting the finite body.
  const auto crossing =
      segment_contact(request.previous_position, result.position,
                      request.excluded_object_flags);
  if (crossing.found) {
    result.position =
        math::add(crossing.point, math::scale(crossing.normal, kClearance));
    classify(crossing.normal);
  }
  // The original capsule/sphere resolver performs two contact corrections. Each
  // correction includes source separation clearance and rechecks the next face.
  // Grounded small ticks raise only the body query, then remove that same lift;
  // the lower-sphere/floor phase still operates at the authoritative pose. This
  // is the original step accommodation, not an invented stair teleport.
  constexpr float kReferenceDelta = 1.0F / 30, kProbeLiftRate = 7.5F;
  const bool grounded_mode = request.mode == CharacterContactMode::grounded;
  const bool continuous_ground = grounded_mode && request.previously_grounded;
  const auto probe_lift =
      grounded_mode ? std::max(kReferenceDelta - request.delta_seconds, 0.0F) *
                          kProbeLiftRate
                    : 0.0F;
  result.position[1] += probe_lift;
  constexpr unsigned kCorrectionPasses = 2;
  for (unsigned pass = 0; pass < kCorrectionPasses; ++pass) {
    const auto contact =
        body_contact({result.position, request.radius, request.cylinder_height,
                      request.excluded_object_flags});
    if (!contact.found) {
      break;
    }
    const auto corrected =
        math::add(result.position,
                  math::scale(contact.normal, contact.depth + kClearance));
    const auto obstruction = segment_contact(result.position, corrected,
                                             request.excluded_object_flags);
    result.position =
        obstruction.found
            ? math::add(obstruction.point,
                        math::scale(obstruction.normal, kClearance))
            : corrected;
    classify(contact.normal);
  }
  result.position[1] -= probe_lift;
  // Source grounded contact always probes support, including positive animated
  // root motion. Only airborne ascent suppresses floor capture.
  if (grounded_mode || request.vertical_velocity <= 0) {
    constexpr float kSupportNormal = 0.9539392F;
    const auto reach = continuous_ground
                           ? kGroundReach -
                                 (1.0F - kSupportNormal) * request.radius +
                                 request.ground_probe_extension
                           : kClearance;
    // Grounded support queries expand the lower sphere by its radius, then
    // subtract that expansion from the correction clearance. Contact edges
    // therefore supply their actual separation direction rather than a
    // guessed vertical step. A higher support contact wins over the floor
    // beneath it; otherwise a short stair would be undone by the down probe.
    if (continuous_ground) {
      const auto support =
          body_contact({result.position, 2 * request.radius, 0,
                        request.excluded_object_flags, kContactNormal});
      const auto correction = support.depth - request.radius + kClearance;
      if (support.found && correction >= -reach &&
          correction <= kGroundReach + kClearance) {
        const auto corrected =
            math::add(result.position, math::scale(support.normal, correction));
        const auto obstruction = segment_contact(result.position, corrected,
                                                 request.excluded_object_flags);
        result.position =
            obstruction.found
                ? math::add(obstruction.point,
                            math::scale(obstruction.normal, kClearance))
                : corrected;
        if (obstruction.found) {
          classify(obstruction.normal);
        }
        result.ground_normal = support.normal;
        result.grounded = true;
      }
    }
    auto floor_target = result.position;
    floor_target[1] -= request.radius / kWalkableNormal + reach;
    const auto floor = segment_contact(result.position, floor_target,
                                       request.excluded_object_flags);
    if (floor.found && floor.normal[1] >= kWalkableNormal) {
      const auto supported_y =
          floor.point[1] + request.radius / floor.normal[1];
      const auto correction = supported_y - result.position[1];
      if (correction >= -reach - kClearance &&
          correction <= kGroundReach + kClearance &&
          (!result.grounded || correction >= -kClearance)) {
        result.position[1] = supported_y + kClearance;
        result.ground_normal = floor.normal;
        result.grounded = true;
      }
    }
  }
  if (!math::all_finite(result.position)) {
    throw std::invalid_argument("Unrepresentable character collision result");
  }
  return result;
}
}  // namespace bw::scene
