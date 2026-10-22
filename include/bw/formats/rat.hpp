// A1: pure RAT metadata owner. parse owns bytes and validates bounds, namespace
// membership and unique full names. Throws Error; borrowed metadata lasts until
// owner mutation/move/destruction. Full names are constructed only here.
// Algorithm: validate sections, walk namespaces, resolve dependency names.
// Cost: O(bytes + metadata), tree checks O(N log N). Optimization: one decode.
// Design: cpp-guidelines I.1 (#ri-explicit), C.122 (#rh-separation):
// explicit ownership and injected file capability. cpp-performance COPY.2
// (guidelines/copy-move/COPY.2-mark-move-constructors-noexcept.md): container
// owners retain implicit moves. Implementation: SL.con.3 (#rsl-bounds) checks
// slices before access; COPY.4 (guidelines/copy-move/
// COPY.4-rule-of-five-rule-of-zero.md) keeps owning values rule-of-zero.

#pragma once
#include "bw/formats/common.hpp"
namespace bw::resources {
struct CatalogFile {
  std::string directory, filename, relative_path;
};
struct Dependency {
  std::optional<std::uint32_t> local_index;
  std::string name;  // Even local-index dependencies are requested through
                     // global name lookup.
};
struct CatalogResource {
  std::array<std::uint32_t, kRatResourceWordCount> raw{};
  std::string name, full_name;
  std::optional<std::string> extra;
  std::vector<Dependency> dependencies;
};
struct NamespaceNode {
  std::uint32_t offset{}, parent{};
  std::string name;
  std::vector<std::uint32_t> children, resources;
};
class RatData {
 public:
  static RatData parse(Bytes bytes);
  const auto& sections() const { return sections_; }
  const auto& files() const { return files_; }
  const auto& resources() const { return resources_; }
  const auto& namespaces() const { return namespaces_; }
  std::span<const std::uint8_t> bytes() const { return bytes_; }

 private:
  Bytes bytes_;
  std::array<Range, kSectionCount> sections_{};
  std::vector<CatalogFile> files_;
  std::vector<CatalogResource> resources_;
  std::vector<NamespaceNode> namespaces_;
};
}  // namespace bw::resources
