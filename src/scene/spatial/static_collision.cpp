// Apply original static-part classification and segment/triangle clearance.
// Borrowed byte extents were validated by CollisionDefinition; individual
// reads remain checked. Metadata allocation occurs only during registration.
// See the header for lifetimes, failure and scaling. Both MCP phases: R.2
// (#rr-use-ptr) bounds borrowed data; MEM.6
// (guidelines/memory/MEM.6-inject-allocators-behind-one-interface.md) injects
// registration storage. Source equations and tolerances are recorded in
// research/engine/subsystems/player-camera-initialization.md.
#include "bw/scene/spatial/static_collision.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <stdexcept>

#include "../../formats/reader.hpp"

namespace bw::scene {
namespace {
using Vec = std::array<float, 3>;
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
Vec vector_add(const Vec& a, const Vec& b) {
  return {a[0] + b[0], a[1] + b[1], a[2] + b[2]};
}
Vec subtract(const Vec& a, const Vec& b) {
  return {a[0] - b[0], a[1] - b[1], a[2] - b[2]};
}
Vec scale(const Vec& a, float value) {
  return {a[0] * value, a[1] * value, a[2] * value};
}
float dot(const Vec& a, const Vec& b) {
  return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}
Vec cross(const Vec& a, const Vec& b) {
  return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2],
          a[0] * b[1] - a[1] * b[0]};
}
bool finite(const Vec& value) {
  return std::all_of(value.begin(), value.end(),
                     [](float v) { return std::isfinite(v); });
}
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
    const auto edge = subtract(
        vertices.at((i + vertices.size() - 1) % vertices.size()), start);
    const float edge_length = std::sqrt(dot(edge, edge));
    if (edge_length <= 0) {
      continue;
    }
    const auto edge_direction = scale(edge, 1.0F / edge_length);
    const auto delta = subtract(start, ray.origin);
    const float parallel = dot(ray.direction, edge_direction);
    const float along_ray = dot(delta, ray.direction),
                along_edge = dot(delta, edge_direction);
    const float denominator = 1.0F - parallel * parallel;
    const float edge_distance =
        std::abs(denominator) >= kIntersectionTolerance
            ? std::clamp((-along_edge + along_ray * parallel) / denominator,
                         0.0F, edge_length)
            : edge_length * 0.5F;
    const float distance = along_ray + edge_distance * parallel;
    const auto separation =
        subtract(vector_add(ray.origin, scale(ray.direction, distance)),
                 vector_add(start, scale(edge_direction, edge_distance)));
    const float distance_squared = dot(separation, separation);
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
  const auto edge1 = subtract(vertices[1], vertices[0]);
  const auto edge2 = subtract(vertices[2], vertices[0]);
  const auto p = cross(ray.direction, edge2);
  const float determinant = dot(edge1, p);
  if (determinant < 0) {
    return std::nullopt;
  }
  const auto delta = subtract(ray.origin, vertices[0]);
  if (determinant < kIntersectionTolerance) {
    if (dot(scale(delta, -1.0F), cross(edge1, edge2)) != 0) {
      return std::nullopt;
    }
    return coplanar_hit(ray, vertices);
  }
  const float u = dot(delta, p);
  if (u < 0 || u > determinant) {
    return std::nullopt;
  }
  const auto q = cross(delta, edge1);
  const float v = dot(ray.direction, q);
  if (v < 0 || u + v > determinant) {
    return std::nullopt;
  }
  return dot(edge2, q) / determinant;
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
      if (!finite(vertex(part, index))) {
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
}
SegmentClearanceResult StaticCollision::clip_segment(
    const SegmentClearance& request) const {
  if (!finite(request.origin) || !finite(request.target) ||
      !std::isfinite(request.clearance) || request.clearance < 0) {
    throw std::invalid_argument("Invalid static segment clearance request");
  }
  const auto delta = subtract(request.target, request.origin);
  const float length = std::sqrt(dot(delta, delta));
  if (length == 0) {
    return {request.target, false};
  }
  const auto direction = scale(delta, 1.0F / length);
  const float extended_length =
      length + request.clearance / kClearanceExtensionDivisor;
  if (!std::isfinite(extended_length)) {
    throw std::invalid_argument("Unrepresentable clearance segment");
  }
  const Ray ray{
      vector_add(request.origin, scale(direction, extended_length * 0.5F)),
      direction, extended_length * 0.5F};
  float closest = std::numeric_limits<float>::max();
  Vec normal{};
  for (const auto& registered : parts_) {
    if ((registered.object_flags &
         (request.excluded_object_flags | kDisabled)) != 0) {
      continue;
    }
    const auto& part = registered.definition.get();
    if (!overlaps(ray, part)) {
      continue;
    }
    for (std::uint32_t i = 0; i < part.triangle_count; ++i) {
      const auto vertices = triangle(part, i);
      const auto hit = triangle_hit(ray, vertices);
      if (hit && *hit >= -ray.half_length && *hit <= ray.half_length &&
          *hit < closest) {
        closest = *hit;
        normal = cross(subtract(vertices[1], vertices[0]),
                       subtract(vertices[2], vertices[0]));
      }
    }
  }
  if (closest == std::numeric_limits<float>::max()) {
    return {request.target, false};
  }
  const float normal_length = std::sqrt(dot(normal, normal));
  normal = normal_length > 0 ? scale(normal, 1.0F / normal_length)
                             : scale(direction, -1.0F);
  const float incidence = -dot(direction, normal);
  const float clearance = std::abs(incidence) > kIncidenceTolerance
                              ? request.clearance / incidence
                              : request.clearance;
  const float travel = ray.half_length + closest - clearance;
  if (travel >= length) {
    return {request.target, false};
  }
  return {vector_add(request.origin, scale(direction, std::max(0.0F, travel))),
          true};
}
}  // namespace bw::scene
