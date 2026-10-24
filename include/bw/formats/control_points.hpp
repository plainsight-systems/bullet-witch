// Immutable authored weighted control-point frames. Parse validates the source
// directory, three-point records and every influence before returning owned
// metadata; no file or pose ownership is borrowed. Missing slots stay explicit.
// Unsupported layouts and malformed numbers/ranges throw resources::Error.
// Algorithm: bound the directory, decode present frame points and flattened
// influences, validate counts and finite weights; animation evaluates them.
// O(slots + influences) construction/storage, no native platform dependency.
// Both phases: Core I.1 (#ri-explicit), SL.con.3 (#rsl-bounds) expose and bound
// records; performance MEM.9 (guidelines/memory/
// MEM.9-embedded-allocate-at-init.md) prepares immutable runtime metadata.
#pragma once
#include "bw/formats/common.hpp"
#include "bw/math/vector.hpp"
#include "bw/memory/allocator.hpp"
namespace bw::formats {
struct ControlPointInfluence {
  std::size_t bone{};
  float weight{};
};
struct ControlPointFrame {
  std::array<math::Vector3<float>, 3> points{};
  std::size_t first_influence{}, influence_count{};
  bool present{};
};
class ControlPointDefinition {
 public:
  static ControlPointDefinition parse(
      std::span<const std::uint8_t> bytes, std::size_t bones,
      const std::shared_ptr<memory::Domain>& domain);
  const auto& frames() const { return frames_; }
  std::span<const ControlPointInfluence> influences(
      const ControlPointFrame& frame) const;

 private:
  explicit ControlPointDefinition(const std::shared_ptr<memory::Domain>& domain)
      : frames_(memory::Allocator<ControlPointFrame>(domain)),
        influences_(memory::Allocator<ControlPointInfluence>(domain)) {}
  memory::Vector<ControlPointFrame> frames_;
  memory::Vector<ControlPointInfluence> influences_;
};
}  // namespace bw::formats
