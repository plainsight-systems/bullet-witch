// Original constrained secondary-bone integration. Preparation owns topology,
// mutable state and scratch; runtime consumes clean pose frames, then publishes
// source-selected bone overrides. Header contracts cite both MCP phases.
// Shared math owns arithmetic; normalized-direction fallback and source update
// ordering remain this solver's policy. No world physics backend is invoked.
#include "bw/animation/secondary_motion.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <stdexcept>
namespace bw::animation {
namespace {
using V = SecondaryVector;
using math::add;
using math::cross;
using math::divide;
using math::dot;
using math::length;
using math::scale;
using math::subtract;
constexpr float kHalf = 0.5F;
constexpr auto kCapsuleMaskBits = std::numeric_limits<std::uint32_t>::digits;
constexpr float kRestPull = 0.4F, kCorrection = 0.2F, kDamping = 0.75F;
constexpr float kGravity = 0.3266666829586029F, kWindResponse = 0.1F;
constexpr float kMinTime = 1.0e-8F, kZeroTimeReciprocal = 1.0e8F;
constexpr float kConeAngle = 1.0471975803375244F;
constexpr V kDown{0, -1, 0}, kUp{0, 1, 0}, kForward{0, 0, 1};
// Preserve the original first-hit capsule mask ordering. Axis coincidence has
// no source finite direction; use a deterministic perpendicular instead of NaN.
bool project_capsules(V& point, std::uint32_t mask,
                      std::span<const SecondaryCapsule> capsules) {
  for (const auto& capsule : capsules) {
    if (mask == 0) break;
    if ((mask & 1U) != 0) {
      const float along = dot(capsule.axis, subtract(point, capsule.origin));
      if (std::abs(along) <= capsule.half_length + capsule.radius) {
        const auto closest =
            add(capsule.origin,
                scale(capsule.axis, std::clamp(along, -capsule.half_length,
                                               capsule.half_length)));
        const auto delta = subtract(point, closest);
        const auto distance = length(delta);
        if (distance <= capsule.radius) {
          auto fallback = cross(capsule.axis, kForward);
          if (length(fallback) == 0) fallback = cross(capsule.axis, kUp);
          point = add(closest,
                      scale(math::normalize_or(
                                delta, math::normalize_or(fallback, {1, 0, 0})),
                            capsule.radius));
          return true;
        }
      }
    }
    mask >>= 1U;
  }
  return false;
}
template <int Count>
float harmonic(float phase) {
  constexpr float kFrequencyGrowth = 1.5F, kAmplitudeGrowth = 1.1F;
  float amplitude = 1, weighted = 0, total = 0;
  for (int i = 0; i < Count; ++i) {
    phase *= kFrequencyGrowth;
    amplitude *= kAmplitudeGrowth;
    total += amplitude;
    weighted +=
        static_cast<float>(std::sin(static_cast<double>(phase))) * amplitude;
  }
  return weighted / total;
}
}  // namespace
// Weight three source points, then orthonormalize their two basis directions.
void evaluate_control_points(const formats::ControlPointDefinition& definition,
                             std::span<const Matrix> skin_world,
                             std::span<Matrix> frames) {
  if (frames.size() != definition.frames().size()) {
    throw std::invalid_argument("Control frame count differs");
  }
  for (std::size_t index = 0; index < frames.size(); ++index) {
    const auto& source = definition.frames().at(index);
    frames[index] = kIdentityMatrix;
    if (!source.present) continue;
    std::array<V, 3> points{};
    if (source.influence_count == 0) points = source.points;
    for (const auto& influence : definition.influences(source)) {
      if (influence.bone >= skin_world.size()) {
        throw std::invalid_argument("Control-point skin index exceeds pose");
      }
      for (std::size_t point = 0; point < points.size(); ++point) {
        points.at(point) =
            add(points.at(point),
                scale(math::transform_point<float>(source.points.at(point),
                                                   skin_world[influence.bone]),
                      influence.weight));
      }
    }
    const auto forward = subtract(points[1], points[0]),
               up = subtract(points[2], points[0]);
    if (length(forward) == 0 || length(cross(up, forward)) == 0) {
      throw std::invalid_argument("Degenerate control-point frame");
    }
    const auto right = math::normalize_or(cross(up, forward), {1, 0, 0}),
               z = math::normalize_or(forward, kForward), y = cross(z, right);
    std::ranges::copy(right, frames[index][0].begin());
    std::ranges::copy(y, frames[index][1].begin());
    std::ranges::copy(z, frames[index][2].begin());
    math::set_translation(frames[index], points[0]);
  }
}
SecondaryMotion::SecondaryMotion(
    std::span<const SecondaryNodeDefinition> nodes,
    std::span<const SecondaryCapsuleDefinition> capsules, std::size_t bones,
    std::size_t control_points, const std::shared_ptr<memory::Domain>& domain)
    : definitions_(nodes.begin(), nodes.end(),
                   memory::Allocator<SecondaryNodeDefinition>(domain)),
      capsule_definitions_(
          capsules.begin(), capsules.end(),
          memory::Allocator<SecondaryCapsuleDefinition>(domain)),
      capsules_(capsules.size(), {},
                memory::Allocator<SecondaryCapsule>(domain)),
      nodes_(nodes.size(), {}, memory::Allocator<Node>(domain)),
      pending_(nodes.size(), {}, memory::Allocator<Node>(domain)),
      overrides_(memory::Allocator<BoneWorldOverride>(domain)),
      positions_(nodes.size(), {}, memory::Allocator<V>(domain)),
      bone_count_(bones),
      control_count_(control_points) {
  if (nodes.empty() || capsules.size() > kCapsuleMaskBits) {
    throw std::invalid_argument("Invalid secondary topology capacity");
  }
  const auto valid_index = [](std::int32_t index, std::size_t size) {
    return index >= -1 && (index < 0 || static_cast<std::size_t>(index) < size);
  };
  for (std::size_t i = 0; i < nodes.size(); ++i) {
    const auto& node = nodes[i];
    if (!valid_index(node.control_point, control_points) ||
        !valid_index(node.bone, bones) ||
        !valid_index(node.parent, nodes.size()) ||
        !valid_index(node.child, nodes.size()) ||
        !valid_index(node.neighbor_a, nodes.size()) ||
        !valid_index(node.neighbor_b, nodes.size()) ||
        !std::isfinite(node.neighbor_stiffness) ||
        node.neighbor_stiffness < 0) {
      throw std::invalid_argument("Invalid secondary node binding");
    }
    if (node.child >= 0 &&
        (static_cast<std::size_t>(node.child) <= i ||
         nodes[static_cast<std::size_t>(node.child)].parent !=
             static_cast<std::int32_t>(i))) {
      throw std::invalid_argument(
          "Secondary chain must be forward and reciprocal");
    }
    if (node.parent < 0 && node.child >= 0 && node.control_point < 0) {
      throw std::invalid_argument("Secondary root lacks control point");
    }
    if (node.kind == SecondaryNodeKind::adjacent_midpoint &&
        (i == 0 || i + 1 == nodes.size() || node.parent < 0)) {
      throw std::invalid_argument("Secondary midpoint lacks neighbors");
    }
    if (node.kind == SecondaryNodeKind::midpoint &&
        (node.parent < 0 || node.child < 0)) {
      throw std::invalid_argument("Secondary midpoint lacks endpoints");
    }
    if (node.bone >= 0) {
      overrides_.push_back(
          {static_cast<std::size_t>(node.bone), kIdentityMatrix});
    }
  }
  for (const auto& capsule : capsules) {
    if (capsule.bone >= bones || !math::all_finite(capsule.origin) ||
        !math::all_finite(capsule.axis) || length(capsule.axis) == 0 ||
        !std::isfinite(capsule.half_length) || capsule.half_length < 0 ||
        !std::isfinite(capsule.radius) || capsule.radius <= 0) {
      throw std::invalid_argument("Invalid secondary body capsule");
    }
  }
}
void SecondaryMotion::validate_inputs(std::span<const Matrix> controls,
                                      std::span<const Matrix> bones) const {
  if (controls.size() != control_count_ || bones.size() != bone_count_) {
    throw std::invalid_argument("Secondary pose count differs");
  }
  for (const auto& matrix : controls) {
    if (!math::all_finite(matrix)) {
      throw std::invalid_argument("Nonfinite control frame");
    }
  }
  for (const auto& matrix : bones) {
    if (!math::all_finite(matrix)) {
      throw std::invalid_argument("Nonfinite bone frame");
    }
  }
}
// Reconstruct direct frames first, then authored intermediate rows and lengths.
void SecondaryMotion::reset(std::span<const Matrix> controls,
                            std::span<const Matrix> bones) {
  validate_inputs(controls, bones);
  std::ranges::fill(pending_, Node{});
  for (std::size_t i = 0; i < definitions_.size(); ++i) {
    const auto& d = definitions_.at(i);
    auto& n = pending_.at(i);
    if (d.kind == SecondaryNodeKind::frame) {
      if (d.control_point >= 0) {
        n.frame = controls[static_cast<std::size_t>(d.control_point)];
      } else if (d.bone >= 0) {
        n.frame = bones[static_cast<std::size_t>(d.bone)];
      } else {
        throw std::invalid_argument("Secondary frame lacks source");
      }
      n.position = math::translation(n.frame);
    }
  }
  for (std::size_t i = 0; i < definitions_.size(); ++i) {
    const auto& d = definitions_.at(i);
    auto& n = pending_.at(i);
    if (d.kind == SecondaryNodeKind::midpoint) {
      const auto& parent = pending_.at(static_cast<std::size_t>(d.parent));
      n.frame = parent.frame;
      n.position =
          scale(add(parent.position,
                    pending_.at(static_cast<std::size_t>(d.child)).position),
                kHalf);
      math::set_translation(n.frame, n.position);
    }
  }
  for (std::size_t i = 0; i < definitions_.size(); ++i) {
    const auto& d = definitions_.at(i);
    auto& n = pending_.at(i);
    if (d.kind == SecondaryNodeKind::adjacent_midpoint) {
      n.frame = pending_.at(static_cast<std::size_t>(d.parent)).frame;
      n.position = scale(
          add(pending_.at(i - 1).position, pending_.at(i + 1).position), kHalf);
      math::set_translation(n.frame, n.position);
    }
  }
  for (std::size_t i = 0; i < definitions_.size(); ++i) {
    const auto& d = definitions_.at(i);
    auto& n = pending_.at(i);
    const auto inv = math::inverse_affine(n.frame);
    if (!inv) throw std::invalid_argument("Singular secondary rest frame");
    if (d.child >= 0) {
      n.rest_child = math::transform_point<float>(
          pending_.at(static_cast<std::size_t>(d.child)).position, *inv);
      n.length = length(n.rest_child);
      if (n.length <= 0) throw std::invalid_argument("Zero secondary segment");
    }
    if (d.neighbor_a >= 0) {
      n.neighbor_length = length(math::transform_point<float>(
          pending_.at(static_cast<std::size_t>(d.neighbor_a)).position, *inv));
      if (n.neighbor_length <= 0) {
        throw std::invalid_argument("Zero secondary neighbor distance");
      }
    }
  }
  nodes_.swap(pending_);
  initialized_ = true;
  publish();
}
void SecondaryMotion::publish() {
  std::size_t output = 0;
  for (std::size_t i = 0; i < nodes_.size(); ++i) {
    positions_.at(i) = nodes_.at(i).position;
    if (definitions_.at(i).bone >= 0) {
      overrides_.at(output++).world = nodes_.at(i).frame;
    }
  }
}
// Advance one anchored chain in original table order. The source stores each
// link's velocity on its parent node and its world frame on the child node.
void SecondaryMotion::solve_chain(std::size_t root, const ChainUpdate& update,
                                  const V& wind, float movement_response,
                                  std::span<const Matrix> controls,
                                  bool first_update) {
  const auto [dt, ground] = update;
  auto& first = pending_.at(root);
  auto frame =
      controls[static_cast<std::size_t>(definitions_.at(root).control_point)];
  auto shift = subtract(math::translation(frame), first.position);
  if (first_update) {
    first.velocity = {};
  } else {
    constexpr float kHorizontalResponse = 0.3F, kVerticalResponse = 0.45F;
    shift[0] *= 1 - movement_response * kHorizontalResponse;
    shift[1] *= 1 - movement_response * kVerticalResponse;
    shift[2] *= 1 - movement_response * kHorizontalResponse;
  }
  first.frame = frame;
  first.position = math::translation(frame);
  const auto wind_direction = math::normalize_or(wind, {1, 0, 0});
  const float inverse_time =
      std::abs(dt) >= kMinTime ? 1 / dt : kZeroTimeReciprocal;
  float cumulative_length = 0;
  for (auto current = root; definitions_.at(current).child >= 0;) {
    auto& parent = pending_.at(current);
    const auto& definition = definitions_.at(current);
    const auto child_index = static_cast<std::size_t>(definition.child);
    auto& child = pending_.at(child_index);
    const auto segment_length = parent.length;
    cumulative_length += segment_length;
    child.position = add(child.position, shift);
    const auto segment = subtract(child.position, parent.position);
    parent.segment_force = scale(divide(segment, segment_length), kRestPull);
    const float alignment =
        length(segment) > 0
            ? 1 - std::abs(
                      dot(math::normalize_or(segment, kDown), wind_direction))
            : 1;
    const auto rest_world =
        math::transform_direction<float>(parent.rest_child, frame);
    auto correction =
        divide(subtract(rest_world, add(segment, scale(parent.velocity, dt))),
               cumulative_length);
    if (length(correction) > 1) {
      correction = math::normalize_or(correction, kDown);
    }
    correction = scale(correction, kCorrection);
    V neighbor{};
    if (definition.neighbor_a >= 0) {
      neighbor = add(
          neighbor, pending_.at(static_cast<std::size_t>(definition.neighbor_a))
                        .neighbor_force);
    }
    if (definition.neighbor_b >= 0) {
      neighbor = subtract(
          neighbor, pending_.at(static_cast<std::size_t>(definition.neighbor_b))
                        .neighbor_force);
    }
    auto velocity =
        add(add(add(scale(wind, segment_length * kWindResponse * alignment),
                    child.segment_force),
                parent.velocity),
            add(correction, neighbor));
    velocity[1] -= kGravity;
    auto direction = add(segment, scale(velocity, dt));
    direction[1] = std::max(direction[1], ground - parent.position[1]);
    auto target =
        add(parent.position,
            scale(math::normalize_or(direction, kDown), segment_length));
    if (project_capsules(target, definition.collider_mask, capsules_)) {
      direction = subtract(target, parent.position);
    }
    // Restrict the local direction to the original cone about negative Y.
    const auto parent_inverse = math::inverse_affine(parent.frame);
    if (!parent_inverse) {
      throw std::invalid_argument("Singular secondary parent frame");
    }
    auto local_direction = math::normalize_or(
        math::transform_direction<float>(direction, *parent_inverse), kDown);
    if (-local_direction[1] <
        static_cast<float>(std::cos(static_cast<double>(kConeAngle)))) {
      local_direction[1] =
          -std::sqrt(local_direction[0] * local_direction[0] +
                     local_direction[2] * local_direction[2]) /
          static_cast<float>(std::tan(static_cast<double>(kConeAngle)));
      local_direction = math::normalize_or(local_direction, kDown);
      direction = math::transform_direction<float>(local_direction, frame);
    }
    const auto up = math::normalize_or(scale(local_direction, -1.0F), kUp);
    const auto right = math::normalize_or(cross(up, kForward), {1, 0, 0});
    const auto forward = cross(right, up);
    auto rotation = kIdentityMatrix;
    std::ranges::copy(right, rotation[0].begin());
    std::ranges::copy(up, rotation[1].begin());
    std::ranges::copy(forward, rotation[2].begin());
    frame = multiply(rotation, frame);
    const auto endpoint =
        add(parent.position,
            scale(math::normalize_or(direction, kDown), segment_length));
    parent.velocity = add(scale(parent.velocity, kDamping * kHalf),
                          scale(subtract(endpoint, child.position),
                                inverse_time * kDamping * kHalf));
    child.position = endpoint;
    math::set_translation(frame, endpoint);
    child.frame = frame;
    if (definition.neighbor_a >= 0) {
      const auto neighbor_child =
          definitions_.at(static_cast<std::size_t>(definition.neighbor_a))
              .child;
      if (neighbor_child >= 0) {
        const auto separation = subtract(
            pending_.at(static_cast<std::size_t>(neighbor_child)).position,
            child.position);
        const auto distance = length(separation);
        parent.neighbor_force =
            distance > 0
                ? scale(separation, ((distance - child.neighbor_length) /
                                     (child.neighbor_length * distance)) *
                                        definition.neighbor_stiffness)
                : V{};
      }
    }
    current = child_index;
  }
}
void SecondaryMotion::advance(float dt, float elapsed, float ground,
                              std::span<const Matrix> controls,
                              std::span<const Matrix> bones,
                              bool first_update) {
  validate_inputs(controls, bones);
  if (!initialized_ || !std::isfinite(dt) || dt < 0 ||
      !std::isfinite(elapsed) || elapsed < 0 || !std::isfinite(ground)) {
    throw std::invalid_argument("Invalid secondary simulation clock/state");
  }
  for (std::size_t i = 0; i < capsules_.size(); ++i) {
    const auto& definition = capsule_definitions_.at(i);
    auto& capsule = capsules_.at(i);
    capsule.origin =
        math::transform_point<float>(definition.origin, bones[definition.bone]);
    capsule.axis =
        math::normalize_or(math::transform_direction<float>(
                               definition.axis, bones[definition.bone]),
                           kUp);
    capsule.half_length = definition.half_length;
    capsule.radius = definition.radius;
  }
  // Preserve authored oscillation rather than a generic wind/cloth tuning.
  constexpr float kWindPhase = 0.39269909262657166F, kWindAmplitude = 4,
                  kWindPower = 1.3F;
  constexpr float kMovementAmplitude = 0.7F;
  const float strength =
      (harmonic<12>(elapsed * kWindPhase) + 1) * kHalf * kWindAmplitude;
  const auto wind = scale(math::normalize_or(V{-0.7F, 0, 0.3F}, {1, 0, 0}),
                          strength * strength * kWindPower);
  const float response =
      ((harmonic<4>(elapsed * (2 * std::numbers::pi_v<float>)) * kHalf) +
       kHalf) *
          kMovementAmplitude +
      kHalf;
  std::ranges::copy(nodes_, pending_.begin());
  for (std::size_t root = 0; root < definitions_.size(); ++root) {
    const auto& definition = definitions_.at(root);
    if (definition.parent < 0 && definition.child >= 0) {
      solve_chain(root, {dt, ground}, wind, response, controls, first_update);
    }
  }
  for (const auto& node : pending_) {
    if (!math::all_finite(node.frame) || !math::all_finite(node.velocity) ||
        !math::all_finite(node.neighbor_force)) {
      throw std::overflow_error("Secondary state exceeds finite range");
    }
  }
  nodes_.swap(pending_);
  publish();
}
}  // namespace bw::animation
