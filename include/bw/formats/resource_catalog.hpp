// Catalog memory migration: encoded bytes, decoded nested strings/vectors,
// traversal scratch and indexes retain the injected allocator owner. Native
// ingress moves its buffer; legacy Bytes conversion copies. Copies retain the
// source domain. Failure unwinds owned storage without allocation fallback.
// R.20 (#rr-owner) preserves ownership; SL.con.3 (#rsl-bounds) preserves
// checked traversal. MEM.6
// (guidelines/memory/MEM.6-inject-allocators-behind-one-interface.md) injects
// allocation; MEM.8 (guidelines/memory/MEM.8-pmr-allocator-aware-containers.md)
// requires explicit nested propagation and owner lifetime. Design and
// implementation queried separately. See resource-memory-migration.md.
// A1: decoded resource catalog owner (RAT). parse owns bytes and validates
// bounds, namespace membership and unique full names. Throws Error; borrowed
// metadata lasts until owner mutation/move/destruction. Full names are
// constructed only here. Algorithm: validate sections, walk namespaces, resolve
// dependency names. Cost: O(bytes + metadata), tree checks O(N log N).
// Optimization: one decode. Design: cpp-guidelines I.1 (#ri-explicit), C.122
// (#rh-separation): explicit ownership and injected file capability.
// cpp-performance COPY.2
// (guidelines/copy-move/COPY.2-mark-move-constructors-noexcept.md): container
// owners retain implicit moves. Implementation: SL.con.3 (#rsl-bounds) checks
// slices before access; COPY.4 (guidelines/copy-move/
// COPY.4-rule-of-five-rule-of-zero.md) keeps owning values rule-of-zero.

// Naming design/implementation consultation: cpp-guidelines P.3 (#rp-what)
// selects role-based names. cpp-performance COPY.4 (guidelines/copy-move/
// COPY.4-rule-of-five-rule-of-zero.md) preserves owning values and implicit
// moves; renaming adds no wrapper, allocation or payload copy.

#pragma once
#include "bw/formats/common.hpp"
#include "bw/memory/allocator.hpp"
namespace bw::resources {
using CatalogBytes = memory::Vector<std::uint8_t>;
struct CatalogFile {
  explicit CatalogFile(const memory::Allocator<char>& allocator = {})
      : directory(allocator), filename(allocator), relative_path(allocator) {}
  memory::String directory, filename, relative_path;
};
struct Dependency {
  Dependency(std::optional<std::uint32_t> index, std::string_view value,
             const memory::Allocator<char>& allocator)
      : local_index(index), name(value, allocator) {}
  std::optional<std::uint32_t> local_index;
  memory::String name;  // Even local-index dependencies are requested through
                        // global name lookup.
};
struct CatalogResource {
  explicit CatalogResource(const memory::Allocator<char>& allocator = {})
      : name(allocator), full_name(allocator), dependencies(allocator) {}
  std::array<std::uint32_t, kCatalogResourceWordCount> raw{};
  memory::String name, full_name;
  std::optional<memory::String> extra;
  memory::Vector<Dependency> dependencies;
};
struct NamespaceNode {
  explicit NamespaceNode(const memory::Allocator<char>& allocator = {})
      : name(allocator), children(allocator), resources(allocator) {}
  std::uint32_t offset{}, parent{};
  memory::String name;
  memory::Vector<std::uint32_t> children, resources;
};
class ResourceCatalogData {
 public:
  static ResourceCatalogData parse(Bytes bytes);
  static ResourceCatalogData parse(CatalogBytes bytes);
  auto allocator() const { return bytes_.get_allocator(); }
  const auto& sections() const { return sections_; }
  const auto& files() const { return files_; }
  const auto& resources() const { return resources_; }
  const auto& namespaces() const { return namespaces_; }
  std::span<const std::uint8_t> bytes() const { return bytes_; }

 private:
  explicit ResourceCatalogData(const memory::Allocator<std::uint8_t>& allocator)
      : bytes_(allocator),
        files_(allocator),
        resources_(allocator),
        namespaces_(allocator) {}
  CatalogBytes bytes_;
  std::array<Range, kSectionCount> sections_{};
  memory::Vector<CatalogFile> files_;
  memory::Vector<CatalogResource> resources_;
  memory::Vector<NamespaceNode> namespaces_;
};
}  // namespace bw::resources
