// V1: public runtime contracts, docs/startup-runtime.md. Failures throw and
// produce nonzero exit; fixtures own their state. Algorithm: drive lifecycle,
// resume waits and check terminal outcomes. Cost bounded by small
// scripts/tasks. Optimization: in-memory fixtures avoid unrelated host I/O. MCP
// design and implementation: I.30 (#ri-encapsulate), SL.con.3 (#rsl-bounds),
// COPY.5 (guidelines/copy-move/COPY.5-resource-owning-types-are-move-only.md)
// guide owned context lifetime, bounded fixtures and callback boundaries.
// Provisioned integration scenario: real read-only catalog/archive/package,
// original Lua, and scheduler/root composition. Screen/global services below
// are deterministic test fixtures, NOT reconstructed game behavior/rendering.
// Naming design/implementation consultation: cpp-guidelines P.3 (#rp-what)
// selects role-based names. cpp-performance COPY.4 (guidelines/copy-move/
// COPY.4-rule-of-five-rule-of-zero.md) preserves owning values and implicit
// moves; renaming adds no wrapper, allocation or payload copy.

#include <iostream>
#include <stdexcept>
#include <vector>

#include "bw/tasks/task_lifecycle_scheduler.hpp"
#include "game/root_driver.hpp"
namespace {
using bw::game::Screen;
class Screens final : public bw::game::StartupServices {
 public:
  std::vector<Screen> started;
  bool hold_menu{true};
  double global(int slot) const override {
    if (slot != 1) {
      throw std::runtime_error("Unexpected global read");
    }
    return 0;
  }
  void set_global(int, double) override {
    throw std::runtime_error("Unexpected global side effect");
  }
  void execute(Screen screen) override { started.push_back(screen); }
  bool pending(Screen screen) override {
    return screen == Screen::title_menu && hold_menu;
  }
  double status(Screen screen) const override {
    return screen == Screen::title ? 1 : 7;
  }
  double latest_stage() const override {
    throw std::runtime_error("Unexpected stage selection");
  }
};
}  // namespace
int main(int argc, char** argv) {
  try {
    if (argc != 2) {
      throw std::runtime_error("Usage: startup-corpus GAME_DIRECTORY");
    }
    // C guarantees argv[1] after the checked argument count.
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
    const std::filesystem::path game(argv[1]);
    const auto path = game / "data/data/stable/stable.rat";
    bw::platform::HostFiles files;
    bw::resources::CatalogSet catalogs;
    catalogs.mount(30, path, bw::content::open_catalog(files, path));
    bw::resources::ResourceService resources(std::move(catalogs), files);
    Screens screens;
    {
      bw::game::RootDriver root(resources, screens, 1);
      bw::tasks::TaskLifecycleScheduler scheduler;
      bw::script::RunResult result{bw::script::RunState::idle, {}};
      (void)scheduler.add(
          30, bw::tasks::Hooks{.update = [&] { result = root.advance(); }});
      for (int tick = 0; tick < 10; ++tick) {
        scheduler.tick();
        if (result.state == bw::script::RunState::failed) {
          throw std::runtime_error(result.diagnostic);
        }
        if (!screens.started.empty() &&
            screens.started.back() == Screen::title_menu) {
          break;
        }
      }
      const std::vector expected{Screen::company_logo,   Screen::memory_card,
                                 Screen::advertise,      Screen::title,
                                 Screen::character_load, Screen::title_menu};
      if (screens.started != expected ||
          result.state != bw::script::RunState::pending ||
          std::get<double>(root.global("mode")) != 2) {
        throw std::runtime_error("Original root title/menu route mismatch");
      }
      screens.hold_menu = false;
      scheduler.tick();
      if (result.state == bw::script::RunState::failed ||
          screens.started.back() != Screen::title) {
        throw std::runtime_error("Menu return did not resume original root");
      }
    }
    if (resources.acquire("script.root.dpk")) {
      throw std::runtime_error("Root destruction did not release publication");
    }
    std::cout << "{\"status\":\"passed\",\"script\":\"original "
                 "root.lub\",\"route\":[\"company_logo\",\"memory_card\","
                 "\"advertise\",\"title\",\"character_load\",\"title_menu\","
                 "\"title\"],\"services\":\"deterministic test fixture; no "
                 "rendering or native equivalence claim\"}\n";
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
