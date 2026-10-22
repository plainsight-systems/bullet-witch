// G1/G2: original root package -> init -> repeated main, with 17 startup
// bindings delegated to explicit game services. This is not a screen renderer.
// Resources/services must outlive this nonmovable driver. Tag must be exclusive
// to this driver; destructor releases it. Single-threaded and nonreentrant.
// Algorithm: request logical DPK, advance resource phases, copy/decode package,
// load original root bytecode, pump init, then main until pending/error/budget.
// _WaitTitle preserves its initial pending return; completed waits resume in
// the same pump. Unknown Lua/native calls fail visibly. Costs O(package bytes)
// setup plus Lua/handler work. Optimization: retain one VM across main calls.
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
};
class RootDriver {
 public:
  RootDriver(resources::ResourceService& resources, StartupServices& services,
             resources::ResourceTag tag);
  ~RootDriver();
  RootDriver(const RootDriver&) = delete;
  RootDriver& operator=(const RootDriver&) = delete;
  RootDriver(RootDriver&&) = delete;
  RootDriver& operator=(RootDriver&&) = delete;
  script::RunResult advance();
  script::Value global(const std::string& name) const {
    return context_.global(name);
  }

 private:
  void bindings();
  std::reference_wrapper<resources::ResourceService> resources_;
  std::reference_wrapper<StartupServices> services_;
  resources::ResourceTag tag_;
  script::Context context_;
  bool loaded_{};
  script::RunResult result_{script::RunState::idle, {}};
};
}  // namespace bw::game
