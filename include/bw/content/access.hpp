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
// Memory adoption: payload/metadata and nested strings retain an owning domain
// allocator; legacy parser defaults remain compatibility allocations. Copies
// preserve allocator ownership and leases may outlive their service. Allocation
// failures unwind without fallback. R.20 (#rr-owner), F.27 (#rf-shared_ptr),
// E.17 (#re-not-always) govern ownership/unwinding; MEM.6
// (guidelines/memory/MEM.6-inject-allocators-behind-one-interface.md) routes
// actual storage; MEM.8
// (guidelines/memory/MEM.8-pmr-allocator-aware-containers.md) motivates
// retaining the resource instead of borrowing a PMR pointer. Both design and
// implementation consulted. See resource-memory-migration.md. A2: compose file
// acquisition/decoding and catalog-relative content location. Files capability
// is borrowed for each call; outputs own data. Errors propagate. Algorithm:
// canonicalize base/candidate, reject escapes; read then parse. Costs: O(path
// components) location, reader costs for open. Optimization: move acquired
// bytes into decoder. Symlink checks do not prevent concurrent races. Design:
// cpp-guidelines I.1 (#ri-explicit), C.122
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
#include "bw/formats/resource_archive.hpp"
#include "bw/platform/files.hpp"
#include "bw/resources/catalog.hpp"
namespace bw::content {
resources::ResourceArchive open_archive(const platform::Files& files,
                                        const std::filesystem::path& path);
resources::ResourceArchive open_archive(const platform::Files& files,
                                        const std::filesystem::path& path,
                                        std::shared_ptr<memory::Domain> domain);
resources::ResourceCatalog open_catalog(const platform::Files& files,
                                        const std::filesystem::path& path);
resources::ResourceCatalog open_catalog(const platform::Files& files,
                                        const std::filesystem::path& path,
                                        std::shared_ptr<memory::Domain> domain);
std::filesystem::path archive_path(const platform::Files& files,
                                   const resources::MountedCatalog& catalog,
                                   std::size_t index);
}  // namespace bw::content
