// Native runtime memory injection: explicit constructor passes its retained
// Domain and recovered root64KiB capacity to Context; legacy constructor
// remains for unprovisioned tools/tests. No allocator fallback on native
// exhaustion. Design R.1 (#rr-raii), SL.C.1 (#rclib-jmp); implementation F.6
// (#rf-noexcept) keeps Lua callback failure inside Context's C boundary.
// Performance MEM.6
// (guidelines/memory/MEM.6-inject-allocators-behind-one-interface.md) injects
// at this owner boundary. See lua-memory-implementation.md for mode/capacity
// evidence. Front-end handoff extension: original Lua submits a mission name
// and waits on the receiving game service. No synthetic mission completion is
// returned. Design MCP I.30 (#ri-encapsulate) keeps compatibility arguments at
// the bridge; implementation E.6 (#re-raii) retains owned VM cleanup on handler
// failure. cpp-performance COPY.4
// (guidelines/copy-move/COPY.4-rule-of-five-rule-of-zero.md) preserves existing
// owner semantics; the mission name is borrowed only during start_mission and a
// retaining receiver must copy it. Existing services without a mission receiver
// fail explicitly. All calls stay on the session thread. G1/G2: original root
// package -> init -> repeated main, with 19 startup bindings delegated to
// explicit game services. This is not a screen renderer. Resources/services
// must outlive this nonmovable driver. Tag must be exclusive to this driver;
// destructor releases it. Single-threaded and nonreentrant. Algorithm: request
// logical DPK, advance resource phases, copy/decode package, load original root
// bytecode, pump init, then main until pending/error/budget. _WaitTitle
// preserves its initial pending return; completed waits resume in the same
// pump. Unknown Lua/native calls fail visibly. Costs O(package bytes) setup
// plus Lua/handler work. Optimization: retain one VM across main calls.
// Global-value side effects belong to game services, never a fabricated array.
// MCP design/implementation: I.30 (#ri-encapsulate) isolates the Lua/native
// boundary; SL.con.3 (#rsl-bounds) validates argument counts/types. Performance
// COPY.5 (guidelines/copy-move/COPY.5-resource-owning-types-are-move-only.md)
// motivates unique context ownership; this callback owner has fixed identity.

#pragma once
#include "bw/resources/service.hpp"
#include "bw/script/context.hpp"
namespace bw::game {
enum class Screen : std::uint8_t {
  company_logo,
  memory_card,
  advertise,
  title,
  character_load,
  title_menu
};
class StartupServices {
 public:
  StartupServices() = default;
  virtual ~StartupServices() = default;
  StartupServices(const StartupServices&) = delete;
  StartupServices& operator=(const StartupServices&) = delete;
  StartupServices(StartupServices&&) = delete;
  StartupServices& operator=(StartupServices&&) = delete;
  virtual double global(int slot) const = 0;
  virtual void set_global(int slot, double value) = 0;
  virtual void execute(Screen screen) = 0;
  virtual bool pending(Screen screen) = 0;
  virtual double status(Screen screen) const = 0;
  virtual double latest_stage() const = 0;
  virtual void start_mission(std::string_view mission);
  virtual bool mission_pending();
};
class RootDriver {
 public:
  RootDriver(resources::ResourceService& resources, StartupServices& services,
             resources::ResourceTag tag);
  RootDriver(resources::ResourceService& resources, StartupServices& services,
             resources::ResourceTag tag,
             const std::shared_ptr<memory::Domain>& domain,
             std::size_t heap_bytes = script::kRootHeapBytes);
  ~RootDriver();
  RootDriver(const RootDriver&) = delete;
  RootDriver& operator=(const RootDriver&) = delete;
  RootDriver(RootDriver&&) = delete;
  RootDriver& operator=(RootDriver&&) = delete;
  script::RunResult advance();
  memory::HeapSnapshot memory_snapshot() const {
    return context_.memory_snapshot();
  }
  script::Value global(const std::string& name) const {
    return context_.global(name);
  }

 private:
  void bindings();
  std::reference_wrapper<resources::ResourceService> resources_;
  std::reference_wrapper<StartupServices> services_;
  resources::ResourceTag tag_;
  std::shared_ptr<memory::Domain> memory_;
  script::Context context_;
  bool loaded_{};
  script::RunResult result_{script::RunState::idle, {}};
};
}  // namespace bw::game
