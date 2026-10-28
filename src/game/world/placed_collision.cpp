// Shared math retains each caller's scalar/accumulator precision; tolerance
// and failure policy remain here. Both-phase I.9/MEM.9 contracts are in
// bw/math/vector.hpp and matrix.hpp.
// Prepare authored shapes into local collision primitives, then reuse bounded
// scratch when applying body-world placements. Convex preparation uses an
// incremental outward hull with horizon expansion; it runs once per model.
// Runtime collision remains in StaticCollision. See placed_collision.hpp for
// ownership, failure and cost contracts, and placed-physics-collision.md for
// original source and pinned Jolt algorithm study. Both MCP phases: I.1
// (ri-explicit) keeps source geometry separate from registration; MEM.9
// (guidelines/memory/MEM.9-embedded-allocate-at-init.md) confines allocation to
// setup and retains reusable per-instance transformation buffers.
#include "placed_collision.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <stdexcept>
#include <tuple>

#include "bw/content/formats/binary_reader.hpp"
#include "bw/math/matrix.hpp"
namespace bw::game {
namespace {
using Vec = math::Vector3<float>;
using DVec = math::Vector3<double>;
using Face = std::array<std::size_t, 3>;
using Edge = std::array<std::size_t, 2>;
constexpr double kRigidTolerance = .001;
constexpr double kHullRelativeTolerance = 1e-8;
constexpr std::size_t kMinimumHullPoints = 4;
constexpr std::size_t kHullEdgeCapacityPerPoint = 6;
constexpr std::size_t kBoxCorners = 8;
constexpr std::uint64_t kFloatBytes = 4, kPointBytes = 12;
DVec normal(const Face& face, std::span<const DVec> points) {
  return math::cross(math::subtract(points[face[1]], points[face[0]]),
                     math::subtract(points[face[2]], points[face[0]]));
}
double signed_distance(const Face& face, std::span<const DVec> points,
                       const DVec& point) {
  const auto n = normal(face, points);
  return math::dot(n, math::subtract(point, points[face[0]])) / math::length(n);
}
template <class Matrix>
void validate_rigid(const Matrix& matrix) {
  for (const auto& row : matrix) {
    for (const auto value : row) {
      if (!std::isfinite(value)) {
        throw std::invalid_argument("Nonfinite placed collision pose");
      }
    }
  }
  std::array<DVec, 3> basis{};
  for (std::size_t i = 0; i < basis.size(); ++i) {
    for (std::size_t j = 0; j < basis.size(); ++j) {
      basis.at(i).at(j) = matrix.at(i).at(j);
    }
    for (std::size_t j = 0; j <= i; ++j) {
      if (std::abs(math::dot(basis.at(i), basis.at(j)) - (i == j ? 1 : 0)) >
          kRigidTolerance) {
        throw std::invalid_argument("Placed collision pose is not rigid");
      }
    }
  }
  if (math::dot(math::cross(basis[0], basis[1]), basis[2]) <
      1 - kRigidTolerance) {
    throw std::invalid_argument("Placed collision pose changes handedness");
  }
}
template <class Matrix>
Vec transform(const Vec& point, const Matrix& matrix) {
  const auto result = math::transform_point<double>(point, matrix);
  if (!math::all_finite(result)) {
    throw std::invalid_argument("Unrepresentable placed collision point");
  }
  return result;
}
Face outward(Face face, std::span<const DVec> points, const DVec& interior,
             double epsilon) {
  const auto n = normal(face, points);
  if (math::dot(n, n) <= epsilon * epsilon * epsilon * epsilon) {
    throw std::invalid_argument("Degenerate convex collision face");
  }
  if (math::dot(n, math::subtract(interior, points[face[0]])) > 0) {
    std::swap(face[1], face[2]);
  }
  return face;
}
auto edge_key(const Edge& edge) {
  return std::pair{std::min(edge[0], edge[1]), std::max(edge[0], edge[1])};
}
// A broad initial tetrahedron avoids choosing a nearly collinear file prefix.
std::array<std::size_t, 4> initial_tetrahedron(std::span<const DVec> points,
                                               double epsilon) {
  std::array<std::size_t, 4> selected{};
  double best = -1;
  for (std::size_t i = 0; i < points.size(); ++i) {
    const double distance = math::dot(points[i], points[i]);
    if (distance > best) {
      best = distance;
      selected[0] = i;
    }
  }
  best = -1;
  for (std::size_t i = 0; i < points.size(); ++i) {
    const auto delta = math::subtract(points[i], points[selected[0]]);
    if (math::dot(delta, delta) > best) {
      best = math::dot(delta, delta);
      selected[1] = i;
    }
  }
  best = -1;
  const auto line = math::subtract(points[selected[1]], points[selected[0]]);
  for (std::size_t i = 0; i < points.size(); ++i) {
    const auto area =
        math::cross(line, math::subtract(points[i], points[selected[0]]));
    if (math::dot(area, area) > best) {
      best = math::dot(area, area);
      selected[2] = i;
    }
  }
  if (best <= epsilon * epsilon * epsilon * epsilon) {
    throw std::invalid_argument("Collinear convex collision cloud");
  }
  const Face plane{selected[0], selected[1], selected[2]};
  best = -1;
  for (std::size_t i = 0; i < points.size(); ++i) {
    const double distance = std::abs(signed_distance(plane, points, points[i]));
    if (distance > best) {
      best = distance;
      selected[3] = i;
    }
  }
  if (best <= epsilon) {
    throw std::invalid_argument("Coplanar convex collision cloud");
  }
  return selected;
}
memory::Vector<scene::CollisionTriangle> convex_hull(
    std::span<const std::uint8_t> bytes,
    const std::shared_ptr<memory::Domain>& domain) {
  const resources::detail::Reader reader{bytes};
  const auto count = bytes.size() / kPointBytes;
  if (bytes.size() % kPointBytes != 0 || count < kMinimumHullPoints) {
    throw std::invalid_argument("Invalid convex collision point count");
  }
  memory::Vector<DVec> points{memory::Allocator<DVec>(domain)};
  points.resize(count);
  double scale = 0;
  for (std::size_t point = 0; point < count; ++point) {
    for (std::size_t axis = 0; axis < 3; ++axis) {
      const auto value = std::bit_cast<float>(
          reader.u32(point * kPointBytes + axis * kFloatBytes));
      if (!std::isfinite(value)) {
        throw std::invalid_argument("Nonfinite convex collision point");
      }
      points.at(point).at(axis) = value;
      scale = std::max(scale, std::abs(static_cast<double>(value)));
    }
  }
  const double epsilon = std::max(scale, 1.0) * kHullRelativeTolerance;
  const auto initial = initial_tetrahedron(points, epsilon);
  DVec interior{};
  for (const auto index : initial) {
    for (std::size_t axis = 0; axis < interior.size(); ++axis) {
      interior.at(axis) += points.at(index).at(axis) / initial.size();
    }
  }
  memory::Vector<Face> faces{memory::Allocator<Face>(domain)};
  memory::Vector<Edge> edges{memory::Allocator<Edge>(domain)};
  faces.reserve(2 * count);
  edges.reserve(kHullEdgeCapacityPerPoint * count);
  for (std::size_t omitted = 0; omitted < initial.size(); ++omitted) {
    Face face{};
    std::size_t corner = 0;
    for (std::size_t i = 0; i < initial.size(); ++i) {
      if (i != omitted) {
        face.at(corner++) = initial.at(i);
      }
    }
    faces.push_back(outward(face, points, interior, epsilon));
  }
  // Remove visible faces and cancel their paired interior edges; the remaining
  // horizon edges each create one new outward face to the added point.
  for (std::size_t point = 0; point < points.size(); ++point) {
    edges.clear();
    std::erase_if(faces, [&](const Face& face) {
      if (signed_distance(face, points, points.at(point)) <= epsilon) {
        return false;
      }
      for (std::size_t edge = 0; edge < face.size(); ++edge) {
        edges.push_back({face.at(edge), face.at((edge + 1) % face.size())});
      }
      return true;
    });
    std::ranges::sort(edges, {}, edge_key);
    for (std::size_t edge = 0; edge < edges.size();) {
      std::size_t end = edge + 1;
      while (end < edges.size() &&
             edge_key(edges.at(end)) == edge_key(edges.at(edge))) {
        ++end;
      }
      if (end - edge == 1) {
        faces.push_back(outward({edges.at(edge)[0], edges.at(edge)[1], point},
                                points, interior, epsilon));
      } else if (end - edge != 2 ||
                 edges.at(edge)[0] != edges.at(edge + 1)[1]) {
        throw std::invalid_argument("Invalid convex collision horizon");
      }
      edge = end;
    }
  }
  // Confirm a closed outward hull contains the source cloud before publication.
  edges.clear();
  for (const auto& face : faces) {
    for (const auto& point : points) {
      if (signed_distance(face, points, point) > epsilon * 4) {
        throw std::invalid_argument(
            "Convex collision hull excludes source point");
      }
    }
    for (std::size_t edge = 0; edge < face.size(); ++edge) {
      edges.push_back({face.at(edge), face.at((edge + 1) % face.size())});
    }
  }
  std::ranges::sort(edges, {}, edge_key);
  if (faces.size() < kMinimumHullPoints || edges.size() % 2 != 0) {
    throw std::invalid_argument("Open convex collision hull");
  }
  for (std::size_t edge = 0; edge < edges.size(); edge += 2) {
    if (edges.at(edge)[0] != edges.at(edge + 1)[1] ||
        edges.at(edge)[1] != edges.at(edge + 1)[0]) {
      throw std::invalid_argument("Nonmanifold convex collision hull");
    }
  }
  memory::Vector<scene::CollisionTriangle> result{
      memory::Allocator<scene::CollisionTriangle>(domain)};
  result.reserve(faces.size());
  for (const auto& face : faces) {
    scene::CollisionTriangle triangle;
    for (std::size_t corner = 0; corner < face.size(); ++corner) {
      for (std::size_t axis = 0; axis < 3; ++axis) {
        triangle.vertices.at(corner).at(axis) =
            static_cast<float>(points.at(face.at(corner)).at(axis));
      }
    }
    result.push_back(triangle);
  }
  return result;
}
constexpr std::array<Face, 12> kBoxFaces{{{0, 2, 1},
                                          {1, 2, 3},
                                          {4, 5, 6},
                                          {5, 7, 6},
                                          {0, 1, 4},
                                          {1, 5, 4},
                                          {2, 6, 3},
                                          {3, 6, 7},
                                          {0, 4, 2},
                                          {2, 4, 6},
                                          {1, 3, 5},
                                          {3, 7, 5}}};
}  // namespace
PreparedPropCollision::PreparedPropCollision(
    const formats::PhysicsCollisionDefinition& definition,
    const std::shared_ptr<memory::Domain>& domain)
    : body_count_(definition.body_joints().size()),
      triangles_(memory::Allocator<Triangle>(domain)),
      capsules_(memory::Allocator<Capsule>(domain)),
      world_triangles_(memory::Allocator<scene::CollisionTriangle>(domain)),
      world_capsules_(memory::Allocator<scene::CollisionCapsule>(domain)) {
  for (const auto& shape : definition.shapes()) {
    validate_rigid(shape.local_pose);
    const auto append = [&](scene::CollisionTriangle triangle) {
      for (auto& point : triangle.vertices) {
        point = transform(point, shape.local_pose);
      }
      triangles_.push_back({triangle, shape.body});
    };
    switch (shape.kind) {
      case formats::PhysicsShapeKind::box: {
        std::array<Vec, kBoxCorners> corners{};
        for (std::size_t corner = 0; corner < corners.size(); ++corner) {
          for (std::size_t axis = 0; axis < 3; ++axis) {
            corners.at(corner).at(axis) = ((corner >> axis) & 1U) != 0
                                              ? shape.dimensions.at(axis)
                                              : -shape.dimensions.at(axis);
          }
        }
        for (const auto& face : kBoxFaces) {
          append({{corners.at(face[0]), corners.at(face[1]),
                   corners.at(face[2])}});
        }
        break;
      }
      case formats::PhysicsShapeKind::convex:
        for (const auto& triangle : convex_hull(shape.convex_points, domain)) {
          append(triangle);
        }
        break;
      case formats::PhysicsShapeKind::sphere:
      case formats::PhysicsShapeKind::capsule: {
        const float half_height =
            shape.kind == formats::PhysicsShapeKind::capsule
                ? shape.dimensions[1] / 2
                : 0;
        if (shape.dimensions[0] <= 0) {
          throw std::invalid_argument("Nonpositive collision capsule radius");
        }
        capsules_.push_back({{transform({0, -half_height, 0}, shape.local_pose),
                              transform({0, half_height, 0}, shape.local_pose),
                              shape.dimensions[0]},
                             shape.body});
        break;
      }
      case formats::PhysicsShapeKind::triangle_mesh:
      case formats::PhysicsShapeKind::triangle_mesh_alt:
        throw std::invalid_argument(
            "Placed triangle-mesh physics shape remains unsupported");
    }
  }
  world_triangles_.resize(triangles_.size());
  world_capsules_.resize(capsules_.size());
}
void PreparedPropCollision::register_instance(
    std::span<const formats::PlacementMatrix> body_world,
    scene::StaticCollision& collision) {
  if (body_world.size() != body_count_) {
    throw std::invalid_argument("Placed collision body count differs");
  }
  for (const auto& pose : body_world) {
    validate_rigid(pose);
    if (pose[0][3] != 0 || pose[1][3] != 0 || pose[2][3] != 0 ||
        pose[3][3] != 1) {
      throw std::invalid_argument("Nonaffine placed collision pose");
    }
  }
  for (std::size_t i = 0; i < triangles_.size(); ++i) {
    const auto& local = triangles_.at(i);
    auto& world = world_triangles_.at(i);
    world.object_flags = local.geometry.object_flags;
    for (std::size_t corner = 0; corner < world.vertices.size(); ++corner) {
      world.vertices.at(corner) =
          transform(local.geometry.vertices.at(corner), body_world[local.body]);
    }
  }
  for (std::size_t i = 0; i < capsules_.size(); ++i) {
    const auto& local = capsules_.at(i);
    auto& world = world_capsules_.at(i);
    world = local.geometry;
    world.start = transform(world.start, body_world[local.body]);
    world.end = transform(world.end, body_world[local.body]);
  }
  collision.add_triangles(world_triangles_, placed_background_collision_group,
                          true);
  collision.add_capsules(world_capsules_, placed_background_collision_group,
                         true);
}
}  // namespace bw::game
