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

#include "bw/resources/catalog.hpp"
namespace bw::resources {
std::string normalize_name(std::string_view name) {
  std::string result(name);
  for (char& ch : result) {
    if (ch >= 'A' && ch <= 'Z') {
      ch = static_cast<char>(ch + ('a' - 'A'));
    }
  }
  return result;
}
memory::String normalize_name(std::string_view name,
                              const memory::Allocator<char>& allocator) {
  memory::String result(name, allocator);
  for (char& ch : result) {
    if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch + ('a' - 'A'));
  }
  return result;
}
ResourceCatalog::ResourceCatalog(ResourceCatalogData data)
    : data_(std::move(data)), index_(data_.allocator()) {
  for (std::size_t i = 0; i < data_.resources().size(); ++i) {
    index_.emplace(data_.resources()[i].full_name, i);
  }
}
std::optional<std::size_t> ResourceCatalog::find(std::string_view name) const {
  auto it = index_.find(normalize_name(name, data_.allocator()));
  return it == index_.end() ? std::nullopt : std::optional(it->second);
}
void CatalogSet::mount(int priority, std::filesystem::path path,
                       ResourceCatalog catalog) {
  if (domain() && catalog.domain() != domain()) {
    throw std::invalid_argument(
        "Catalog allocation domain does not match mount owner");
  }
  catalogs_.push_back({priority, std::move(path), std::move(catalog)});
}
Resolution CatalogSet::resolve(std::string_view name) const {
  std::optional<Resolution> best;
  int priority = 0;
  bool ambiguous = false;
  for (std::size_t i = 0; i < catalogs_.size(); ++i) {
    auto resource = catalogs_[i].catalog.find(name);
    if (!resource) {
      continue;
    }
    if (!best || catalogs_[i].priority < priority) {
      best = Resolution{i, *resource};
      priority = catalogs_[i].priority;
      ambiguous = false;
    } else if (catalogs_[i].priority == priority) {
      ambiguous = true;
    }
  }
  if (!best) {
    throw Error(ErrorCode::not_found,
                "Resource not found: " + std::string(name));
  }
  if (ambiguous) {
    throw Error(ErrorCode::ambiguous,
                "Equal-priority catalog matches: " + std::string(name));
  }
  return *best;
}
}  // namespace bw::resources
