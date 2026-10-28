// Original mission navigation archive admission. Decode indexed patrol paths
// and search graphs once into mission-domain storage; actors borrow immutable
// spans and retain their own progress. The archive owns all returned values;
// input bytes need only survive construction. Invalid ranges, duplicate path
// IDs, nonfinite positions or graph endpoints fail admission. Empty input is
// the original missing optional archive, not a synthesized graph.
// Algorithm: validate arcs directory -> follow group refs (including padding)
// -> decode ptrl/patr paths or scrh/pint/pass vertices and links -> publish.
// O(bytes + records + squared path counts) for duplicate-ID validation;
// setup allocations only; lookup never allocates. Core
// I.30 (#ri-encapsulate) keeps native offsets out of actor APIs; performance
// MEM.9 (guidelines/memory/MEM.9-embedded-allocate-at-init.md) confines decoded
// storage to admission. Source contracts: first-combat-physics-boundary.md.
#pragma once
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

#include "bw/math/vector.hpp"
#include "bw/memory/allocator.hpp"

namespace bw::formats {
using NavigationPoint = math::Vector3<float>;
struct NavigationVertex {
  NavigationPoint position{};
  std::uint32_t authored_word{};
};
struct NavigationEdge {
  std::uint32_t first{}, second{}, authored_word{}, reserved{};
};
struct NavigationGroup {
  enum class Kind : std::uint8_t { patrol, search };
  Kind kind{};
  std::size_t first{}, count{}, first_edge{}, edge_count{};
};
class NavigationArchive {
 public:
  NavigationArchive(std::span<const std::uint8_t>,
                    const std::shared_ptr<memory::Domain>&);
  std::span<const NavigationGroup> groups() const { return groups_; }
  std::optional<std::size_t> find_group(std::string_view name) const;
  std::span<const NavigationPoint> patrol(std::size_t group,
                                          std::int32_t path) const;
  std::span<const NavigationVertex> vertices(std::size_t group) const;
  std::span<const NavigationEdge> edges(std::size_t group) const;
  std::span<const std::uint32_t> neighbors(std::size_t group,
                                           std::size_t vertex) const;

 private:
  struct Path {
    std::int32_t id{};
    std::size_t first{}, count{};
  };
  memory::Vector<NavigationGroup> groups_;
  memory::Vector<char> names_;
  memory::Vector<std::size_t> name_offsets_;
  memory::Vector<Path> paths_;
  memory::Vector<NavigationPoint> points_;
  memory::Vector<NavigationVertex> vertices_;
  memory::Vector<NavigationEdge> edges_;
  struct Adjacency {
    std::size_t first{}, count{};
  };
  memory::Vector<Adjacency> adjacency_;
  memory::Vector<std::uint32_t> neighbors_;
};
}  // namespace bw::formats
