// Memory extension: native Context owns a bounded reclaiming heap backed by its
// injected Domain. Lua state/global state/coroutines retain this context
// through destruction; close runs before heap release. Legacy default uses
// stock Lua. Wrapper impl/map keys/nodes/argument vector use Domain; public
// Value strings, Reply vectors and type-erased callback captures remain
// caller-owned boundary types. Copies across those APIs are not claimed
// allocator-complete. Native heap defaults to recovered root64KiB, with
// measured native occupancy exposed; no automatic growth. Constructor/load
// failures throw; pump failure is terminal. All allocating host Lua API
// operations use a protected C boundary, so Lua longjmp cannot bypass C++
// owners. Context stays single-threaded; independent contexts have independent
// callbacks, no global current allocator. Design R.1 (#rr-raii), SL.C.1
// (#rclib-jmp); performance EMB.4 bounded backing without claiming first-fit
// constant-time. Implementation F.6 (#rf-noexcept), MEM.6
// (guidelines/memory/MEM.6-inject-allocators-behind-one-interface.md) routes
// allocation through explicit context capability. See
// lua-memory-implementation.md. E2: owned Lua 5.0.2 context with
// immediate/waiting native bridges. Single thread, no reentrant calls. Binding
// callbacks own captures; borrowed services must outlive the context. Native
// argument spans last only through the call; a pending callback must copy any
// arguments it retains. Destroy cancels the retained poll and closes Lua.
// Moved-from contexts may only be destroyed/assigned. Script errors are
// terminal pump results; API misuse/load errors throw. No file/OS libraries are
// opened. Algorithm: load chunk, anchor coroutine, start named function,
// resume; on a bridge wait poll immediately, return while true, clear then
// resume when false. Cost follows executed Lua instructions; arguments/results
// copy strings once. Optimization: retained coroutine/poll avoids replaying
// completed script work. Safety budget: 100000 instructions per resume and 64
// bridge resumes per pump; these are host guards, not recovered game timing.
// Not an untrusted-code sandbox. Design/implementation MCP: I.30
// (#ri-encapsulate) confines C/runtime details; SL.con.3 (#rsl-bounds) checks
// byte/index access. cpp-performance COPY.5
// (guidelines/copy-move/COPY.5-resource-owning-types-are-move-only.md):
// unique runtime owners cannot be copied; moves preserve stable heap identity.

#pragma once
#include <functional>
#include <memory>
#include <variant>

#include "bw/formats/common.hpp"
#include "bw/memory/coalescing_heap.hpp"
namespace bw::script {
inline constexpr std::size_t kRootHeapBytes = std::size_t{64} * 1024;
using Value = std::variant<std::monostate, double, bool, std::string>;
struct Reply {
  std::vector<Value> values;
  std::function<bool()> pending;
};
using Native = std::function<Reply(std::span<const Value>)>;
enum class RunState : std::uint8_t { idle, running, pending, complete, failed };
struct RunResult {
  RunState state;
  std::string diagnostic;
};
class Context {
 public:
  Context();  // Legacy tool/test route; native composition supplies a domain.
  explicit Context(const std::shared_ptr<memory::Domain>& domain,
                   std::size_t heap_bytes = kRootHeapBytes);
  memory::HeapSnapshot memory_snapshot() const;
  ~Context();
  Context(const Context&) = delete;
  Context& operator=(const Context&) = delete;
  Context(Context&&) noexcept;
  Context& operator=(Context&&) noexcept;
  void bind(std::string name, Native native);
  void load_binary(std::span<const std::uint8_t> bytes);
  void load_source(std::string_view source);
  void start(const std::string& function);
  RunResult pump();
  Value global(const std::string& name) const;

 private:
  struct Impl;
  std::shared_ptr<Impl> impl_;
};
}  // namespace bw::script
