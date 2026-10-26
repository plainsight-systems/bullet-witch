// Decode original spatial-light records once, then preserve their ordered
// sampling and packing. Header owns the source/cost contract. Implementation
// SL.con.3 validates every source offset/index before admission; CDSA.32/MEM.9
// use owned decoded coefficients and fixed per-query scratch, not byte parsing.
// Redesign consultations I.1 (#ri-explicit), CDSA.32 and MEM.9 preserve
// explicit strict/blended policies over the same immutable prepared field.
// Source strict selection stops at its first containing zone; it never averages
// zones.
#include "bw/lighting/irradiance_field.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <stdexcept>

#include "bw/content/formats/binary_reader.hpp"
#include "bw/math/euler.hpp"
namespace bw::scene {
namespace {
using Vector = math::Vector3<float>;
using Reader = resources::detail::Reader;
constexpr std::size_t kCoefficientCount = 9, kChannels = 3, kIndexBytes = 54;
constexpr float kBlendExtent = 1, kBlendDiameter = 2, kBlendVolume = 8;
constexpr std::size_t kMaximumZones = 4;
constexpr std::size_t kCornerCount = 8;
constexpr std::size_t kVectorBytes = 16, kTwoVectors = 32, kThreeVectors = 48;
constexpr std::size_t kGridHeaderBytes = 64, kIntensityOffset = 24;
constexpr std::size_t kDictionaryOffset = 20, kTableOffset = 12;
constexpr std::size_t kCountOffset = 8;
float scalar(const Reader& reader, std::uint64_t offset) {
  const auto value = std::bit_cast<float>(reader.u32(offset));
  if (!std::isfinite(value)) {
    resources::detail::malformed("Nonfinite light field scalar");
  }
  return value;
}
Vector vector(const Reader& reader, std::uint64_t offset) {
  return {scalar(reader, offset), scalar(reader, offset + 4),
          scalar(reader, offset + 2 * sizeof(float))};
}
Vector point(const Vector& p, const math::Matrix4<float>& matrix) {
  return math::transform_point<float>(p, math::affine_part(matrix));
}
bool inside(const Vector& p, const Vector& extent, float margin = 0) {
  for (std::size_t axis = 0; axis < 3; ++axis) {
    if (!(p.at(axis) > -extent.at(axis) - margin &&
          p.at(axis) < extent.at(axis) + margin)) {
      return false;
    }
  }
  return true;
}
void accumulate(IrradianceCoefficients& result,
                const IrradianceCoefficients& input, float weight) {
  for (std::size_t coefficient = 0; coefficient < kCoefficientCount;
       ++coefficient) {
    for (std::size_t channel = 0; channel < kChannels; ++channel) {
      result.at(coefficient).at(channel) +=
          input.at(coefficient).at(channel) * weight;
    }
  }
}
struct AxisSamples {
  std::array<std::uint32_t, 2> index{};
  std::array<float, 2> weight{1, 0}, position{};
};
struct AxisQuery {
  float position, extent;
  std::uint32_t divisions;
};
AxisSamples axis_samples(AxisQuery query) {
  const auto [position, extent, divisions] = query;
  AxisSamples result;
  if (divisions == 0) return result;
  const float count = static_cast<float>(divisions), half_cell = extent / count;
  constexpr float kHalf = .5F;
  const float coordinate =
      (position * count * (1.F / extent) + count + 1.F) * kHalf;
  constexpr float kSourceIntegerLimit = 2147483520.F;
  if (!std::isfinite(coordinate) ||
      std::abs(coordinate) > kSourceIntegerLimit) {
    throw std::overflow_error("Light grid query exceeds source integer range");
  }
  // Original converts toward zero, then decrements every negative value.
  auto lower = static_cast<std::int64_t>(coordinate);
  if (coordinate < 0) --lower;
  const float physical = coordinate * half_cell * kBlendDiameter;
  auto upper = lower;
  if ((static_cast<float>(lower) + kHalf) * half_cell * kBlendDiameter <=
      physical) {
    ++upper;
  } else {
    --lower;
  }
  const float boundary = static_cast<float>(upper) * half_cell * kBlendDiameter;
  result.weight = {boundary - (physical - half_cell),
                   (physical + half_cell) - boundary};
  for (std::size_t side = 0; side < 2; ++side) {
    const auto index = std::clamp(side == 0 ? lower : upper, std::int64_t{0},
                                  static_cast<std::int64_t>(divisions));
    result.index.at(side) = static_cast<std::uint32_t>(index);
    result.position.at(side) =
        static_cast<float>(index) * half_cell * kBlendDiameter +
        (half_cell - extent);
  }
  return result;
}
}  // namespace
IrradianceField::IrradianceField(std::span<const std::uint8_t> bytes,
                                 const std::shared_ptr<memory::Domain>& domain)
    : zones_(memory::Allocator<Zone>(domain)),
      exclusions_(memory::Allocator<Box>(domain)),
      samples_(memory::Allocator<IrradianceCoefficients>(domain)) {
  const Reader reader{bytes};
  constexpr std::uint32_t kSignature = 0x544c4853;
  if (reader.u32(0) != kSignature || reader.u32(4) != 1) {
    resources::detail::unsupported("Unsupported spatial light field");
  }
  const auto zone_count = reader.u32(kCountOffset),
             table = reader.u32(kTableOffset),
             fallback = reader.u32(kVectorBytes),
             dictionary = reader.u32(kDictionaryOffset);
  intensity_ = scalar(reader, kIntensityOffset);
  reader.range(table, static_cast<std::uint64_t>(zone_count) * 4);
  const auto coefficients = [&](std::uint64_t offset) {
    reader.range(offset, kIndexBytes);
    IrradianceCoefficients result{};
    for (std::size_t i = 0; i < kCoefficientCount; ++i) {
      for (std::size_t c = 0; c < kChannels; ++c) {
        result.at(i).at(c) = scalar(
            reader, static_cast<std::uint64_t>(dictionary) +
                        4ULL * reader.u16(offset + 2 * (i * kChannels + c)));
      }
    }
    return result;
  };
  const auto box = [&](std::uint64_t offset, bool sphere) {
    Box result;
    result.extent = vector(reader, offset + (sphere ? kVectorBytes : 0));
    result.center =
        vector(reader, offset + (sphere ? kThreeVectors : kTwoVectors));
    const auto rotation =
        vector(reader, offset + (sphere ? kTwoVectors : kVectorBytes));
    const auto transforms = math::euler_xyz_transform(
        math::EulerPlacement<float>{rotation, result.center});
    result.to_local = transforms.inverse;
    result.to_world = transforms.forward;
    if (sphere) {
      const float radius = scalar(reader, offset + 3 * sizeof(float));
      result.radius_squared = radius * radius;
      result.sphere_center = vector(reader, offset);
      if (radius < 0 || !std::isfinite(result.radius_squared)) {
        resources::detail::malformed("Invalid light sphere radius");
      }
    }
    for (const auto value : result.extent) {
      if (value < 0) {
        resources::detail::malformed("Negative light volume extent");
      }
    }
    return result;
  };
  if (fallback != 0) {
    fallback_ =
        coefficients(static_cast<std::uint64_t>(fallback) + kGridHeaderBytes);
  }
  zones_.reserve(zone_count);
  for (std::size_t i = 0; i < zone_count; ++i) {
    const auto record = reader.u32(static_cast<std::uint64_t>(table) + 4 * i);
    const auto grid = reader.u32(record),
               bounds = reader.u32(static_cast<std::uint64_t>(record) + 4);
    Zone zone;
    zone.bounds = box(bounds, true);
    zone.grid = box(grid, false);
    std::uint64_t count = 1;
    for (std::size_t axis = 0; axis < 3; ++axis) {
      const auto divisions = reader.u32(static_cast<std::uint64_t>(grid) +
                                        kThreeVectors + 4 * axis);
      if (divisions > bytes.size() / kIndexBytes ||
          (divisions != 0 && !(zone.grid.extent.at(axis) > 0))) {
        resources::detail::malformed("Invalid light grid dimensions");
      }
      zone.divisions.at(axis) = divisions;
      const auto width = static_cast<std::uint64_t>(divisions) + 1;
      if (count > bytes.size() / kIndexBytes / width) {
        resources::detail::malformed("Light grid exceeds input");
      }
      count *= width;
    }
    reader.range(static_cast<std::uint64_t>(grid) + kGridHeaderBytes,
                 count * kIndexBytes);
    zone.first_sample = samples_.size();
    for (std::uint64_t sample = 0; sample < count; ++sample) {
      samples_.push_back(coefficients(static_cast<std::uint64_t>(grid) +
                                      kGridHeaderBytes + sample * kIndexBytes));
    }
    zone.exclusions =
        reader.u32(static_cast<std::uint64_t>(record) + kCountOffset);
    zone.first_exclusion = exclusions_.size();
    const auto exclusions =
        reader.u32(static_cast<std::uint64_t>(record) + kTableOffset);
    reader.range(exclusions, static_cast<std::uint64_t>(zone.exclusions) * 4);
    for (std::size_t exclusion = 0; exclusion < zone.exclusions; ++exclusion) {
      exclusions_.push_back(box(
          reader.u32(static_cast<std::uint64_t>(exclusions) + 4 * exclusion),
          true));
    }
    zones_.push_back(zone);
  }
}
IrradianceCoefficients IrradianceField::sample_zone(
    const Zone& zone, const Vector& position) const {
  const auto local = point(position, zone.grid.to_local);
  std::array<AxisSamples, 3> axes;
  for (std::size_t axis = 0; axis < 3; ++axis) {
    axes.at(axis) = axis_samples(
        {local.at(axis), zone.grid.extent.at(axis), zone.divisions.at(axis)});
  }
  std::array<float, kCornerCount> weights{};
  std::array<std::size_t, kCornerCount> indices{};
  float total = 0;
  for (std::size_t x = 0; x < 2; ++x) {
    for (std::size_t y = 0; y < 2; ++y) {
      for (std::size_t z = 0; z < 2; ++z) {
        const auto corner = (x * 2 + y) * 2 + z;
        const Vector sample_position{axes[0].position.at(x),
                                     axes[1].position.at(y),
                                     axes[2].position.at(z)};
        const auto world = point(sample_position, zone.grid.to_world);
        bool excluded = false;
        for (std::size_t e = 0; e < zone.exclusions; ++e) {
          const auto& exclusion = exclusions_.at(zone.first_exclusion + e);
          if (inside(point(world, exclusion.to_local), exclusion.extent)) {
            excluded = true;
            break;
          }
        }
        const float weight =
            axes[2].weight.at(z) * axes[1].weight.at(y) * axes[0].weight.at(x);
        if (excluded || !(weight > 0)) continue;
        weights.at(corner) = weight;
        total += weight;
        indices.at(corner) = zone.first_sample +
                             ((zone.divisions[1] + 1ULL) * axes[0].index.at(x) +
                              axes[1].index.at(y)) *
                                 (zone.divisions[2] + 1ULL) +
                             (zone.divisions[2] - axes[2].index.at(z));
      }
    }
  }
  IrradianceCoefficients result{};
  if (total > 0) {
    for (std::size_t corner = 0; corner < kCornerCount; ++corner) {
      if (weights.at(corner) > 0) {
        accumulate(result, samples_.at(indices.at(corner)),
                   weights.at(corner) / total);
      }
    }
  }
  return result;
}
// The strict receiver uses a different grid kernel from actor interpolation:
// nearest source cell, central weight two, then four horizontal neighbors.
// Original cell maxima exclude the terminal layer for neighbor admission, but
// the central cell is unconditional; zero-dimensional axes therefore remain
// meaningful. No exclusion-volume or trilinear weight is shared accidentally.
IrradianceCoefficients IrradianceField::sample_strict_zone(
    const Zone& zone, const Vector& position) const {
  const auto local = point(position, zone.grid.to_local);
  std::array<std::int64_t, 3> cell{};
  constexpr float kSourceIntegerLimit = 2147483520.F;
  for (std::size_t axis = 0; axis < cell.size(); ++axis) {
    const auto count = static_cast<std::int64_t>(zone.divisions.at(axis));
    const float extent = zone.grid.extent.at(axis);
    const float coordinate =
        extent > 0
            ? ((1.F / extent) * local.at(axis) * static_cast<float>(count) +
               static_cast<float>(count) + 1.F) *
                  .5F
            : 0;
    if (!std::isfinite(coordinate) ||
        std::abs(coordinate) > kSourceIntegerLimit) {
      throw std::overflow_error(
          "Strict light grid exceeds source integer range");
    }
    auto index = static_cast<std::int64_t>(coordinate);
    if (coordinate < 0) --index;
    index = std::max<std::int64_t>(index, 0);
    if (count > 0 && index >= count) index = count - 1;
    cell.at(axis) = index;
  }
  cell[2] = static_cast<std::int64_t>(zone.divisions[2]) - cell[2];
  constexpr std::array<std::array<int, 2>, 5> kNeighbors{
      {{0, 0}, {-1, 0}, {1, 0}, {0, -1}, {0, 1}}};
  IrradianceCoefficients result{};
  float total = 0;
  for (std::size_t index = 0; index < kNeighbors.size(); ++index) {
    const auto x = cell[0] + kNeighbors.at(index)[0];
    const auto z = cell[2] + kNeighbors.at(index)[1];
    if (index != 0 &&
        (x < 0 || x >= zone.divisions[0] || z < 0 || z >= zone.divisions[2])) {
      continue;
    }
    const auto sample =
        zone.first_sample +
        ((zone.divisions[1] + 1ULL) * static_cast<std::size_t>(x) +
         static_cast<std::size_t>(cell[1])) *
            (zone.divisions[2] + 1ULL) +
        static_cast<std::size_t>(z);
    const float weight = index == 0 ? 2.F : 1.F;
    accumulate(result, samples_.at(sample), weight);
    total += weight;
  }
  const float reciprocal = 1.F / total;
  for (auto& coefficient : result) {
    for (auto& channel : coefficient) channel *= reciprocal;
  }
  return result;
}
IrradianceCoefficients IrradianceField::sample(
    const Vector& position, float owner_intensity,
    IrradianceSampling policy) const {
  if (!math::all_finite(position) || !std::isfinite(owner_intensity)) {
    throw std::invalid_argument("Nonfinite light query");
  }
  // Placed-object receivers use strict bounds and the first matching source
  // zone, followed by its separate fixed-neighborhood grid evaluation.
  if (policy == IrradianceSampling::first_containing) {
    auto result = fallback_;
    for (const auto& zone : zones_) {
      const auto delta = math::subtract(position, zone.bounds.sphere_center);
      if (math::dot(delta, delta) > zone.bounds.radius_squared) continue;
      if (!inside(point(position, zone.bounds.to_local), zone.bounds.extent)) {
        continue;
      }
      result = sample_strict_zone(zone, position);
      break;
    }
    const float intensity = intensity_ * owner_intensity;
    for (auto& coefficient : result) {
      for (auto& channel : coefficient) channel *= intensity;
    }
    return result;
  }
  if (policy != IrradianceSampling::blended) {
    throw std::invalid_argument("Unknown irradiance sampling policy");
  }
  std::array<IrradianceCoefficients, kMaximumZones + 1> values{};
  std::array<float, kMaximumZones + 1> weights{};
  std::size_t count = 0;
  float total = 0;
  for (const auto& zone : zones_) {
    const auto delta = math::subtract(position, zone.bounds.sphere_center);
    if (math::dot(delta, delta) > zone.bounds.radius_squared) continue;
    const auto local = point(position, zone.bounds.to_local);
    if (!inside(local, zone.bounds.extent, kBlendExtent)) continue;
    values.at(count) = sample_zone(zone, position);
    Vector widths{};
    for (std::size_t axis = 0; axis < 3; ++axis) {
      widths.at(axis) = std::min(
          kBlendDiameter,
          local.at(axis) >= 0
              ? zone.bounds.extent.at(axis) - (local.at(axis) - kBlendExtent)
              : local.at(axis) + kBlendExtent + zone.bounds.extent.at(axis));
    }
    weights.at(count) = widths[1] * widths[0] * widths[2];
    total += weights.at(count);
    ++count;
    if (count == kMaximumZones) break;
  }
  const float remainder = kBlendVolume - total;
  if (remainder > 0) {
    values.at(count) = fallback_;
    weights.at(count) = remainder;
    total += remainder;
    ++count;
  }
  IrradianceCoefficients result{};
  if (count == 1) {
    result = values[0];
  } else if (total > 0) {
    for (std::size_t i = 0; i < count; ++i) {
      accumulate(result, values.at(i), weights.at(i) / total);
    }
  }
  // Source first combines file and live-owner intensity, then scales every RGB
  // coefficient. I.1/MEM.9 retain explicit input and allocation-free sampling.
  const float intensity = intensity_ * owner_intensity;
  for (auto& coefficient : result) {
    for (auto& channel : coefficient) channel *= intensity;
  }
  return result;
}
PackedIrradiance pack_irradiance(const IrradianceCoefficients& input) {
  constexpr float kConstant = .2820947766304016F, kLinear = -.3257349729537964F,
                  kZ = .3257349729537964F,
                  kConstantQuadratic = .07884789258241653F,
                  kXY = .27313709259033203F, kYZ = -.27313709259033203F,
                  kZZ = .2365436851978302F, kDifference = .13656854629516602F;
  constexpr std::size_t kYZCoefficient = 5, kZZCoefficient = 6,
                        kZXCoefficient = 7, kDifferenceCoefficient = 8;
  constexpr std::size_t kDifferenceVector = 6;
  PackedIrradiance result{};
  for (std::size_t c = 0; c < kChannels; ++c) {
    result.at(c) = {input[3].at(c) * kLinear, input[1].at(c) * kLinear,
                    input[2].at(c) * kZ,
                    input[0].at(c) * kConstant -
                        input[kZZCoefficient].at(c) * kConstantQuadratic};
    result.at(c + 3) = {input[4].at(c) * kXY, input[kYZCoefficient].at(c) * kYZ,
                        input[kZZCoefficient].at(c) * kZZ,
                        input[kZXCoefficient].at(c) * kYZ};
    result[kDifferenceVector].at(c) =
        input[kDifferenceCoefficient].at(c) * kDifference;
  }
  result[kDifferenceVector][3] = 1;
  return result;
}
}  // namespace bw::scene
