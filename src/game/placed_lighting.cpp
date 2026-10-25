// Prepare independent emitter and receiver observations from borrowed placed
// model data. The emitter resolves its authored joint; the receiver evaluates
// control-point slot zero through mapped world matrices. Both return owned
// values without allocation or mutation. Shared math owns point transforms.
// Both-phase I.1/SL.con.3 and MEM.9 contracts are declared in
// placed_lighting.hpp.
#include "placed_lighting.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <stdexcept>

#include "bw/math/matrix.hpp"
#include "formats/reader.hpp"

namespace bw::game {
// Resolve emitter pose through its authored joint while keeping the original
// outer-object fallback distinct from the receiver control-point observation.
std::optional<scene::LocalLight> prepare_placed_light(
    const formats::PhysicsCollisionDefinition& definition,
    std::span<const formats::PlacementMatrix> body_world,
    const formats::PlacementMatrix* object_world, std::size_t joint_count) {
  if (!definition.local_light()) return std::nullopt;
  if (body_world.size() != definition.body_joints().size()) {
    throw std::invalid_argument("Incomplete placed light body palette");
  }
  const auto& source = *definition.local_light();
  const formats::PlacementMatrix* transform = object_world;
  bool found = false;
  if (source.joint >= 0 &&
      static_cast<std::size_t>(source.joint) < joint_count) {
    for (std::size_t body = 0; body < body_world.size(); ++body) {
      if (definition.body_joints().at(body) != source.joint) continue;
      if (found) throw std::invalid_argument("Ambiguous placed light joint");
      transform = &body_world[body];
      found = true;
    }
  }
  if (source.joint >= 0 &&
      static_cast<std::size_t>(source.joint) < joint_count && !found) {
    throw std::invalid_argument(
        "Placed light joint has no body-world publication");
  }
  // Original joint lookup falls back to the owning object's complete transform.
  if (transform == nullptr) {
    throw std::invalid_argument(
        "Placed light needs an explicit owner transform");
  }
  for (const auto& row : *transform) {
    for (const auto value : row) {
      if (!std::isfinite(value)) {
        throw std::invalid_argument("Nonfinite placed light transform");
      }
    }
  }
  return scene::LocalLight{
      {(*transform)[3][0], (*transform)[3][1], (*transform)[3][2]},
      source.diffuse,
      source.specular,
      source.attenuation};
}

namespace {
using resources::detail::malformed;
using resources::detail::Reader;
using resources::detail::unsupported;
constexpr std::uint64_t kWord = 4, kHeader = 12, kPointCount = 3;
float finite_number(const Reader& reader, std::uint64_t offset) {
  const float value = std::bit_cast<float>(reader.u32(offset));
  if (!std::isfinite(value)) malformed("Nonfinite placed control-point value");
  return value;
}
math::Vector3<float> checked_translation(
    const formats::PlacementMatrix& matrix) {
  const auto result = math::translation(matrix);
  if (!math::all_finite(result)) {
    malformed("Nonfinite placed owner translation");
  }
  return result;
}
}  // namespace
math::Vector3<float> placed_receiver_position(
    const ResidentModelView& model,
    std::span<const formats::PlacementMatrix> matrices) {
  if (matrices.empty() || matrices.size() != model.body_joints.size()) {
    malformed("Placed control-point body association differs");
  }
  // The outer owner retains the first placement even when that body's joint
  // is not zero. Missing control-point data returns this source fallback.
  const auto fallback = checked_translation(matrices.front());
  if (model.control_points.empty()) return fallback;
  const Reader reader{model.control_points};
  reader.range(0, kHeader);
  constexpr std::uint32_t kControlMagic = 0x4250435f;
  if (reader.u32(0) != kControlMagic) {
    unsupported("Unsupported placed control-point encoding");
  }
  const auto slots = reader.u32(2 * kWord);
  if (slots == 0) return fallback;
  reader.range(kHeader, static_cast<std::uint64_t>(slots) * kWord);
  const std::uint64_t record = reader.u32(kHeader);
  if (record == 0) return fallback;
  if (record < kHeader + static_cast<std::uint64_t>(slots) * kWord) {
    malformed("Placed control-point overlaps directory");
  }
  reader.range(record, 2 * kWord);
  if (reader.u32(record) != kPointCount) {
    unsupported("Placed control point requires three points");
  }
  const std::uint64_t count = reader.u32(record + kWord);
  const std::uint64_t point = record + 2 * kWord;
  const std::uint64_t indices = point + kPointCount * 3 * kWord;
  reader.range(point, kPointCount * 3 * kWord + count * 2 * kWord);
  const math::Vector3<float> origin{finite_number(reader, point),
                                    finite_number(reader, point + kWord),
                                    finite_number(reader, point + 2 * kWord)};
  if (count == 0) return origin;
  math::Vector3<float> result{};
  for (std::uint64_t index = 0; index < count; ++index) {
    const auto joint =
        std::bit_cast<std::int32_t>(reader.u32(indices + index * kWord));
    if (joint == -1) return fallback;
    if (joint < 0) malformed("Invalid placed control-point joint");
    const auto body = std::ranges::find(model.body_joints, joint);
    if (body == model.body_joints.end()) {
      unsupported("Placed control point requires an unprepared joint pose");
    }
    const auto& matrix =
        matrices[static_cast<std::size_t>(body - model.body_joints.begin())];
    if (!std::ranges::all_of(
            matrix, [](const auto& row) { return math::all_finite(row); })) {
      malformed("Nonfinite placed control-point matrix");
    }
    // Direct-world mode has already removed the model transform. Preserve the
    // authored weights and apply each mapped body matrix exactly once.
    const float weight =
        finite_number(reader, indices + (count + index) * kWord);
    result = math::add(
        result,
        math::scale(math::transform_point_basis_first(origin, matrix), weight));
  }
  if (!math::all_finite(result)) {
    malformed("Unrepresentable placed control-point origin");
  }
  return result;
}
}  // namespace bw::game
