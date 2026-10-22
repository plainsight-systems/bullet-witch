// A3: startup USER-resource lifecycle, docs/startup-runtime.md.
// Single-threaded; the Files capability must outlive this service. Owns
// catalogs, requested-file interests and publications. Leases retain immutable
// CPR owners after release. Errors are terminal per file until all tags release
// it; lookup never reports failed/unsupported work ready. Request resolves the
// dependency closure before changing interests; allocation failure may leave
// valid partial interests. Algorithm: mark closure (cycle-safe), open all
// tagged files, publish USER in phase 1, finish phase 2; release interests and
// identity-matching publications. Each pump advances one global phase; it never
// spins on failed/pending work. Cost: closure O((R+D) log R), pumps O(files +
// records), retained archive bytes. Optimization: leases share archives;
// publication lookup does not copy payloads. Design: cpp-guidelines R.20
// (#rr-owner) makes archive retention explicit; cpp-performance COPY.4
// (guidelines/copy-move/COPY.4-rule-of-five-rule-of-zero.md) keeps container
// owners rule-of-zero. Implementation: SL.con.3 (#rsl-bounds) bounds every
// decoded slice; COPY.4 avoids accidental owning-container copies.

// Naming design/implementation consultation: cpp-guidelines P.3 (#rp-what)
// selects role-based names. cpp-performance COPY.4 (guidelines/copy-move/
// COPY.4-rule-of-five-rule-of-zero.md) preserves owning values and implicit
// moves; renaming adds no wrapper, allocation or payload copy.

#include "bw/resources/service.hpp"

#include <algorithm>
#include <limits>
namespace bw::resources {
ResourceLease::ResourceLease(std::shared_ptr<const ResourceArchive> archive,
                             std::size_t record)
    : archive_(std::move(archive)), record_(record) {
  if (!archive_) {
    throw Error(ErrorCode::malformed, "Null resource lease");
  }
  (void)archive_->payload(record_);
}
std::span<const std::uint8_t> ResourceLease::bytes() const {
  return archive_->payload(record_);
}
ResourceService::Key ResourceService::file_key(Resolution r) const {
  constexpr std::size_t file_field = 3;
  return {r.catalog_index, catalogs_.catalogs()
                               .at(r.catalog_index)
                               .catalog.resources()
                               .at(r.resource_index)
                               .raw[file_field]};
}
void ResourceService::request(std::string_view name, ResourceTag tag) {
  std::set<std::string> seen;
  std::set<Key> closure;
  std::vector<std::string> pending{std::string(name)};
  while (!pending.empty()) {
    auto next = normalize_name(pending.back());
    pending.pop_back();
    if (!seen.insert(next).second) {
      continue;
    }
    const auto r = catalogs_.resolve(next);
    closure.insert(file_key(r));
    for (const auto& dep : catalogs_.catalogs()
                               .at(r.catalog_index)
                               .catalog.resources()
                               .at(r.resource_index)
                               .dependencies) {
      pending.push_back(dep.name);
    }
  }
  requests_[tag].insert(seen.begin(), seen.end());
  for (const auto& key : closure) {
    loaded_[key].tags.insert(tag);
  }
}
void ResourceService::publish(const Key& key, File& file) {
  // Validate the entire supported archive before publishing any record.
  std::map<std::string, Publication, std::less<>> additions;
  for (std::size_t i = 0; i < file.archive->records().size(); ++i) {
    const auto& record = file.archive->records()[i];
    if (record.type != "USER" ||
        (record.publication_name && !record.first_payload) ||
        record.raw[2] != std::numeric_limits<std::uint32_t>::max()) {
      throw Error(ErrorCode::unsupported,
                  "Runtime record/inline reference unsupported: " +
                      record.type + ":" + record.name);
    }
    if (!record.publication_name) {
      continue;
    }
    const auto name = normalize_name(*record.publication_name);
    const auto selected = file_key(catalogs_.resolve(name));
    // A lower-precedence catalog may contain the same publication. It must not
    // replace the selected owner's data or remove it when unloaded.
    if (selected != key) {
      continue;
    }
    if (!additions.emplace(name, Publication{key, i}).second) {
      throw Error(ErrorCode::ambiguous, "Duplicate USER publication: " + name);
    }
  }
  publications_.merge(additions);
}
LoadReport ResourceService::pump(ResourceTag tag) {
  auto phase = LoadState::ready;
  bool found = false;
  for (const auto& [key, file] : loaded_) {
    (void)key;
    if (!file.tags.contains(tag)) {
      continue;
    }
    found = true;
    if (file.state == LoadState::failed) {
      return {file.state, file.diagnostic};
    }
    phase = std::min(phase, file.state);
  }
  if (!found) {
    return {LoadState::unrequested, {}};
  }
  if (phase == LoadState::ready) {
    return completion(tag);
  }
  for (auto& [key, file] : loaded_) {
    if (!file.tags.contains(tag) || file.state != phase) {
      continue;
    }
    try {
      if (phase == LoadState::requested) {
        const auto& mount = catalogs_.catalogs().at(key.first);
        file.archive = std::make_shared<ResourceArchive>(content::open_archive(
            files_.get(),
            content::archive_path(files_.get(), mount, key.second)));
        file.state = LoadState::opened;
      } else if (phase == LoadState::opened) {
        publish(key, file);
        file.state = LoadState::published;
      } else if (phase == LoadState::published) {
        // USER has no phase-2 callback. Other handlers are explicitly rejected.
        file.state = LoadState::ready;
      }
    } catch (const Error& error) {
      file.state = LoadState::failed;
      file.diagnostic = error.what();
    }
  }
  for (const auto& [key, file] : loaded_) {
    (void)key;
    if (file.tags.contains(tag) && file.state == LoadState::failed) {
      return {file.state, file.diagnostic};
    }
  }
  if (phase == LoadState::published) {
    return completion(tag);
  }
  return {phase == LoadState::requested ? LoadState::opened
          : phase == LoadState::opened  ? LoadState::published
                                        : LoadState::ready,
          {}};
}
LoadReport ResourceService::completion(ResourceTag tag) const {
  for (const auto& name : requests_.at(tag)) {
    auto result = status(name);
    if (result.state != LoadState::ready) {
      return result;
    }
  }
  return {LoadState::ready, {}};
}
LoadReport ResourceService::status(std::string_view name) const {
  const auto key = file_key(catalogs_.resolve(name));
  auto found = loaded_.find(key);
  if (found == loaded_.end()) {
    return {LoadState::unrequested, {}};
  }
  if (found->second.state == LoadState::ready &&
      !publications_.contains(normalize_name(name))) {
    return {LoadState::failed, "Archive did not publish requested name"};
  }
  return {found->second.state, found->second.diagnostic};
}
std::optional<ResourceLease> ResourceService::acquire(
    std::string_view name) const {
  auto found = publications_.find(normalize_name(name));
  if (found == publications_.end()) {
    return std::nullopt;
  }
  const auto& file = loaded_.at(found->second.file);
  if (file.state == LoadState::failed) {
    return std::nullopt;
  }
  return ResourceLease(file.archive, found->second.record);
}
void ResourceService::release(ResourceTag tag) {
  requests_.erase(tag);
  for (auto it = loaded_.begin(); it != loaded_.end();) {
    it->second.tags.erase(tag);
    if (!it->second.tags.empty()) {
      ++it;
      continue;
    }
    std::erase_if(publications_, [&](const auto& entry) {
      return entry.second.file == it->first;
    });
    it = loaded_.erase(it);
  }
}
}  // namespace bw::resources
