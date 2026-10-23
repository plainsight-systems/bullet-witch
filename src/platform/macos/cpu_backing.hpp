// Explicit macOS CPU backing selection for target composition. The returned
// shared provider must survive its allocations; domains retain that ownership.
// Factory bootstrap uses the system C++ heap and may throw before any domain
// exists. The provider itself acquires aligned, uninitialized native storage
// and individually releases it, with no page/residency or GPU contract.
// One bootstrap allocation; per-call cost is delegated to native libmalloc.
// Design MCP: R.1 (#rr-raii) retains ownership; performance MEM.6
// (guidelines/memory/MEM.6-inject-allocators-behind-one-interface.md) injects
// target backing. Implementation MCP: E.6 (#re-raii) and MEM.6 preserve paired
// lifetime without recursively allocating the provider through its own domain.
// Reference/design: research/engine/subsystems/memory-macos-backing.md.
#pragma once

#include <memory>

#include "bw/memory/cpu_backing.hpp"

namespace bw::platform::macos {
[[nodiscard]] std::shared_ptr<memory::CpuBacking> make_cpu_backing();
}  // namespace bw::platform::macos
