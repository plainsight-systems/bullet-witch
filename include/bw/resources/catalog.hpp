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
// A3: indexed catalog ownership and priority resolution. ResourceCatalog owns
// its immutable decoded ResourceCatalogData; mounts own catalogs. Views borrow
// from stable owners; mutation requires exclusive access.
// Missing/equal-priority resolution throws Error. Algorithm: build index,
// mount, choose smallest priority key. Costs: O(N log N) construction, O(M log
// N) lookup, O(N) index strings. Optimization: reuse decoded full names and
// build the lookup index once. Design: cpp-guidelines I.1 (#ri-explicit), C.122
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
#include <filesystem>
#include <map>

#include "bw/formats/resource_catalog.hpp"
namespace bw::resources {
class ResourceCatalog {
 public:
  explicit ResourceCatalog(ResourceCatalogData data);
  static ResourceCatalog parse(Bytes bytes) {
    return ResourceCatalog(ResourceCatalogData::parse(std::move(bytes)));
  }
  static ResourceCatalog parse(CatalogBytes bytes) {
    return ResourceCatalog(ResourceCatalogData::parse(std::move(bytes)));
  }
  auto domain() const { return data_.allocator().domain(); }
  const auto& sections() const { return data_.sections(); }
  const auto& files() const { return data_.files(); }
  const auto& resources() const { return data_.resources(); }
  const auto& namespaces() const { return data_.namespaces(); }
  auto bytes() const { return data_.bytes(); }
  std::optional<std::size_t> find(std::string_view name) const;

 private:
  ResourceCatalogData data_;
  std::map<memory::String, std::size_t, std::less<>,
           memory::Allocator<std::pair<const memory::String, std::size_t>>>
      index_;
};
std::string normalize_name(std::string_view name);
memory::String normalize_name(std::string_view name,
                              const memory::Allocator<char>& allocator);
struct MountedCatalog {
  int priority;
  std::filesystem::path path;
  ResourceCatalog catalog;
};
struct Resolution {
  std::size_t catalog_index, resource_index;
};
class CatalogSet {
 public:
  CatalogSet() = default;  // Legacy tool construction.
  explicit CatalogSet(std::shared_ptr<memory::Domain> domain)
      : catalogs_(memory::Allocator<MountedCatalog>(std::move(domain))) {}
  auto domain() const { return catalogs_.get_allocator().domain(); }
  void mount(int priority, std::filesystem::path path, ResourceCatalog catalog);
  Resolution resolve(std::string_view name) const;
  const auto& catalogs() const { return catalogs_; }

 private:
  memory::Vector<MountedCatalog> catalogs_;
};
}  // namespace bw::resources
