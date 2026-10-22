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

#pragma once
#include <functional>
#include <memory>
#include <set>

#include "bw/content/access.hpp"
namespace bw::resources {
using ResourceTag = std::uint32_t;
enum class LoadState : std::uint8_t {
  unrequested,
  requested,
  opened,
  published,
  ready,
  failed
};
class ResourceLease {
 public:
  ResourceLease(std::shared_ptr<const CprArchive> archive, std::size_t record);
  std::span<const std::uint8_t> bytes() const;

 private:
  std::shared_ptr<const CprArchive> archive_;
  std::size_t record_;
};
struct LoadReport {
  LoadState state;
  std::string diagnostic;
};
class ResourceService {
 public:
  ResourceService(CatalogSet catalogs, const platform::Files& files)
      : catalogs_(std::move(catalogs)), files_(files) {}
  void request(std::string_view name, ResourceTag tag);
  LoadReport pump(ResourceTag tag);
  LoadReport status(std::string_view name) const;
  std::optional<ResourceLease> acquire(std::string_view name) const;
  void release(ResourceTag tag);

 private:
  using Key = std::pair<std::size_t, std::size_t>;
  struct File {
    std::set<ResourceTag> tags;
    LoadState state{LoadState::requested};
    std::shared_ptr<const CprArchive> archive;
    std::string diagnostic;
  };
  struct Publication {
    Key file;
    std::size_t record;
  };
  Key file_key(Resolution resolution) const;
  LoadReport completion(ResourceTag tag) const;
  void publish(const Key& key, File& file);
  std::map<ResourceTag, std::set<std::string>> requests_;
  CatalogSet catalogs_;
  std::reference_wrapper<const platform::Files> files_;
  std::map<Key, File> loaded_;
  std::map<std::string, Publication, std::less<>> publications_;
};
}  // namespace bw::resources
