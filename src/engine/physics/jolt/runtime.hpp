// Private Jolt registration owner. Public engine capabilities never expose it.
// Process-global upstream allocator hooks are installed once per live Runtime;
// all worlds retain that owner, whose domain survives all upstream frees.
// No competing registration, global heap fallback or hidden worker pool.
// Core R.1 (#rr-raii) pairs registration; performance MEM.6 routes storage to
// the supplied domain. See first-combat-physics-boundary.md for pinned sources.
#pragma once
#include <Jolt/Jolt.h>

// Jolt requires its foundation header before any component header.
#include <Jolt/Core/Factory.h>

#include <memory>
#include <optional>

#include "bw/memory/domain.hpp"

namespace bw::physics::detail {
class Runtime {
 public:
  explicit Runtime(std::shared_ptr<memory::Domain> domain);
  ~Runtime();
  Runtime(const Runtime&) = delete;
  Runtime& operator=(const Runtime&) = delete;
  Runtime(Runtime&&) = delete;
  Runtime& operator=(Runtime&&) = delete;
  const std::shared_ptr<memory::Domain>& domain() const noexcept {
    return domain_;
  }

 private:
  std::shared_ptr<memory::Domain> domain_;
  std::optional<JPH::Factory> factory_;
};
}  // namespace bw::physics::detail
