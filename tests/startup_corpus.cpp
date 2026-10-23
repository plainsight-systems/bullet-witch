// Required output-policy observation applies C.128 (#rh-override): the fixture
// explicitly retains authored settings and checks publication. GPU.9
// (guidelines/gpu/GPU.9-suballocate-and-alias-transient-gpu-memory.md) adds no
// duplicate device owner. This fixture does not verify native host forwarding.
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

#include <algorithm>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <vector>

#include "bw/formats/world_texture.hpp"
#include "bw/tasks/task_lifecycle_scheduler.hpp"
#include "game/mission_loading.hpp"
#include "game/root_driver.hpp"
#include "game/stage_inspection.hpp"
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
// CPU fixture observes actual asset/selection ownership; it does not render or
// establish native color equivalence. Device shader tests remain separate.
class WorldOutput final : public bw::render::WorldOutput {
 public:
  bw::render::WorldModelHandle upload_model(
      const bw::formats::ModelDefinition& model) override {
    ++live_models;
    records.push_back(
        {0, 0, model.draws().size(), model.materials().size(), false, false});
    return {records.size()};
  }
  bw::render::WorldTextureHandle upload_world_texture(
      const bw::formats::WorldTextureImage& image) override {
    if (image.pixels.empty()) {
      throw std::runtime_error("Empty original world texture");
    }
    ++live_textures;
    return {++texture_identity};
  }
  void release_world_texture(bw::render::WorldTextureHandle) noexcept override {
    --live_textures;
  }
  void bind_model_textures(
      bw::render::WorldModelHandle model,
      std::span<const bw::render::WorldTextureHandle> textures) override {
    records.at(model.value - 1).texture_bindings_ready = std::ranges::all_of(
        textures, [](auto texture) { return texture.value != 0; });
  }
  bw::render::WorldGeometryStatus geometry_status(
      bw::render::WorldModelHandle model) const override {
    return records.at(model.value - 1);
  }
  void release_model(bw::render::WorldModelHandle model) noexcept override {
    if (model.value != 0) --live_models;
  }
  void set_world_output_parameters(
      const bw::render::WorldOutputParameters& parameters) override {
    output_parameters = parameters;
  }
  void set_world_draws(
      std::span<const bw::render::WorldColorDraw> draws) override {
    for (const auto& draw : draws) {
      if (draw.model.value == 0 ||
          draw.draw >= records.at(draw.model.value - 1).draws) {
        throw std::runtime_error(
            "Inspection refers to an unavailable original draw");
      }
    }
    submitted = draws.size();
    posed_draws = static_cast<std::size_t>(std::ranges::count_if(
        draws, [](const auto& draw) { return !draw.joint_matrices.empty(); }));
    caster_draws = static_cast<std::size_t>(std::ranges::count_if(
        draws, [](const auto& draw) { return draw.casts_shadow; }));
    harmonic_draws = static_cast<std::size_t>(
        std::ranges::count_if(draws, [](const auto& draw) {
          return (draw.effective_shader_flags & 0x200U) != 0;
        }));
    if (!draws.empty()) {
      sky_parameters = draws.front().parameters;
      sky_state = draws.front().state;
      sky_shader_flags = draws.front().effective_shader_flags;
      if (draws.size() > 1) world_parameters = draws[1].parameters;
    }
  }
  void update_world_texture_offsets(
      std::span<const bw::render::WorldTextureOffsetUpdate> updates) override {
    for (const auto& update : updates) {
      if (update.model.value == 0 ||
          update.draw >= records.at(update.model.value - 1).draws) {
        throw std::runtime_error(
            "Material update refers to an unavailable draw");
      }
    }
    texture_updates.assign(updates.begin(), updates.end());
  }
  std::vector<bw::render::WorldTextureOffsetUpdate> texture_updates;
  void clear_world_draws() noexcept override { submitted = 0; }
  bw::render::WorldOutputParameters output_parameters;
  bw::render::WorldColorParameters sky_parameters, world_parameters;
  bw::render::WorldSurfaceState sky_state;
  std::uint32_t sky_shader_flags{};
  std::vector<bw::render::WorldGeometryStatus> records;
  std::size_t live_models{}, live_textures{}, submitted{}, harmonic_draws{},
      posed_draws{}, caster_draws{};
  std::uint64_t texture_identity{};
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
void reconstructed_session(bw::resources::ResourceService& resources,
                           const bw::platform::Files& files, bool concept_route,
                           const std::filesystem::path& game) {
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
    // Navigate the real menu policy to costume selection and an owned mission
    // request.
    for (unsigned frame = 0; frame < 2000; ++frame) {
      std::uint32_t pressed = 0x1010;
      std::uint32_t repeated = 0;
      if (menu.shown) {
        pressed = 0;
        const auto& view = menu.current;
        const bool chosen = profile.costume() == 5;
        if (view.page == bw::game::MenuPage::main) {
          const std::size_t desired = chosen ? (concept_route ? 4 : 0) : 3;
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
        } else if (view.page == bw::game::MenuPage::extra) {
          if (view.rows.at(view.selected).compatibility_id == 1) {
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
        session.gameplay_request()->mission !=
            (concept_route ? "concept" : "st01_00") ||
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
    if (concept_route) {
      const auto& gameplay = session.gameplay_request();
      if (!gameplay) {
        throw std::runtime_error("Concept request is missing");
      }
      const auto& request = gameplay.value();
      if (request.script_name != "cm01_00" || request.selection.stage != 1 ||
          request.selection.original_status != 5 ||
          session.status(Screen::title_menu) != 6 || session.global(3) != 1) {
        throw std::runtime_error(
            "Original root concept accessor/alias mismatch");
      }
      WorldOutput world;
      bw::game::MissionLoading loading(
          files, resources.catalogs(), resources.memory_domain(), &world,
          bw::game::MissionLoadOptions{game, profile.current_difficulty()});
      bw::game::MissionDriver mission(resources, loading, 101,
                                      request.script_name,
                                      resources.memory_domain());
      for (unsigned step = 0; step < 128; ++step) {
        const auto& progress = mission.advance();
        if (progress.phase == bw::game::MissionPhase::failed) {
          throw std::runtime_error(progress.diagnostic);
        }
        if (progress.phase == bw::game::MissionPhase::blocked ||
            progress.phase == bw::game::MissionPhase::awaiting_world_setup) {
          break;
        }
      }
      if (mission.progress().phase !=
              bw::game::MissionPhase::awaiting_world_setup ||
          mission.stage_name() != "st04_c" || !loading.loading_scene().active ||
          loading.loading_scene().item != 5 || !loading.sky() ||
          !loading.stage() || mission.memory_snapshot().capacity != 0x40000 ||
          !session.mission_pending()) {
        throw std::runtime_error(
            "Original mission load did not finish at the world-setup boundary");
      }
      std::cerr << "Original cm01_00 load returned: "
                << mission.progress().diagnostic << '\n';
      if (!loading.sound_bank() || loading.sound_bank()->waves().size() != 6 ||
          loading.sound_bank()->cues().size() != 5 ||
          loading.cover_points().size() != 42 || !loading.text_table() ||
          loading.preloaded_models() != 54 ||
          loading.character_support_sets() != 11 ||
          loading.npc_palette_bindings() != 178) {
        throw std::runtime_error(
            "Original mission dependency construction census mismatch");
      }
      std::cerr << "Original native-load owners: " << loading.preloaded_models()
                << " dependency model variants, "
                << loading.character_support_sets()
                << " character support sets, " << loading.npc_palette_bindings()
                << " NPC palette bindings, "
                << loading.sound_bank()->cues().size() << " sound cues, "
                << loading.cover_points().size() << " cover points\n";
      constexpr std::int32_t kClearEvent = 1001, kGameOverEvent = 1000;
      const auto callbacks = loading.callbacks();
      if (callbacks.size() != 64 || callbacks[0].event != kClearEvent ||
          callbacks[0].function != "clear" || callbacks[0].active ||
          callbacks[1].event != kGameOverEvent ||
          callbacks[1].function != "gameover" || callbacks[1].active) {
        throw std::runtime_error(
            "Original mission callback registration mismatch");
      }
      const auto retained_models = world.live_models;
      const auto retained_textures = world.live_textures;
      // Repeated optional-member construction preserves the same absence and
      // ownership contracts outside the complete original load sequence.
      bw::game::MissionLoading inputs(files, resources.catalogs(),
                                      resources.memory_domain());
      const auto lease = resources.acquire("script.cm01_00.dpk");
      if (!lease) throw std::runtime_error("Selected mission package vanished");
      inputs.retain_package(bw::resources::NamedFilePackage::parse(
          lease->bytes(), resources.memory_domain()));
      inputs.request(bw::game::MissionLoadKind::score, "score");
      if (inputs.poll().state != bw::game::MissionLoadState::ready ||
          inputs.package_input(bw::game::MissionLoadKind::score).bytes.size() !=
              1704) {
        throw std::runtime_error("Original score member ownership mismatch");
      }
      inputs.request(bw::game::MissionLoadKind::sound_environment,
                     "st04_c_env");
      if (inputs.poll().state != bw::game::MissionLoadState::ready ||
          inputs.package_input(bw::game::MissionLoadKind::sound_environment)
              .present) {
        throw std::runtime_error(
            "Absent optional environment did not preserve reset state");
      }
      inputs.request(bw::game::MissionLoadKind::text, "cm01_00");
      if (inputs.poll().state != bw::game::MissionLoadState::ready ||
          !inputs.text_table() || inputs.text_table()->records(3) != 18) {
        throw std::runtime_error("Original mission text registration mismatch");
      }
      inputs.request(bw::game::MissionLoadKind::hiding_places, "st04_c_kakure");
      if (inputs.poll().state != bw::game::MissionLoadState::ready ||
          inputs.cover_points().size() != 42) {
        throw std::runtime_error("Original cover-point construction mismatch");
      }
      if (!inputs.text_table()) {
        throw std::runtime_error("Registered text owner disappeared");
      }
      std::cerr << "Original package consumers: text entries="
                << inputs.text_table()->records(3)
                << ", cover points=" << inputs.cover_points().size() << '\n';
      inputs.cancel();
      {
        const auto& stage = loading.stage();
        const auto& sky = loading.sky();
        if (!stage || !sky) {
          throw std::runtime_error(
              "Original stage/sky definitions are missing");
        }
        // Native class-zero selection owns ten ordinary stage models. Three
        // custom passes remain explicit omissions in the static draw subset.
        const auto ordinary_models = std::ranges::count_if(
            stage->models,
            [](const auto& model) { return model.static_world_object; });
        if (ordinary_models != 10) {
          throw std::runtime_error(
              "Original ordinary stage model census mismatch");
        }
        const auto background_models = loading.resident_dependencies();
        const auto& background = loading.background();
        bw::game::StageInspection inspection(
            world, {stage.value(), loading.resident_models()},
            {sky.value(), loading.sky_resident_models()},
            resources.memory_domain(), std::nullopt,
            {background ? &*background : nullptr, background_models});
        std::cerr
            << "Original static inspection: " << inspection.selected_draws()
            << " selected, " << inspection.omitted_draws()
            << " omitted selected-world draws; synthetic device fixture\n";
        // Actual authored labels bind two independent float2 curves. Advancing
        // the existing view updates retained constants without rebuilding it.
        const auto memory_before_animation =
            resources.memory_domain()->snapshot().live_bytes;
        inspection.advance(1.0);
        if (world.texture_updates.size() != 6 ||
            resources.memory_domain()->snapshot().live_bytes !=
                memory_before_animation) {
          throw std::runtime_error(
              "Stage material animation binding/allocation mismatch");
        }
        // Each source TANM encloses three DIP draws before its TRST reset.
        std::array<float, 6> scrolling{};
        for (std::size_t i = 0; i < scrolling.size(); ++i) {
          const auto& offset = world.texture_updates.at(i).texture_offset[0];
          if (offset[0] != 1 || offset[2] != 0 || offset[3] != 0) {
            throw std::runtime_error(
                "Material float2 animation lost its source padding");
          }
          scrolling.at(i) = offset[1];
        }
        std::ranges::sort(scrolling);
        for (std::size_t i = 0; i < scrolling.size(); ++i) {
          const float expected = i < 3 ? -.015F : -.0075F;
          if (std::abs(scrolling.at(i) - expected) > .000001F) {
            throw std::runtime_error(
                "Stage material animation did not sample authored keys at "
                "30fps");
          }
        }
        if (!stage->environment) {
          throw std::runtime_error("Original stage environment is missing");
        }
        const auto& authored_output = stage->environment->tone_mapping;
        // The original sky follows the eye; its material-space translation is
        // only authored height, regardless of the player's world position.
        if (world.sky_parameters.world_rows[0][3] != 0 ||
            world.sky_parameters.world_rows[1][3] !=
                stage->environment->sky_height ||
            world.sky_parameters.world_rows[2][3] != 0) {
          throw std::runtime_error(
              "Sky instance did not retain camera centering");
        }
        if (world.sky_state.depth_compare !=
                bw::render::WorldDepthCompare::always ||
            world.sky_state.depth_write) {
          throw std::runtime_error(
              "Sky pass must disable depth testing and writes");
        }
        const std::array<std::array<float, 4>, 7> white_irradiance{
            {{0, 0, 0, 0.9999998807907104F},
             {0, 0, 0, 0.9999998807907104F},
             {0, 0, 0, 0.9999998807907104F},
             {},
             {},
             {},
             {0, 0, 0, 1}}};
        if (world.sky_shader_flags != 0x06e00200 ||
            world.sky_parameters.irradiance_coefficients != white_irradiance ||
            world.sky_parameters.ambient != std::array<float, 4>{}) {
          throw std::runtime_error(
              "Sky must retain original white harmonics without ambient "
              "substitution");
        }
        if (stage->environment->ambient_color !=
                std::array<float, 3>{1, 1, 1} ||
            world.world_parameters.irradiance_coefficients !=
                white_irradiance ||
            world.harmonic_draws != world.submitted) {
          throw std::runtime_error(
              "Ordinary stage objects lost authored spherical-harmonic "
              "lighting");
        }
        if (!world.output_parameters.tone_mapping_enabled ||
            world.output_parameters.exposure_key !=
                authored_output.exposure_key ||
            world.output_parameters.gamma != authored_output.gamma ||
            world.output_parameters.bloom_enabled !=
                authored_output.bloom_enabled ||
            world.output_parameters.exposure.history_window !=
                authored_output.history_window) {
          throw std::runtime_error(
              "Stage inspection dropped authored display policy");
        }
        if (inspection.selected_draws() != 4100 ||
            inspection.omitted_draws() != 0 || inspection.sky_draws() != 1 ||
            inspection.background_draws() != 674 || world.posed_draws != 674 ||
            world.caster_draws != 4100 || world.submitted != 4775) {
          throw std::runtime_error(
              "Original static inspection selection differs from material "
              "census");
        }
      }
      if (world.live_models != retained_models ||
          world.live_textures != retained_textures || world.submitted != 0) {
        throw std::runtime_error(
            "Inspection retained device owners after teardown");
      }
      mission.cancel();
      if (mission.progress().phase != bw::game::MissionPhase::cancelled ||
          loading.sky() || resources.acquire("script.cm01_00.dpk") ||
          world.live_models != 0 || world.live_textures != 0) {
        throw std::runtime_error(
            "Mission cancellation retained script or definition ownership");
      }
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
      const auto concept_path = game / "data/concept/cm01_00/cm01_00.rat";
      catalogs.mount(
          40, concept_path,
          resource_memory
              ? bw::content::open_catalog(files, concept_path, resource_memory)
              : bw::content::open_catalog(files, concept_path));
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
      reconstructed_session(resources, files, false, game);
      reconstructed_session(resources, files, true, game);
    }
#if BW_TEST_MACOS_MEMORY
    // Inspect charges only after catalogs, sessions and retained publications
    // retire.
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
           "costume5; concept alias cm01_00 load returned awaiting setup; "
           "cancelled\",\"profile\":\"native storage round trip\",\"media\":"
           "\"controlled observations; original title/menu/costume assets "
           "decoded with "
           "synthetic renderer\"}}\n";
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
