// Memory adoption: payload/metadata and nested strings retain an owning domain
// allocator; legacy parser defaults remain compatibility allocations. Copies
// preserve allocator ownership and leases may outlive their service. Allocation
// failures unwind without fallback. R.20 (#rr-owner), F.27 (#rf-shared_ptr),
// E.17 (#re-not-always) govern ownership/unwinding; MEM.6
// (guidelines/memory/MEM.6-inject-allocators-behind-one-interface.md) routes
// actual storage; MEM.8
// (guidelines/memory/MEM.8-pmr-allocator-aware-containers.md) motivates
// retaining the resource instead of borrowing a PMR pointer. Both design and
// implementation consulted. See resource-memory-migration.md. P1: read-only
// file capability and host adapter. Results own bytes/paths; implementations
// throw io Error on failure. No borrowed buffers or mutations. Algorithm:
// establish size, allocate, perform complete read; canonical resolves an
// absolute location. Cost O(file bytes). Optimization: read into final owner.
// Canonical containment is a point-in-time check, not a race-free sandbox.
// Design: cpp-guidelines I.1 (#ri-explicit), C.122 (#rh-separation): explicit
// ownership and injected file capability. cpp-performance COPY.2
// (guidelines/copy-move/COPY.2-mark-move-constructors-noexcept.md): container
// owners retain implicit moves. Implementation: SL.con.3 (#rsl-bounds) checks
// slices before access; COPY.4 (guidelines/copy-move/
// COPY.4-rule-of-five-rule-of-zero.md) keeps owning values rule-of-zero.

#include <fstream>
#include <limits>

#include "bw/platform/files.hpp"
namespace bw::platform {
using resources::Bytes;
using resources::Error;
using resources::ErrorCode;
namespace {
template <class Buffer>
Buffer read_buffer(const std::filesystem::path& path,
                   const typename Buffer::allocator_type& allocator) {
  std::ifstream input(path, std::ios::binary | std::ios::ate);
  if (!input) {
    throw Error(ErrorCode::io, "Cannot open " + path.string());
  }
  const auto end = input.tellg();
  if (end < 0 ||
      static_cast<std::uint64_t>(end) >
          std::numeric_limits<std::size_t>::max() ||
      static_cast<std::uint64_t>(end) >
          static_cast<std::uint64_t>(
              std::numeric_limits<std::streamsize>::max())) {
    throw Error(ErrorCode::io, "Invalid file size: " + path.string());
  }
  Buffer result(static_cast<std::size_t>(end), allocator);
  input.seekg(0);
  // Character access may alias any object representation; the read below is
  // bounded by the already-sized vector. No typed-object/alignment cast occurs.
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
  auto* destination = reinterpret_cast<char*>(result.data());
  if (!result.empty() &&
      !input.read(destination, static_cast<std::streamsize>(result.size()))) {
    throw Error(ErrorCode::io, "Incomplete file read: " + path.string());
  }
  return result;
}
}  // namespace
Bytes HostFiles::read(const std::filesystem::path& path) const {
  return read_buffer<Bytes>(path, {});
}
resources::ArchiveBytes HostFiles::read(
    const std::filesystem::path& path,
    std::shared_ptr<memory::Domain> domain) const {
  return read_buffer<resources::ArchiveBytes>(
      path, memory::Allocator<std::uint8_t>(std::move(domain)));
}
std::filesystem::path HostFiles::canonical(
    const std::filesystem::path& path) const {
  std::error_code error;
  const auto absolute = std::filesystem::absolute(path, error);
  if (error) {
    throw Error(ErrorCode::io, error.message());
  }
  auto result = std::filesystem::weakly_canonical(absolute, error);
  if (error) {
    throw Error(ErrorCode::io, error.message());
  }
  return result;
}
}  // namespace bw::platform
