// A2: compose file acquisition/decoding and catalog-relative content location.
// Files capability is borrowed for each call; outputs own data. Errors
// propagate. Algorithm: canonicalize base/candidate, reject escapes; read then
// parse. Costs: O(path components) location, reader costs for open.
// Optimization: move acquired bytes into decoder. Symlink checks do not prevent
// concurrent races. Design: cpp-guidelines I.1 (#ri-explicit), C.122
// (#rh-separation): explicit ownership and injected file capability.
// cpp-performance COPY.2
// (guidelines/copy-move/COPY.2-mark-move-constructors-noexcept.md): container
// owners retain implicit moves. Implementation: SL.con.3 (#rsl-bounds) checks
// slices before access; COPY.4 (guidelines/copy-move/
// COPY.4-rule-of-five-rule-of-zero.md) keeps owning values rule-of-zero.

#pragma once
#include "bw/formats/cpr.hpp"
#include "bw/platform/files.hpp"
#include "bw/resources/catalog.hpp"
namespace bw::content {
resources::CprArchive open_cpr(const platform::Files& files,
                               const std::filesystem::path& path);
resources::RatCatalog open_rat(const platform::Files& files,
                               const std::filesystem::path& path);
std::filesystem::path archive_path(const platform::Files& files,
                                   const resources::MountedCatalog& catalog,
                                   std::size_t index);
}  // namespace bw::content
