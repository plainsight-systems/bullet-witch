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

#pragma once
#include <filesystem>

#include "bw/formats/common.hpp"
namespace bw::platform {
class Files {
 public:
  Files() = default;
  virtual ~Files() = default;
  Files(const Files&) = delete;
  Files& operator=(const Files&) = delete;
  Files(Files&&) = delete;
  Files& operator=(Files&&) = delete;
  virtual resources::Bytes read(const std::filesystem::path& path) const = 0;
  virtual std::filesystem::path canonical(
      const std::filesystem::path& path) const = 0;
};
class HostFiles final : public Files {
 public:
  resources::Bytes read(const std::filesystem::path& path) const override;
  std::filesystem::path canonical(
      const std::filesystem::path& path) const override;
};
}  // namespace bw::platform
