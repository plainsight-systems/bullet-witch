// Decode authored background placement independently of simulation/rendering.
// Borrow immutable package bytes for names; their owner must outlive this view.
// Own validated placement/matrix metadata in the supplied domain. Exclusive
// construction; immutable reads afterward. Malformed ranges/nonfinite
// transforms throw resources::Error, unknown versions remain unsupported.
// Algorithm: bound header and directories, resolve names, decode versioned
// prefixes, validate and expand each source row-vector 4x3 matrix. Costs
// O(names + placements + matrices) with metadata allocated only during load. No
// per-frame allocation or copying. Both MCP phases: cpp-guidelines C.32
// (#rc-dtor-ptr) distinguishes borrowed names from owned metadata;
// cpp-performance MEM.9 (guidelines/memory/MEM.9-embedded-allocate-at-init.md)
// bounds allocation to load. Source and scene ownership:
// stage-scene-presentation.md.
#pragma once
#include "bw/memory/allocator.hpp"
#include "common.hpp"
namespace bw::formats {
using PlacementMatrix = std::array<std::array<float, 4>, 4>;
struct BackgroundPlacement {
  std::size_t model{}, first_matrix{}, matrix_count{};
  std::int16_t group{-1};
  // Original body/part/group state words, not floating-point vectors.
  // Primary word2 contains disabled mesh groups; zero means initially visible.
  std::array<std::uint32_t, 4> primary_parameters{}, secondary_parameters{};
};
class BackgroundPlacementDefinition {
 public:
  static BackgroundPlacementDefinition parse(
      std::span<const std::uint8_t> bytes,
      const std::shared_ptr<memory::Domain>& domain);
  const auto& names() const { return names_; }
  const auto& placements() const { return placements_; }
  const auto& matrices() const { return matrices_; }

 private:
  explicit BackgroundPlacementDefinition(
      const std::shared_ptr<memory::Domain>& domain)
      : names_(memory::Allocator<std::string_view>(domain)),
        placements_(memory::Allocator<BackgroundPlacement>(domain)),
        matrices_(memory::Allocator<PlacementMatrix>(domain)) {}
  memory::Vector<std::string_view> names_;
  memory::Vector<BackgroundPlacement> placements_;
  memory::Vector<PlacementMatrix> matrices_;
};
}  // namespace bw::formats
