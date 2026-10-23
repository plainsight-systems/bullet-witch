// Memory extension to this existing provisioned scenario: on Mac inject the
// actual CPU provider into catalogs/resources, Lua, tasks and surfaces, then
// verify all charged storage is
// released after the original-root/menu owners retire. This covers native file
// reads and parsing plus original-script ownership; media/device observations
// remain controlled fixtures, not native playback. Design/implementation MCP
// R.1 (#rr-raii) scopes retained owners; MEM.6
// (guidelines/memory/MEM.6-inject-allocators-behind-one-interface.md) injects
// the native allocator at the scenario boundary. No new integration scenario.
// V1: public runtime contracts, docs/startup-runtime.md. Failures throw and
// produce nonzero exit; fixtures own their state. Algorithm: drive lifecycle,
// resume waits and check terminal outcomes. Cost bounded by small
// scripts/tasks. Optimization: in-memory fixtures avoid unrelated host I/O. MCP
// design and implementation: I.30 (#ri-encapsulate), SL.con.3 (#rsl-bounds),
// COPY.5 (guidelines/copy-move/COPY.5-resource-owning-types-are-move-only.md)
// guide owned context lifetime, bounded fixtures and callback boundaries.
// Provisioned integration scenario: real read-only catalog/archive/package,
// original Lua, and scheduler/root composition. Screen/global services below
// retain deterministic routing fixtures; an additional assertion in this same
// provisioned scenario uses the production StartupSession and controlled media.
// Neither assertion claims native device rendering or playback.
// Naming design/implementation consultation: cpp-guidelines P.3 (#rp-what)
// selects role-based names. cpp-performance COPY.4 (guidelines/copy-move/
// COPY.4-rule-of-five-rule-of-zero.md) preserves owning values and implicit
// moves; renaming adds no wrapper, allocation or payload copy.

// Startup composition design/implementation: cpp-guidelines I.1 (#ri-explicit)
// makes controlled observations distinct from reconstructed screen behavior;
// cpp-performance MEM.9 (guidelines/memory/
// MEM.9-embedded-allocate-at-init.md) bounds the frame loop and keeps media
// observation state inline. Real archive setup allocates before those updates.

#include <chrono>
#include <iostream>
#include <stdexcept>
#include <vector>

#include "bw/tasks/task_lifecycle_scheduler.hpp"
#include "game/root_driver.hpp"
#include "game/startup_menu_surface.hpp"
#include "game/startup_services.hpp"
#include "game/startup_title_surface.hpp"
#if BW_TEST_MACOS_MEMORY
#include "platform/macos/cpu_backing.hpp"
#endif
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
class Media final : public bw::game::LogoMedia {
 public:
  bool open_movie(std::string_view path) override {
    opened.emplace_back(path);
    return true;
  }
  bool movie_complete() const override { return true; }
  void stop_movie() noexcept override { ++stops; }
  void begin_fade(bw::game::FadeDirection, float) override {}
  bool fade_pending() const override { return false; }
  std::vector<std::string> opened;
  unsigned stops{};
};
class SpriteOutput final : public bw::render::SpriteOutput {
 public:
  bw::render::TextureHandle upload_texture(
      std::uint32_t width, std::uint32_t height,
      std::span<const std::uint8_t> rgba) override {
    if (rgba.size() != static_cast<std::size_t>(width) * height * 4) {
      throw std::runtime_error("Invalid original sprite pixels");
    }
    ++live;
    return {++identity};
  }
  void release_texture(bw::render::TextureHandle) noexcept override { --live; }
  void set_sprites(std::span<const bw::render::Sprite> commands) override {
    if (!commands.empty()) {
      presented = true;
    }
  }
  void clear_sprites() noexcept override {}
  std::size_t live{};
  std::uint64_t identity{};
  bool presented{};
};
struct ObservedMenu final : public bw::game::MenuSurface {
 public:
  ObservedMenu(bw::resources::ResourceService& resources, SpriteOutput& output)
      : surface_(resources, output) {}
  void begin() override {
    shown = false;
    surface_.begin();
  }
  bool ready() override { return surface_.ready(); }
  void show(const bw::game::MenuPresentation& view) override {
    surface_.show(view);
    current = view;
    shown = true;
  }
  void end() noexcept override {
    surface_.end();
    shown = false;
  }
  bw::game::MenuPresentation current;
  bool shown{};

  bw::game::StartupMenuSurface surface_;
};
void reconstructed_session(bw::resources::ResourceService& resources) {
  Media media;
  SpriteOutput output;
  bw::game::StartupTitleSurface surface(resources, output, [] { return 0U; });
  const auto directory =
      std::filesystem::current_path() /
      "research/evidence/reconstruction/frontend" /
      ("corpus-profile-" +
       std::to_string(
           std::chrono::steady_clock::now().time_since_epoch().count()));
  if (!std::filesystem::create_directories(directory)) {
    throw std::runtime_error("Corpus profile directory already exists");
  }
  auto storage = bw::platform::make_save_storage(directory);
  bw::game::Profile profile;
  ObservedMenu menu(resources, output);
  bw::game::FrontEnd frontend(profile, *storage, media, menu, {});
  {
    bw::game::StartupSession session(media, &surface, &frontend,
                                     resources.memory_domain());
    session.start_root(resources, 2);
    bw::script::RunResult result{bw::script::RunState::idle, {}};
    for (unsigned frame = 0; frame < 2000; ++frame) {
      std::uint32_t pressed = 0x1010;
      std::uint32_t repeated = 0;
      if (menu.shown) {
        pressed = 0;
        const auto& view = menu.current;
        const bool chosen = profile.costume() == 5;
        if (view.page == bw::game::MenuPage::main) {
          const std::size_t desired = chosen ? 0 : 3;
          if (view.selected == desired) {
            pressed = 0x1010;
          } else {
            repeated = 0x02000000;
          }
        } else if (view.page == bw::game::MenuPage::options) {
          if (chosen) {
            pressed = 0x2020;
          } else if (view.selected == 2) {
            pressed = 0x1010;
          } else {
            repeated = 0x02000000;
          }
        } else if (view.page == bw::game::MenuPage::costume) {
          if (view.rows.at(view.selected).compatibility_id == 5) {
            pressed = 0x1010;
          } else {
            repeated = 0x02000000;
          }
        } else if (view.page == bw::game::MenuPage::difficulty) {
          pressed = 0x1010;
        }
      }
      result = session.advance({0.1F, pressed}, repeated);
      if (result.state == bw::script::RunState::failed ||
          session.gameplay_request()) {
        break;
      }
    }
    const std::vector<std::string> expected{
        "data/movie/marvelousLogo.ssu", "data/movie/xeedLogo.ssu",
        "data/movie/advertise.ssu", "data/movie/witchLOGO.ssu"};
    if (result.state == bw::script::RunState::failed ||
        !session.gameplay_request() ||
        session.gameplay_request()->mission != "st01_00" ||
        session.gameplay_request()->selection.costume != 5 ||
        !session.mission_pending() || media.opened != expected ||
        media.stops != 4 ||
        session.progress(Screen::company_logo).status !=
            bw::game::FlowStatus::complete ||
        session.progress(Screen::memory_card).status !=
            bw::game::FlowStatus::complete ||
        !session.window_close_enabled() || !output.presented ||
        output.live != 0 || session.status(Screen::title) != 1) {
      throw std::runtime_error(
          "Original root reconstructed startup route mismatch: " +
          result.diagnostic);
    }
    bw::game::Profile reloaded;
    if (reloaded.load(*storage).status != bw::platform::SaveStatus::ok ||
        reloaded.costume() != 5 ||
        std::filesystem::exists(directory / "BulletWitchSaveData")) {
      throw std::runtime_error(
          "Native profile round trip lost costume or invented checkpoint");
    }
  }
  if (resources.acquire("script.root.dpk")) {
    throw std::runtime_error(
        "Session destruction did not release root publication");
  }
}
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
#if BW_TEST_MACOS_MEMORY
    const auto resource_memory =
        bw::memory::Domain::create(bw::platform::macos::make_cpu_backing());
#else
    const std::shared_ptr<bw::memory::Domain> resource_memory;
#endif
    {
      auto catalogs = resource_memory
                          ? bw::resources::CatalogSet(resource_memory)
                          : bw::resources::CatalogSet();
      catalogs.mount(30, path,
                     resource_memory ? bw::content::open_catalog(
                                           files, path, resource_memory)
                                     : bw::content::open_catalog(files, path));
      for (unsigned index = 1; index <= 5; ++index) {
        const auto name = std::string("PW") + std::to_string(index);
        const auto costume_path =
            game / "data/concept" / name / (name + ".rat");
        catalogs.mount(static_cast<int>(index), costume_path,
                       resource_memory
                           ? bw::content::open_catalog(files, costume_path,
                                                       resource_memory)
                           : bw::content::open_catalog(files, costume_path));
      }
#if BW_TEST_MACOS_MEMORY
      bw::resources::ResourceService resources(std::move(catalogs), files,
                                               resource_memory);
#else
      bw::resources::ResourceService resources(std::move(catalogs), files);
#endif
      Screens screens;
      {
        auto root =
            resource_memory
                ? bw::game::RootDriver(resources, screens, 1, resource_memory)
                : bw::game::RootDriver(resources, screens, 1);
        auto scheduler =
            resource_memory ? bw::tasks::TaskLifecycleScheduler(resource_memory)
                            : bw::tasks::TaskLifecycleScheduler();
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
        if (resource_memory) {
          const auto heap = root.memory_snapshot();
          if (heap.capacity != bw::script::kRootHeapBytes ||
              heap.rejected_requests != 0 || heap.live_allocations == 0) {
            throw std::runtime_error(
                "Original root heap capacity or admission mismatch");
          }
          std::cerr << "Root Lua heap: capacity=" << heap.capacity
                    << ", peak occupied=" << heap.peak_occupied_bytes
                    << ", free=" << heap.free_bytes
                    << ", largest free=" << heap.largest_free_bytes
                    << ", rejected=" << heap.rejected_requests << '\n';
        }
      }
      if (resources.acquire("script.root.dpk")) {
        throw std::runtime_error(
            "Root destruction did not release publication");
      }
      reconstructed_session(resources);
    }
#if BW_TEST_MACOS_MEMORY
    const auto memory = resource_memory->snapshot();
    if (memory.live_allocations != 0 || memory.live_bytes != 0 ||
        memory.backing_bytes != 0 || memory.pending_bytes != 0 ||
        memory.pending_backing_bytes != 0 || memory.pending_allocations != 0 ||
        memory.peak_live_bytes == 0) {
      throw std::runtime_error(
          "Native migrated CPU memory did not retire cleanly");
    }
    std::cerr << "Migrated CPU memory: peak payload=" << memory.peak_live_bytes
              << ", peak requested backing=" << memory.peak_backing_bytes
              << ", live after teardown=" << memory.live_bytes << '\n';
#endif
    std::cout
        << "{\"status\":\"passed\",\"script\":\"original "
           "root.lub\",\"route\":[\"company_logo\",\"memory_card\","
           "\"advertise\",\"title\",\"character_load\",\"title_menu\","
           "\"title\"],\"services\":\"deterministic test fixture; no "
           "rendering or native equivalence claim\",\"reconstructed_session\":"
           "{\"route\":[\"company_logo\",\"memory_card\",\"advertise\","
           "\"title\",\"character_load\",\"title_menu\"],"
           "\"boundary\":\"owned st01_00 gameplay request, "
           "costume5\",\"profile\":\"native storage round trip\",\"media\":"
           "\"controlled observations; original title/menu/costume assets "
           "decoded with "
           "synthetic renderer\"}}\n";
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
