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

#include <iostream>
#include <stdexcept>
#include <vector>

#include "bw/tasks/task_lifecycle_scheduler.hpp"
#include "game/root_driver.hpp"
#include "game/startup_services.hpp"
#include "game/startup_title_surface.hpp"
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
void reconstructed_session(bw::resources::ResourceService& resources) {
  Media media;
  SpriteOutput output;
  bw::game::StartupTitleSurface surface(resources, output, [] { return 0U; });
  {
    bw::game::StartupSession session(media, &surface);
    session.start_root(resources, 2);
    bw::script::RunResult result{bw::script::RunState::idle, {}};
    for (unsigned frame = 0; frame < 500; ++frame) {
      result = session.advance({0.1F, 0x1010});
      if (result.state == bw::script::RunState::failed) {
        break;
      }
    }
    const std::vector<std::string> expected{
        "data/movie/marvelousLogo.ssu", "data/movie/xeedLogo.ssu",
        "data/movie/advertise.ssu", "data/movie/witchLOGO.ssu"};
    if (result.state != bw::script::RunState::failed ||
        result.diagnostic.find("Character load screen is not implemented") ==
            std::string::npos ||
        media.opened != expected || media.stops != 4 ||
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
    reconstructed_session(resources);
    std::cout
        << "{\"status\":\"passed\",\"script\":\"original "
           "root.lub\",\"route\":[\"company_logo\",\"memory_card\","
           "\"advertise\",\"title\",\"character_load\",\"title_menu\","
           "\"title\"],\"services\":\"deterministic test fixture; no "
           "rendering or native equivalence claim\",\"reconstructed_session\":"
           "{\"route\":[\"company_logo\",\"memory_card\",\"advertise\","
           "\"title\"],"
           "\"boundary\":\"unsupported character_load\",\"media\":"
           "\"controlled test observations; original title assets decoded with "
           "synthetic renderer\"}}\n";
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
