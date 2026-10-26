// Report the chosen authored face surface alongside the existing corrected
// endpoint. Prepared and borrowed paths preserve the same byte; analytic hits
// retain absent metadata. Both-phase I.1/E.4/CDSA.32/MEM.9 contracts are in the
// header; query results copy values, never borrow reordered primitive storage.
// Shared math retains each caller's scalar/accumulator precision; tolerance
// and failure policy remain here. Both-phase I.9/MEM.9 contracts are in
// bw/math/vector.hpp and matrix.hpp.
// Apply original static-part classification and segment/triangle clearance.
// Borrowed byte extents were validated by CollisionDefinition; individual
// reads remain checked. Metadata allocation occurs only during registration.
// See the header for lifetimes, failure and scaling. Both MCP phases: R.2
// (#rr-use-ptr) bounds borrowed data; MEM.6
// (guidelines/memory/MEM.6-inject-allocators-behind-one-interface.md) injects
// registration storage. Source equations and tolerances are recorded in
// research/engine/subsystems/player-camera-initialization.md.
#include "bw/spatial/static_collision.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <stdexcept>

#include "bw/content/formats/binary_reader.hpp"
#include "bw/math/vector.hpp"

namespace bw::scene {
namespace {
using Vec = math::Vector3<float>;
constexpr std::uint32_t kStaticFlags = 0x10001, kDisabled = 0x80000000;
constexpr std::uint32_t kSolidCategory = 0x40, kSpecialCategory = 0x20;
constexpr std::uint32_t kPlayerWall = 0x1200, kPlayerCamera = 0x3200;
constexpr std::uint32_t kNonPlayer = 0x200, kNonEnemy = 0x2100,
                        kNonCamera = 0x2200;
constexpr float kIntersectionTolerance = 0.000001F;
constexpr float kIncidenceTolerance = 0.0001F;
constexpr float kClearanceExtensionDivisor = 0.01F;
constexpr std::size_t kVertexBytes = 12, kFaceBytes = 8, kIndexBytes = 2;
constexpr std::size_t kFloatBytes = sizeof(float);
constexpr unsigned kByteBits = 8;
bool prefix(std::string_view name, std::string_view expected) {
  if (name.size() < expected.size()) {
    return false;
  }
  for (std::size_t i = 0; i < expected.size(); ++i) {
    auto ch = name[i];
    if (ch >= 'A' && ch <= 'Z') {
      ch = static_cast<char>(ch + ('a' - 'A'));
    }
    if (ch != expected[i]) {
      return false;
    }
  }
  return true;
}
std::optional<std::uint32_t> registration_flags(
    const formats::CollisionPart& part, CollisionSourceMode mode) {
  if (part.vertex_count == 0 || part.triangle_count == 0) {
    return std::nullopt;
  }
  if (mode == CollisionSourceMode::combined) {
    return kStaticFlags;
  }
  if (prefix(part.name, "wa_") || prefix(part.name, "de_") ||
      ((part.flags & kSolidCategory) == 0 &&
       (part.flags & kSpecialCategory) != 0) ||
      (mode == CollisionSourceMode::character &&
       (prefix(part.name, "pp_") || prefix(part.name, "hp_")))) {
    return std::nullopt;
  }
  auto flags = kStaticFlags;
  if (prefix(part.name, "pw_")) {
    flags |= kPlayerWall;
  } else if (prefix(part.name, "pc_")) {
    flags |= kPlayerCamera;
  } else if (prefix(part.name, "np_")) {
    flags |= kNonPlayer;
  } else if (prefix(part.name, "ne_")) {
    flags |= kNonEnemy;
  } else if (prefix(part.name, "nc_") || prefix(part.name, "cc_")) {
    flags |= kNonCamera;
  }
  return flags;
}
Vec vertex(const formats::CollisionPart& part, std::uint32_t index) {
  const resources::detail::Reader reader{part.vertices};
  Vec result{};
  for (std::size_t axis = 0; axis < result.size(); ++axis) {
    result.at(axis) = std::bit_cast<float>(
        reader.u32(std::uint64_t{index} * kVertexBytes + axis * kFloatBytes));
  }
  return result;
}
std::array<Vec, 3> triangle(const formats::CollisionPart& part,
                            std::uint32_t index) {
  const resources::detail::Reader reader{part.triangles};
  std::array<Vec, 3> result{};
  for (std::size_t corner = 0; corner < result.size(); ++corner) {
    const std::size_t offset =
        std::size_t{index} * kFaceBytes + corner * kIndexBytes;
    reader.range(offset, kIndexBytes);
    const auto value = std::uint32_t{part.triangles[offset]} |
                       (std::uint32_t{part.triangles[offset + 1]} << kByteBits);
    result.at(corner) = vertex(part, value);
  }
  return result;
}
struct Ray {
  Vec origin{}, direction{};
  float half_length{};
};
// Compare segment and part bounds before touching the triangle stream.
bool overlaps(const Ray& ray, const formats::CollisionPart& part) {
  for (std::size_t axis = 0; axis < ray.origin.size(); ++axis) {
    const float reach = std::abs(ray.direction.at(axis) * ray.half_length);
    if (ray.origin.at(axis) + reach <
            part.center.at(axis) - part.extent.at(axis) ||
        ray.origin.at(axis) - reach >
            part.center.at(axis) + part.extent.at(axis)) {
      return false;
    }
  }
  return true;
}
// Coplanar contact uses the closest point on each bounded triangle edge. For
// parallel lines the original routine chooses the edge midpoint.
std::optional<float> coplanar_hit(const Ray& ray,
                                  const std::array<Vec, 3>& vertices) {
  float closest = std::numeric_limits<float>::max();
  float best_distance = std::numeric_limits<float>::max();
  for (std::size_t i = 0; i < vertices.size(); ++i) {
    const auto& start = vertices.at(i);
    const auto edge = math::subtract(
        vertices.at((i + vertices.size() - 1) % vertices.size()), start);
    const float edge_length = math::length(edge);
    if (edge_length <= 0) {
      continue;
    }
    const auto edge_direction = math::scale(edge, 1.0F / edge_length);
    const auto delta = math::subtract(start, ray.origin);
    const float parallel = math::dot(ray.direction, edge_direction);
    const float along_ray = math::dot(delta, ray.direction),
                along_edge = math::dot(delta, edge_direction);
    const float denominator = 1.0F - parallel * parallel;
    const float edge_distance =
        std::abs(denominator) >= kIntersectionTolerance
            ? std::clamp((-along_edge + along_ray * parallel) / denominator,
                         0.0F, edge_length)
            : edge_length * 0.5F;
    const float distance = along_ray + edge_distance * parallel;
    const auto separation = math::subtract(
        math::add(ray.origin, math::scale(ray.direction, distance)),
        math::add(start, math::scale(edge_direction, edge_distance)));
    const float distance_squared = math::dot(separation, separation);
    if (distance < closest && distance_squared <= kIntersectionTolerance &&
        distance_squared <= best_distance + kIntersectionTolerance) {
      closest = distance;
      best_distance = distance_squared;
    }
  }
  if (best_distance > kIntersectionTolerance) {
    return std::nullopt;
  }
  return closest;
}
std::optional<float> triangle_hit(const Ray& ray,
                                  const std::array<Vec, 3>& vertices) {
  const auto edge1 = math::subtract(vertices[1], vertices[0]);
  const auto edge2 = math::subtract(vertices[2], vertices[0]);
  const auto p = math::cross(ray.direction, edge2);
  const float determinant = math::dot(edge1, p);
  if (determinant < 0) {
    return std::nullopt;
  }
  const auto delta = math::subtract(ray.origin, vertices[0]);
  if (determinant < kIntersectionTolerance) {
    if (math::dot(math::scale(delta, -1.0F), math::cross(edge1, edge2)) != 0) {
      return std::nullopt;
    }
    return coplanar_hit(ray, vertices);
  }
  const float u = math::dot(delta, p);
  if (u < 0 || u > determinant) {
    return std::nullopt;
  }
  const auto q = math::cross(delta, edge1);
  const float v = math::dot(ray.direction, q);
  if (v < 0 || u + v > determinant) {
    return std::nullopt;
  }
  return math::dot(edge2, q) / determinant;
}
}  // namespace
void StaticCollision::add(const formats::CollisionDefinition& definition,
                          CollisionSourceMode mode) {
  // Legacy combined records have a different part header and no typed decoder.
  if (mode == CollisionSourceMode::combined) {
    resources::detail::unsupported(
        "Legacy combined collision layout is not decoded");
  }
  constexpr unsigned kVertexFormatShift = 2;
  constexpr std::uint32_t kVertexFormatMask = 7, kFloat3Format = 1;
  for (const auto& part : definition.parts()) {
    if (!registration_flags(part, mode)) {
      continue;
    }
    if (((part.flags >> kVertexFormatShift) & kVertexFormatMask) !=
        kFloat3Format) {
      resources::detail::unsupported(
          "Static collision query requires float3 vertices");
    }
    for (std::uint32_t index = 0; index < part.vertex_count; ++index) {
      if (!math::all_finite(vertex(part, index))) {
        resources::detail::malformed("Nonfinite static collision vertex");
      }
    }
  }
  // Reserve before changing registration so validation/allocation failure
  // preserves it.
  if (definition.parts().size() > parts_.max_size() - parts_.size()) {
    throw std::length_error("Static collision registration exceeds capacity");
  }
  parts_.reserve(parts_.size() + definition.parts().size());
  for (const auto& part : definition.parts()) {
    if (const auto flags = registration_flags(part, mode)) {
      parts_.push_back({std::cref(part), *flags});
    }
  }
  character_ready_ = false;
}
SegmentClearanceResult StaticCollision::clip_segment(
    const SegmentClearance& request) const {
  if (!math::all_finite(request.origin) || !math::all_finite(request.target) ||
      !std::isfinite(request.clearance) || request.clearance < 0) {
    throw std::invalid_argument("Invalid static segment clearance request");
  }
  const auto delta = math::subtract(request.target, request.origin);
  const float length = math::length(delta);
  if (length == 0) {
    return {request.target, false};
  }
  const auto direction = math::scale(delta, 1.0F / length);
  const float extended_length =
      length + request.clearance / kClearanceExtensionDivisor;
  if (!std::isfinite(extended_length)) {
    throw std::invalid_argument("Unrepresentable clearance segment");
  }
  const Ray ray{
      math::add(request.origin, math::scale(direction, extended_length * 0.5F)),
      direction, extended_length * 0.5F};
  float closest = std::numeric_limits<float>::max();
  Vec normal{};
  std::optional<std::uint8_t> closest_surface;
  std::size_t closest_ordinal = std::numeric_limits<std::size_t>::max();
  const auto evaluate = [&](const std::array<Vec, 3>& vertices,
                            std::size_t ordinal,
                            std::optional<std::uint8_t> surface) {
    const auto hit = triangle_hit(ray, vertices);
    if (hit && *hit >= -ray.half_length && *hit <= ray.half_length &&
        (*hit < closest || (*hit == closest && ordinal < closest_ordinal))) {
      closest = *hit;
      closest_ordinal = ordinal;
      closest_surface = surface;
      normal = math::cross(math::subtract(vertices[1], vertices[0]),
                           math::subtract(vertices[2], vertices[0]));
    }
  };
  if (character_ready_) {
    // Live cameras reuse the prepared hierarchy, with the original triangle
    // narrow phase (including coplanar edges) rather than scanning every part.
    std::array<std::size_t, std::numeric_limits<std::size_t>::digits> stack{};
    std::size_t pending = character_nodes_.empty() ? 0 : 1;
    while (pending != 0) {
      const auto& node = character_nodes_.at(stack.at(--pending));
      bool separated = false;
      for (std::size_t axis = 0; axis < 3; ++axis) {
        const auto extent = std::abs(ray.direction.at(axis) * ray.half_length);
        if (ray.origin.at(axis) + extent < node.minimum.at(axis) ||
            ray.origin.at(axis) - extent > node.maximum.at(axis)) {
          separated = true;
          break;
        }
      }
      if (separated) continue;
      if (node.count == 0) {
        stack.at(pending++) = node.first;
        stack.at(pending++) = node.right;
        continue;
      }
      for (std::size_t i = node.first; i < node.first + node.count; ++i) {
        const auto& primitive = character_primitives_.at(i);
        if (!primitive.capsule &&
            (primitive.flags & (request.excluded_object_flags | kDisabled)) ==
                0) {
          evaluate(primitive.vertices, primitive.ordinal, primitive.surface);
        }
      }
    }
    if (!capsules_.empty()) {
      const auto hit = segment_contact(
          request.origin,
          math::add(request.origin, math::scale(direction, extended_length)),
          request.excluded_object_flags);
      const auto distance = hit.depth * extended_length - ray.half_length;
      if (hit.found && distance < closest) {
        closest = distance;
        normal = hit.normal;
        closest_surface = hit.surface;
      }
    }
  } else {
    if (!additional_triangles_.empty() || !capsules_.empty()) {
      throw std::logic_error("Placed collision geometry is not prepared");
    }
    // Camera-only callers may still borrow validated stage definitions without
    // character preparation; this retains the original initial-view contract.
    std::size_t ordinal = 0;
    for (const auto& registered : parts_) {
      if ((registered.object_flags &
           (request.excluded_object_flags | kDisabled)) != 0) {
        continue;
      }
      const auto& part = registered.definition.get();
      if (!overlaps(ray, part)) continue;
      for (std::uint32_t i = 0; i < part.triangle_count; ++i) {
        constexpr std::size_t kFaceBytes = 8, kSurfaceByte = 7;
        evaluate(triangle(part, i), ordinal++,
                 part.triangles[i * kFaceBytes + kSurfaceByte]);
      }
    }
  }
  if (closest == std::numeric_limits<float>::max()) {
    return {request.target, false};
  }
  const float normal_length = math::length(normal);
  normal = normal_length > 0 ? math::scale(normal, 1.0F / normal_length)
                             : math::scale(direction, -1.0F);
  const float incidence = -math::dot(direction, normal);
  const float clearance = std::abs(incidence) > kIncidenceTolerance
                              ? request.clearance / incidence
                              : request.clearance;
  const float travel = ray.half_length + closest - clearance;
  if (travel >= length) {
    return {request.target, false};
  }
  return {
      math::add(request.origin, math::scale(direction, std::max(0.0F, travel))),
      true, closest_surface};
}
}  // namespace bw::scene
