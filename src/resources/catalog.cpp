// A3: indexed catalog ownership and priority resolution. RatCatalog owns its
// immutable decoded RatData; mounts own catalogs. Views borrow from stable
// owners; mutation requires exclusive access. Missing/equal-priority resolution
// throws Error. Algorithm: build index, mount, choose smallest priority key.
// Costs: O(N log N) construction, O(M log N) lookup, O(N) index strings.
// Optimization: reuse decoded full names and build the lookup index once.
// Design: cpp-guidelines I.1 (#ri-explicit), C.122 (#rh-separation):
// explicit ownership and injected file capability. cpp-performance COPY.2
// (guidelines/copy-move/COPY.2-mark-move-constructors-noexcept.md): container
// owners retain implicit moves. Implementation: SL.con.3 (#rsl-bounds) checks
// slices before access; COPY.4 (guidelines/copy-move/
// COPY.4-rule-of-five-rule-of-zero.md) keeps owning values rule-of-zero.

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
RatCatalog::RatCatalog(RatData data) : data_(std::move(data)) {
  for (std::size_t i = 0; i < data_.resources().size(); ++i) {
    index_.emplace(data_.resources()[i].full_name, i);
  }
}
std::optional<std::size_t> RatCatalog::find(std::string_view name) const {
  auto it = index_.find(normalize_name(name));
  return it == index_.end() ? std::nullopt : std::optional(it->second);
}
void CatalogSet::mount(int priority, std::filesystem::path path,
                       RatCatalog catalog) {
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
