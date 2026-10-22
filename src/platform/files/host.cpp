// P1: read-only file capability and host adapter. Results own bytes/paths;
// implementations throw io Error on failure. No borrowed buffers or mutations.
// Algorithm: establish size, allocate, perform complete read; canonical
// resolves an absolute location. Cost O(file bytes). Optimization: read into
// final owner. Canonical containment is a point-in-time check, not a race-free
// sandbox. Design: cpp-guidelines I.1 (#ri-explicit), C.122 (#rh-separation):
// explicit ownership and injected file capability. cpp-performance COPY.2
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
Bytes HostFiles::read(const std::filesystem::path& path) const {
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
  Bytes result(static_cast<std::size_t>(end));
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
std::filesystem::path HostFiles::canonical(
    const std::filesystem::path& path) const {
  std::error_code error;
  auto result = std::filesystem::weakly_canonical(
      std::filesystem::absolute(path, error), error);
  if (error) {
    throw Error(ErrorCode::io, error.message());
  }
  return result;
}
}  // namespace bw::platform
