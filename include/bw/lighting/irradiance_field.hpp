// Immutable prepared SHLT spatial irradiance. Construction validates original
// relative offsets, dictionary indices, finite geometry/coefficient data and
// cell extents, then owns decoded zones/samples in the supplied domain. Input
// bytes need not outlive construction. Unknown versions/malformed data reject.
// Sampling consumes a finite world point. Preserve source zone order, sphere
// admission, oriented expanded boxes, first four overlaps, cell-center weights,
// exclusion volumes and default remainder. Return nine RGB coefficients in
// source world-normal basis; packing preserves the original seven-vector GPU
// polynomial. No inferred directional light or stage ambient substitution.
// Strict sampling instead selects the first containing oriented zone without
// overlap expansion/blending, then averages a five-point X/Z neighborhood at
// one Y cell. Its central weight is two; valid neighbor weights are one. This
// source path does not apply the actor sampler's exclusion volumes.
// Boundary points fall through to the next zone or authored default. Policies
// are explicit (Core I.1, #ri-explicit); CDSA.32 shares one prepared field
// across both consumers rather than decoding another prop-specific
// representation. Prepare O(samples + zones + exclusions), storing 108 bytes
// per decoded sample plus bounded zone metadata. Sample O(zones +
// four*eight*exclusions), fixed scratch/no allocation; const queries are
// concurrent-safe. Domain ownership releases all prepared storage together. See
// character-light-sampling.md. Both phases: Core SL.con.3 (#rsl-bounds)
// validates source spans and indices; performance CDSA.32 prepares consumer
// data once and MEM.9 confines allocation to setup
// (guidelines/memory/MEM.9-embedded-allocate-at-init.md).
#pragma once
#include <span>

#include "bw/math/matrix.hpp"
#include "bw/memory/allocator.hpp"

namespace bw::scene {
inline constexpr std::size_t kIrradianceCoefficientCount = 9;
inline constexpr std::size_t kPackedIrradianceCount = 7;
using IrradianceCoefficients =
    std::array<math::Vector3<float>, kIrradianceCoefficientCount>;
using PackedIrradiance =
    std::array<math::Vector<float, 4>, kPackedIrradianceCount>;
enum class IrradianceSampling : std::uint8_t { blended, first_containing };
PackedIrradiance pack_irradiance(const IrradianceCoefficients& coefficients);
class IrradianceField {
 public:
  IrradianceField(std::span<const std::uint8_t> bytes,
                  const std::shared_ptr<memory::Domain>& domain);
  IrradianceCoefficients sample(
      const math::Vector3<float>& world_position, float owner_intensity = 1,
      IrradianceSampling policy = IrradianceSampling::blended) const;
  // Finite owner_intensity is the mutable source owner's multiplier, separate
  // from the immutable file intensity. Multiply both before RGB coefficients;
  // signed values are retained. Default is the original pre-activation value.
  // I.1/MEM.9 (design and implementation): explicit input, no query allocation.
  std::size_t zone_count() const { return zones_.size(); }
  std::size_t sample_count() const { return samples_.size(); }

 private:
  struct Box {
    math::Vector3<float> center{}, sphere_center{}, extent{};
    float radius_squared{};
    math::Matrix4<float> to_local{}, to_world{};
  };
  struct Zone {
    Box bounds, grid;
    std::array<std::uint32_t, 3> divisions{};
    std::size_t first_sample{}, first_exclusion{}, exclusions{};
  };
  IrradianceCoefficients sample_zone(
      const Zone& zone, const math::Vector3<float>& position) const;
  IrradianceCoefficients sample_strict_zone(
      const Zone& zone, const math::Vector3<float>& position) const;
  memory::Vector<Zone> zones_;
  memory::Vector<Box> exclusions_;
  memory::Vector<IrradianceCoefficients> samples_;
  IrradianceCoefficients fallback_{};
  float intensity_{1};
};
}  // namespace bw::scene
