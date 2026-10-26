// Gameplay extension brackets actual actor requests with one resource frame;
// successful resident switches publish mission selection before later actions.
// Original CSTM values and consumed ammunition guard against presentation-only
// fire passing this existing scenario. No additional resource mutation API.
// Both-phase E.4 and MEM.9 preserve transaction order and fixed owner storage.
// Lighting fixture publication uses a setup-only index (I.1/MEM.9) so the
// corpus exercises full production batches without quadratic observer work.
// Progression extension remains within this existing original-root integration
// scenario. Save a source-rule purchase, load through the actual front end,
// inspect immutable handoff and initialize source resident weapon classes from
// mission authority. Controlled renderer does not imply native device parity.
// Design C.9/I.30, implementation E.4 (#re-design-invariants); both-phase MEM.9
// (guidelines/memory/MEM.9-embedded-allocate-at-init.md) bounds added value
// work. Caster regression: both-phase I.1 (#ri-explicit) checks source pass
// admission against retained original chunk metadata; GPU.1 (guidelines/gpu/
// GPU.1-keep-data-on-device.md) adds no production readback or frame-time work.
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
#include <bit>
#include <chrono>
#include <iostream>
#include <map>
#include <stdexcept>
#include <vector>

#include "bw/audio/wave.hpp"
#include "bw/formats/texture_mips.hpp"
#include "bw/formats/world_texture.hpp"
#include "bw/tasks/task_lifecycle_scheduler.hpp"
#include "game/gameplay_hud_surface.hpp"
#include "game/mission_audio.hpp"
#include "game/mission_control.hpp"
#include "game/mission_gameplay.hpp"
#include "game/mission_loading.hpp"
#include "game/mission_pause_surface.hpp"
#include "game/root_driver.hpp"
#include "game/source_random.hpp"
#include "game/stage_inspection.hpp"
#include "game/startup_loading_surface.hpp"
#include "game/startup_menu_surface.hpp"
#include "game/startup_services.hpp"
#include "game/startup_title_surface.hpp"
#if BW_TEST_MACOS_MEMORY
#include "platform/macos/cpu_backing.hpp"
#endif
namespace {
// Independent authored fixture: production placement is an explicitly named
// shim.
constexpr bw::game::ScriptedPlayerPose kFixtureInitialPose{
    {-61.91F, .65F, -97.57F}, -.28F};
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
  explicit SpriteOutput(bool texture_mips = false)
      : texture_mips_(texture_mips) {}

  bw::render::TextureHandle upload_texture(
      const bw::formats::WorldTextureImage& image) override {
    const std::size_t channels =
        image.format == bw::formats::WorldTextureFormat::rgba16_float ? 8 : 4;
    if (image.shape != bw::formats::WorldTextureShape::two_dimensional ||
        image.mip_levels != 1 ||
        image.pixels.size() !=
            static_cast<std::size_t>(image.width) * image.height * channels) {
      throw std::runtime_error("Invalid decoded original sprite resource");
    }
    if (texture_mips_) {
      const auto prepared = bw::formats::generate_texture_mips(
          image, image.pixels.get_allocator());
      if (prepared.mip_levels != static_cast<std::uint32_t>(std::bit_width(
                                     std::max(image.width, image.height)))) {
        throw std::runtime_error("DDS sprite mip generation omitted levels");
      }
    }
    ++live;
    return {++identity};
  }

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
    // Match the native output's positive-area contract, including transition
    // endpoints. A mock that accepts collapsed quads hides real host exits.
    for (const auto& command : commands) {
      if (!(command.destination.width > 0) ||
          !(command.destination.height > 0)) {
        throw std::runtime_error(
            "Corpus sprite has nonpositive destination area");
      }
    }
    latest.assign(commands.begin(), commands.end());
    if (!commands.empty()) {
      presented = true;
    }
  }
  void clear_sprites() noexcept override {}
  std::size_t live{};
  std::uint64_t identity{};
  bool presented{};
  std::vector<bw::render::Sprite> latest;

  const bool texture_mips_;
};
// Count only reads through this capability; selection must reuse admission.
class ObservedFiles final : public bw::platform::Files {
 public:
  explicit ObservedFiles(const bw::platform::Files& source) : source_(source) {}
  bw::resources::Bytes read(const std::filesystem::path& path) const override {
    ++reads_;
    return source_.read(path);
  }
  bw::resources::ArchiveBytes read(
      const std::filesystem::path& path,
      std::shared_ptr<bw::memory::Domain> domain) const override {
    ++reads_;
    return source_.read(path, std::move(domain));
  }
  std::filesystem::path canonical(
      const std::filesystem::path& path) const override {
    return source_.canonical(path);
  }
  std::size_t reads() const { return reads_; }

 private:
  mutable std::size_t reads_{};
  const bw::platform::Files& source_;
};
// CPU fixture observes actual asset/selection ownership; it does not render or
// establish native color equivalence. Device shader tests remain separate.
class WorldOutput final : public bw::render::WorldOutput {
 public:
  explicit WorldOutput(bool texture_mips = false)
      : texture_mips_(texture_mips) {}

  bw::render::WorldModelHandle upload_model(
      const bw::formats::ModelDefinition& model) override {
    if (reject_next_model) {
      reject_next_model = false;
      throw std::runtime_error("Injected model upload failure");
    }
    ++live_models;
    auto& chunks = model_chunks.emplace_back();
    for (const auto& draw : model.draws()) chunks.push_back(draw.chunk);
    records.push_back(
        {0, 0, model.draws().size(), model.materials().size(), false, false});
    return {records.size()};
  }
  bw::render::WorldTextureHandle upload_world_texture(
      const bw::formats::WorldTextureImage& image) override {
    const auto expected_levels =
        texture_mips_ ? static_cast<std::uint32_t>(
                            std::bit_width(std::max(image.width, image.height)))
                      : 1U;
    if (image.mip_levels != expected_levels) {
      throw std::runtime_error(
          "Texture preparation lost its launch mip policy");
    }
    std::size_t expected_bytes = 0;
    auto width = image.width, height = image.height;
    const std::size_t channels =
        image.format == bw::formats::WorldTextureFormat::rgba16_float ? 8 : 4;
    const std::size_t faces =
        image.shape == bw::formats::WorldTextureShape::cube ? 6 : 1;
    for (std::uint32_t level = 0; level < image.mip_levels; ++level) {
      expected_bytes +=
          static_cast<std::size_t>(width) * height * channels * faces;
      width = std::max(width / 2, 1U);
      height = std::max(height / 2, 1U);
    }
    if (image.pixels.size() != expected_bytes) {
      throw std::runtime_error(
          "Texture preparation published an incomplete mip payload");
    }
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
      if (draw.casts_shadow &&
          model_chunks.at(draw.model.value - 1).at(draw.draw) > 1) {
        throw std::runtime_error(
            "Static caster includes a pass outside original opaque/cutout "
            "selection");
      }
    }
    draw_commands.assign(draws.begin(), draws.end());
    draw_destinations.clear();
    for (std::size_t index = 0; index < draw_commands.size(); ++index) {
      const auto& draw = draw_commands.at(index);
      draw_destinations.emplace(std::pair{draw.model.value, draw.draw}, index);
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
  void replace_world_model_draws(
      bw::render::WorldModelHandle previous,
      std::span<const bw::render::WorldColorDraw> replacement) override {
    if (std::exchange(reject_next_replacement, false)) {
      throw std::runtime_error("Injected appearance publication failure");
    }
    std::vector<bw::render::WorldColorDraw> pending;
    bool replaced = false;
    for (const auto& draw : draw_commands) {
      if (draw.model == previous) {
        if (!replaced) {
          pending.insert(pending.end(), replacement.begin(), replacement.end());
        }
        replaced = true;
      } else {
        pending.push_back(draw);
      }
    }
    if (!replaced) throw std::runtime_error("Previous model was not submitted");
    set_world_draws(pending);
  }
  std::vector<bw::render::WorldColorDraw> draw_commands;
  // Fixture observation must not scan thousands of unrelated retained draws
  // for each production batch element. This index changes no expected value.
  std::multimap<std::pair<std::uint64_t, std::size_t>, std::size_t>
      draw_destinations;
  auto destinations(bw::render::WorldModelHandle model, std::size_t draw) {
    const auto [first, last] =
        draw_destinations.equal_range({model.value, draw});
    return std::ranges::subrange(first, last);
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
  void update_world_poses(
      std::span<const bw::render::WorldPoseUpdate> updates) override {
    ++pose_publications;
    for (const auto& update : updates) {
      if (update.model.value == 0 ||
          update.draw >= records.at(update.model.value - 1).draws ||
          update.joint_matrices.empty()) {
        throw std::runtime_error("Pose update refers to an unavailable draw");
      }
    }
  }
  void update_world_lighting(
      std::span<const bw::render::WorldLightingUpdate> updates) override {
    for (const auto& update : updates) {
      const auto matches = destinations(update.model, update.draw);
      if (!std::ranges::any_of(matches, [&](const auto& entry) {
            const auto& draw = draw_commands.at(entry.second);
            return draw.instance == update.instance.value_or(draw.instance);
          })) {
        throw std::runtime_error(
            "Lighting update refers to an unavailable draw");
      }
      if (update.local_light_count > update.local_lights.size()) {
        throw std::runtime_error("Too many local lights");
      }
      for (const auto& light : update.local_lights) {
        for (const auto& row : {light.position, light.diffuse, light.specular,
                                light.attenuation}) {
          for (float value : row) {
            if (!std::isfinite(value)) {
              throw std::runtime_error("Nonfinite sampled local light");
            }
          }
        }
      }
      for (const auto& row : update.irradiance_coefficients) {
        for (float value : row) {
          if (!std::isfinite(value)) {
            throw std::runtime_error("Nonfinite sampled light");
          }
        }
      }
    }
    for (const auto& update : updates) {
      for (const auto& entry : destinations(update.model, update.draw)) {
        auto& draw = draw_commands.at(entry.second);
        if (draw.instance == update.instance.value_or(draw.instance)) {
          draw.parameters.irradiance_coefficients =
              update.irradiance_coefficients;
          draw.parameters.local_lights = update.local_lights;
          draw.parameters.local_light_meta[0] = update.local_light_count;
          if (update.reflection_texture) {
            draw.reflection_texture = update.reflection_texture;
          }
        }
      }
    }
  }
  void clear_world_draws() noexcept override {
    submitted = 0;
    draw_commands.clear();
    draw_destinations.clear();
  }
  void update_shape_positions(
      std::span<const bw::render::WorldShapePositionUpdate> updates) override {
    for (const auto& update : updates) {
      if (update.model.value == 0 || update.model.value > records.size() ||
          update.positions.empty()) {
        throw std::runtime_error("Shape update refers to unavailable geometry");
      }
    }
  }
  void update_world_visibility(
      std::span<const bw::render::WorldVisibilityUpdate> updates) override {
    for (const auto& update : updates) {
      const auto matches = destinations(update.model, update.draw);
      if (!std::ranges::any_of(matches, [&](const auto& entry) {
            const auto& draw = draw_commands.at(entry.second);
            return draw.instance == update.instance.value_or(draw.instance);
          })) {
        throw std::runtime_error(
            "Visibility update refers to an unavailable draw");
      }
    }
    for (const auto& update : updates) {
      for (const auto& entry : destinations(update.model, update.draw)) {
        auto& draw = draw_commands.at(entry.second);
        if (draw.instance == update.instance.value_or(draw.instance)) {
          draw.visible = update.visible;
          draw.casts_shadow = update.casts_shadow.value_or(draw.casts_shadow);
          if (update.owner_opacity) draw.owner_opacity = update.owner_opacity;
        }
      }
    }
  }
  void update_world_view(const bw::render::WorldViewUpdate& update) override {
    view_update = update;
  }
  std::optional<bw::render::WorldViewUpdate> view_update;
  bw::render::WorldOutputParameters output_parameters;
  bw::render::WorldColorParameters sky_parameters, world_parameters;
  bw::render::WorldSurfaceState sky_state;
  std::uint32_t sky_shader_flags{};
  bool reject_next_model{}, reject_next_replacement{};
  std::vector<bw::render::WorldGeometryStatus> records;
  std::vector<std::vector<std::uint32_t>> model_chunks;
  std::size_t live_models{}, live_textures{}, submitted{}, harmonic_draws{},
      posed_draws{}, caster_draws{};
  std::uint64_t texture_identity{};
  std::size_t pose_publications{};

  const bool texture_mips_;
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
class CorpusAudioOutput final : public bw::audio::CueOutput {
 public:
  struct Slot {
    bw::audio::CueRequest request;
    std::uint32_t generation{};
    bool active{};
  };
  std::array<Slot, 128> slots{};
  std::array<std::size_t, 51> bank_plays{};
  std::size_t registrations{}, plays{}, stops{}, positions{}, updates{},
      listener_updates{}, pause_calls{};
  bool sealed{}, paused{};
  bool trace_cues{};
  std::array<std::array<std::uint16_t, 2>, 512> cue_trace{};
  std::size_t cue_trace_size{};
  void register_bank(std::uint16_t, std::span<const std::uint8_t> sound,
                     std::span<const std::uint8_t> waves,
                     std::span<const std::uint16_t>) override {
    if (sound.empty() || waves.empty() || sealed) {
      throw std::runtime_error("Invalid corpus bank");
    }
    ++registrations;
  }
  void seal() override { sealed = true; }
  std::optional<bw::audio::CueHandle> play(
      const bw::audio::CueRequest& request) override {
    if (!sealed) throw std::runtime_error("Unsealed corpus output");
    for (std::size_t i = 0; i < slots.size(); ++i) {
      auto& slot = slots.at(i);
      if (slot.active) continue;
      slot.request = request;
      slot.active = true;
      ++slot.generation;
      ++plays;
      ++bank_plays.at(request.bank);
      if (trace_cues) {
        cue_trace.at(cue_trace_size++) = {request.bank, request.cue};
      }
      return bw::audio::CueHandle{static_cast<std::uint32_t>(i),
                                  slot.generation};
    }
    return {};
  }
  bool active(bw::audio::CueHandle handle) const override {
    return handle.slot < slots.size() && slots.at(handle.slot).active &&
           slots.at(handle.slot).generation == handle.generation;
  }
  bool stop(bw::audio::CueHandle handle) override {
    if (!active(handle)) return false;
    slots.at(handle.slot).active = false;
    ++stops;
    return true;
  }
  bool set_position(bw::audio::CueHandle handle,
                    const bw::math::Vector3<float>& position) override {
    if (!active(handle)) return false;
    slots.at(handle.slot).request.position = position;
    ++positions;
    return true;
  }
  void set_listener(const bw::audio::Listener&) override { ++listener_updates; }
  void pause(bool value) override {
    paused = value;
    ++pause_calls;
  }
  void set_mix_gains(const bw::audio::MixGains& value) override {
    gains = value;
  }
  bw::audio::MixGains gains;
  void update() override {
    ++updates;
    // Controlled output retires presentation-only one-shots at its service
    // boundary; gameplay cue lifetime remains explicit in actor fixtures.
    for (auto& slot : slots) {
      if (slot.request.pause_domain == bw::audio::PauseDomain::presentation) {
        slot.active = false;
      }
    }
  }
};
// A known slot or a familiar filename cannot grant semantics to another
// actor or an unrelated publication. Reuse one actual decoded track here;
// this tests admission identity, independently of pose/render appearance.
// The fresh pose lies outside the source lamp influence sphere (radius5.5924).
// A complete empty packet must clear all retained direct-light slots.
void require_initial_local_light_clear(const WorldOutput& world,
                                       bw::render::WorldModelHandle body) {
  const auto draw = std::ranges::find_if(
      world.draw_commands,
      [&](const auto& candidate) { return candidate.model == body; });
  if (draw == world.draw_commands.end() ||
      draw->parameters.local_light_meta[0] != 0) {
    throw std::runtime_error("Initial source light selection should be empty");
  }
  for (const auto& light : draw->parameters.local_lights) {
    for (const auto& row :
         {light.position, light.diffuse, light.specular, light.attenuation}) {
      if (!std::ranges::all_of(row, [](float value) { return value == 0; })) {
        throw std::runtime_error(
            "Empty source selection retained a local light");
      }
    }
  }
}

void unknown_pose_admission(const bw::content::CharacterSupport& original,
                            const bw::formats::SkeletonDefinition& skeleton,
                            const std::shared_ptr<bw::memory::Domain>& domain) {
  struct PublicationIdentity {
    std::string_view owner;
    std::string_view publication;
    std::string_view name;
  };
  const auto verify = [&](PublicationIdentity identity) {
    bw::content::CharacterSupport support(domain);
    support.motions.push_back({bw::content::CharacterPublication(
                                   identity.publication, {},
                                   bw::memory::Allocator<std::byte>(domain)),
                               original.motions.front().definition});
    const std::array aliases{
        bw::animation::ClipAliasRequest{1, identity.name, true}};
    const bw::game::CharacterAnimations prepared(
        support, skeleton, identity.owner, 2, aliases, domain);
    if (prepared.pose_semantics(0) != bw::game::MotionPoseSemantics::unknown) {
      throw std::runtime_error("Unverified publication gained pose admission");
    }
  };
  verify({"npc", "test.npc_stand_l", "stand_l"});
  verify({"pw0", "test.pw0_dm_ear", "dm_ear"});
  verify({"wa0", "test.wa0_unclassified", "unclassified"});
}

// Controlled presentation intervals enter the same engine publication owner as
// the native host. This fixture does not sample a real device clock. Every
// stage borrows its own clock/control lifetime; helpers cannot advance a
// private clock.
struct CorpusWorldTime {
  bw::world::WorldClock clock;
  bw::game::MissionControl control;
  std::optional<bw::game::MissionPlayerState> player;
  std::optional<bw::game::PlayerResources> resources;
  void bind_resources(const bw::game::PlayerResourceSettings& settings) {
    player.emplace(bw::game::MissionPlayerEntry{
        .concept_mission = true,
        .concept_profile = bw::game::ConceptProfile::development_completed});
    resources.emplace(settings, *player, clock);
  }
  // Mirror the host's successful resident-selection transaction explicitly;
  // the actor has no authority to mutate effective mission selection.
  void change_weapon(bw::game::StageInspection& scene,
                     bw::game::MissionLoading& loading, unsigned weapon,
                     bool normal = false) {
    if (normal) {
      scene.switch_weapon(loading, weapon);
    } else {
      scene.change_weapon(loading, weapon);
    }
    if (player) player->select_weapon(weapon);
  }
  void set_paused(bw::game::StageInspection& scene, bool paused) {
    if (!control.set_user_paused(paused)) {
      throw std::logic_error("Fixture user-pause request was excluded");
    }
    scene.synchronize_pause();
  }
  void advance(bw::game::StageInspection& scene, double seconds,
               bw::game::StageTraversalInput input = {}, bool ui = false) {
    const auto frame =
        clock.publish(static_cast<float>(seconds), control.admission(ui));
    if (resources) resources->begin_frame(frame);
    scene.advance(frame, input);
    if (resources) resources->finish_frame();
  }
};

// Exercise the production mission coordinator and its original pause resources
// within this existing original-content scenario. Controlled output validates
// resource/command lifetime; no native visual parity is claimed.
struct CorpusPausePresentation {
  CorpusPausePresentation(bw::resources::ResourceService& service,
                          SpriteOutput& output,
                          bw::game::StartupMenuSurface& menu,
                          const bw::platform::Files& reader,
                          const std::filesystem::path& location,
                          bw::game::Profile& save, CorpusAudioOutput& sound,
                          bw::platform::SaveStorage& storage)
      : resources(service),
        sprites(output),
        auxiliary(menu),
        files(reader),
        game(location),
        profile(save),
        audio(sound),
        storage(storage) {}
  ~CorpusPausePresentation() = default;
  CorpusPausePresentation(const CorpusPausePresentation&) = delete;
  CorpusPausePresentation& operator=(const CorpusPausePresentation&) = delete;
  CorpusPausePresentation(CorpusPausePresentation&&) = delete;
  CorpusPausePresentation& operator=(CorpusPausePresentation&&) = delete;
  bw::resources::ResourceService& resources;
  SpriteOutput& sprites;
  bw::game::StartupMenuSurface& auxiliary;
  const bw::platform::Files& files;
  const std::filesystem::path& game;
  bw::game::Profile& profile;
  CorpusAudioOutput& audio;
  bw::platform::SaveStorage& storage;
};
enum class CorpusPauseRoute : std::uint8_t { children_and_resume, retirement };
void mission_gameplay_composition(
    CorpusWorldTime& time, bw::game::StageInspection& stage,
    bw::game::MissionLoading& loading, bw::game::SourceRandom& random,
    const CorpusPausePresentation& context,
    CorpusPauseRoute route = CorpusPauseRoute::children_and_resume) {
  const auto retained_textures = context.sprites.live;
  auto icons = context.files.read(context.game / "data/menu/9900",
                                  context.resources.memory_domain());
  auto text = context.files.read(context.game / "data/menu/9952",
                                 context.resources.memory_domain());
  bw::game::GameplayHudSurface hud(context.resources, context.sprites);
  hud.prepare();
  bw::game::MissionPauseSurface surface(context.resources, context.sprites,
                                        context.auxiliary, std::move(icons),
                                        std::move(text));
  struct ObservedPause final : bw::game::MissionPauseOutput {
    explicit ObservedPause(bw::game::MissionPauseSurface& owner)
        : owner(owner) {}
    bw::game::MissionPauseSurface& owner;
    bw::game::MissionPauseView active{bw::game::MissionPauseView::none};
    bw::game::PauseMenuPresentation root;
    bw::game::PauseHelpPresentation help;
    bw::game::PauseOptionsPresentation options;
    bw::game::MenuPresentation costume;
    bool activate(bw::game::MissionPauseView view) override {
      const auto changed = owner.activate(view);
      active = view;
      return changed;
    }
    const std::array<unsigned, bw::game::help_topic_count>& help_line_counts()
        const override {
      return owner.help_line_counts();
    }
    void show(const bw::game::PauseMenuPresentation& view) override {
      owner.show(view);
      root = view;
    }
    void show(const bw::game::PauseHelpPresentation& view) override {
      owner.show(view);
      help = view;
    }
    void show(const bw::game::PauseOptionsPresentation& view) override {
      owner.show(view);
      options = view;
    }
    void show(const bw::game::ControllerOptionsPresentation& view) override {
      owner.show(view);
    }
    void show(const bw::game::MenuPresentation& view) override {
      owner.show(view);
      costume = view;
    }
  } output(surface);
  if (!time.player || !time.resources) {
    throw std::logic_error("Coordinator fixture resources absent");
  }
  bw::game::DeviceOptions device(context.storage);
  const bw::game::PauseOptionsServices settings{
      context.profile.settings_control(),
      context.profile.settings_save_control(context.storage), device.control(),
      device.save_control()};
  // The first coordinator admission binds the actor's resource capability once.
  // Later retirement tests the same interaction owner after the intervening
  // actor acceptance paths, without manufacturing a second resource binding.
  std::optional<bw::game::MissionGameplay> gameplay;
  std::optional<bw::game::MissionInteraction> retirement_interaction;
  if (route == CorpusPauseRoute::retirement) {
    retirement_interaction.emplace(time.control, *time.player, loading, stage,
                                   output, context.audio,
                                   context.profile.help_control(), settings);
  } else {
    gameplay.emplace(time.clock, time.control, *time.player, *time.resources,
                     loading, stage, hud, output, random, context.audio,
                     context.profile.help_control(), settings,
                     bw::game::CockpitActivation::inspection);
  }
  const auto rejects = [](auto operation) {
    bool rejected = false;
    try {
      operation();
    } catch (const std::logic_error&) {
      rejected = true;
    }
    if (!rejected) {
      throw std::runtime_error("Coordinator accepted invalid phase order");
    }
  };
  if (gameplay) {
    rejects([&] { gameplay->advance(); });
    (void)gameplay->begin(0, {});
    rejects([&] { (void)gameplay->begin(0, {}); });
    rejects([&] { gameplay->present(); });
    gameplay->advance();
    rejects([&] { gameplay->advance(); });
    gameplay->present();
  }
  const auto before = time.resources->snapshot();
  const auto world_seconds = time.clock.simulation_seconds();
  const auto pose = stage.player_state()->pose.position;
  constexpr float kFrame = 1.F / 60;
  constexpr std::uint32_t kConfirm = 0x1010, kCancel = 0x2020,
                          kDown = 0x02000000, kHorizontal = 0x04000000;
  bool retirement = false;
  const auto tick = [&](bw::game::MissionInput input) {
    bool ui_consumed = false;
    if (gameplay) {
      const auto disposition = gameplay->begin(kFrame, input);
      retirement |=
          disposition.transition == bw::game::MissionTransition::retire;
      ui_consumed = disposition.ui_consumed;
      gameplay->advance();
      gameplay->present();
    } else {
      const auto disposition = retirement_interaction->admit(kFrame, input);
      retirement |= retirement_interaction->transition_requested() ==
                    bw::game::MissionTransition::retire;
      ui_consumed = disposition.ui_consumed;
      retirement_interaction->present();
    }
    // The host pumps the shared audio service independently of paused actors.
    context.audio.update();
    if (!ui_consumed || time.clock.simulation_seconds() != world_seconds ||
        stage.player_state()->pose.position != pose ||
        time.resources->snapshot().magic != before.magic) {
      throw std::runtime_error(
          "Native pause advanced authoritative world state");
    }
    if (context.sprites.live > bw::render::kMaximumTextures ||
        context.sprites.latest.size() > bw::render::kMaximumSprites) {
      throw std::runtime_error("Native pause and HUD exceed renderer capacity");
    }
  };
  const auto wait = [&](auto complete) {
    constexpr unsigned kMaximumFrames = 240;
    for (unsigned frame = 0; frame < kMaximumFrames && !complete(); ++frame) {
      tick({});
    }
    if (!complete()) {
      throw std::runtime_error("Native pause transition did not complete");
    }
  };
  const auto command = [&](bw::game::MenuInput input) {
    tick({.menu = input});
  };
  const auto root_ready = [&] {
    return output.active == bw::game::MissionPauseView::root &&
           output.root.phase == bw::game::PauseMenuPhase::interactive;
  };
  tick({.pause_pressed = true});
  wait(root_ready);
  if (route == CorpusPauseRoute::retirement) {
    while (output.root.selected != bw::game::PauseMenuAction::retire) {
      command({0, kDown});
    }
    command({kConfirm, 0});
    wait([&] { return output.root.dialog_progress >= 1; });
    // Native dialog also requires a short input delay; use supplied UI time.
    constexpr unsigned kDialogSettleFrames = 30;
    for (unsigned frame = 0; frame < kDialogSettleFrames; ++frame) tick({});
    command({0, kHorizontal});
    command({kConfirm, 0});
    wait([&] { return retirement; });
    if (!time.control.user_paused()) {
      throw std::runtime_error("Retirement resumed live world before teardown");
    }
  } else {
    command({kConfirm, 0});
    wait([&] {
      return output.active == bw::game::MissionPauseView::help &&
             output.help.phase == bw::game::PauseHelpPhase::topics;
    });
    // This corpus enters the explicitly authored completed concept context.
    // Exercise its real coordinator and purchased Help surface together, not
    // an independently manufactured all-topic presentation.
    constexpr unsigned kUnclaimedTopic = 18;
    if (time.player->origin() !=
            bw::game::ProgressionOrigin::development_completed ||
        output.help.count != bw::game::help_topic_count - 1 ||
        output.help.topics[0] != 0 ||
        std::ranges::find(
            std::span(output.help.topics).first(output.help.count),
            kUnclaimedTopic) !=
            std::span(output.help.topics).first(output.help.count).end()) {
      throw std::runtime_error(
          "Native Help lost source-granted completed concept eligibility");
    }
    // Original topic zero contains logical movement and camera markers.
    // Swap only the copied binding observation while keeping the real surface
    // resident: W/S/A/D needs three more glyph quads than the pad movement
    // icon.
    const auto textures_before_icons = context.sprites.live;
    tick({.help_icons = bw::game::help_input_icons(
              bw::game::controller_default_settings(), true)});
    const auto pad_glyphs = context.sprites.latest.size();
    tick({.help_icons = bw::game::help_input_icons(
              bw::game::controller_default_settings(), false)});
    if (context.sprites.live != textures_before_icons ||
        context.sprites.latest.size() != pad_glyphs + 3) {
      throw std::runtime_error(
          "Help logical icons ignored copied keyboard/pad bindings");
    }
    command({kConfirm, 0});
    if (!context.profile.help_control().progress().read[0]) {
      throw std::runtime_error("Native Help did not mark selected topic read");
    }
    // The original Help receiver is fixed UI input. Remapped gameplay keys
    // cannot move its paragraph; a physical wheel impulse can.
    tick({.player = {.movement = {0, -1}}});
    if (output.help.scroll_pixels != 0) {
      throw std::runtime_error("Gameplay movement scrolled native Help");
    }
    tick({.menu = {.vertical_axis = -2}});
    constexpr unsigned kScrollFrames = 30;
    for (unsigned frame = 0; frame < kScrollFrames; ++frame) {
      tick({});
    }
    if (output.help.maximum_scroll > 0 && output.help.scroll_pixels <= 0) {
      throw std::runtime_error("Native Help paragraph did not scroll");
    }
    command({kCancel, 0});
    const auto saved_help = context.profile.help_control().progress();
    command({0, kDown});
    if (output.help.phase != bw::game::PauseHelpPhase::topics ||
        output.help.selected != 1 || output.help.topics[1] != 1) {
      throw std::runtime_error("Native Help cannot select the next topic");
    }
    command({kConfirm, 0});
    if (output.help.phase != bw::game::PauseHelpPhase::reading ||
        !output.help.read[1] ||
        context.profile.help_control().progress().available !=
            saved_help.available ||
        context.profile.help_control().progress().read != saved_help.read) {
      throw std::runtime_error(
          "Native inherited Help reading changed permanent progression");
    }
    command({kCancel, 0});
    command({0, kDown});
    if (output.help.phase != bw::game::PauseHelpPhase::topics ||
        output.help.selected != 2) {
      throw std::runtime_error(
          "Native Help Back did not restore topic navigation");
    }
    command({kCancel, 0});
    wait(root_ready);
    command({0, kDown});
    command({kConfirm, 0});
    wait([&] {
      return output.active == bw::game::MissionPauseView::options &&
             !output.options.opening;
    });
    while (output.options.selected != bw::game::PauseOption::brightness) {
      command({0, kDown});
    }
    command({kConfirm, 0});
    wait([&] { return !output.options.opening; });
    if (output.options.page != bw::game::PauseOptionsPage::brightness) {
      throw std::runtime_error("Native brightness calibration child missing");
    }
    command({kCancel, 0});
    wait([&] {
      return output.options.page == bw::game::PauseOptionsPage::options;
    });
    while (output.options.selected != bw::game::PauseOption::costume) {
      command({0, kDown});
    }
    command({kConfirm, 0});
    if (output.active != bw::game::MissionPauseView::costume ||
        output.costume.page != bw::game::MenuPage::costume) {
      throw std::runtime_error("Native Options costume child missing");
    }
    command({kCancel, 0});
    command({kCancel, 0});
    command({kCancel, 0});
    wait(root_ready);
    command({kCancel, 0});
    command({kConfirm, 0});
    wait([&] { return !time.control.user_paused(); });
    if (output.active != bw::game::MissionPauseView::none || retirement) {
      throw std::runtime_error(
          "Native Resume did not retire pause publication");
    }
  }
  surface.end();
  hud.end();
  if (context.sprites.live != retained_textures) {
    throw std::runtime_error("Native pause child transitions leaked textures");
  }
  std::cerr << "Gameplay coordinator: native pause "
            << (route == CorpusPauseRoute::retirement
                    ? "retirement request"
                    : "Help/Options/costume/Resume")
            << ", world freeze and resource lifetime passed\n";
}

void model_cancellation_substeps(CorpusWorldTime& time,
                                 bw::game::StageInspection& inspection) {
  // Enter a second actual evade, then cross its frame25 cancellation
  // early in a 50ms outer update. A later parent subdivision must observe
  // that event before selecting another body owner, without a GPU flush.
  bool crossed_cancel = false;
  for (unsigned frame = 0; frame < 180; ++frame) {
    time.advance(inspection, static_cast<double>(1.0F / 60.0F),
                 {.movement = {.evade_held = frame == 0}});
    const auto* body =
        inspection.player_animation_state(bw::game::CharacterMotionBank::body);
    if (inspection.player_state()->phase !=
            bw::game::PlayerMovementPhase::landing ||
        body == nullptr) {
      continue;
    }
    const auto clip_frame = body->seconds * body->clip->frames_per_second();
    if (clip_frame >= 23 && clip_frame < 25) {
      time.advance(inspection, static_cast<double>(.05F));
      if (inspection.player_state()->phase !=
          bw::game::PlayerMovementPhase::grounded) {
        throw std::runtime_error(
            "Later parent substep missed completed model cancellation");
      }
      crossed_cancel = true;
      break;
    }
  }
  if (!crossed_cancel) {
    throw std::runtime_error("Missing actual landing cancellation witness");
  }
}
// Exercise actual Stage light publication across the first authored lamp's
// source influence boundary, using ordinary controller input and retained data.
void placed_light_publication(
    const WorldOutput& resources, bw::game::MissionLoading& loading,
    const std::shared_ptr<bw::memory::Domain>& domain) {
  WorldOutput output;
  output.records = resources.records;
  output.model_chunks = resources.model_chunks;
  const auto& stage = loading.stage();
  const auto& sky = loading.sky();
  const auto& background = loading.background();
  if (!stage || !sky || !background) {
    throw std::runtime_error("Placed light witness requires authored scene");
  }
  CorpusWorldTime time;
  bw::game::StageInspection inspection(
      time.clock, time.control, output,
      {*stage, loading.resident_models(), loading.resident_textures()},
      {*sky, loading.sky_resident_models()}, domain, std::nullopt,
      {&*background, loading.resident_dependencies()},
      bw::game::PlayerActorSource{
          loading.resident_player(), loading.resident_weapon(),
          loading.player_support(), loading.weapon_support(),
          kFixtureInitialPose},
      {}, {loading.character_light(), loading.enemy_light()});
  require_initial_local_light_clear(output, loading.resident_player().handle);
  const auto prepared_bytes = domain->snapshot().live_bytes;
  bw::game::StageTraversalInput approach;
  approach.movement.direction = {0, -.5F};
  for (int frame = 0; frame < 90; ++frame) {
    time.advance(inspection, 1.0 / 60, approach);
  }
  const auto body =
      std::ranges::find_if(output.draw_commands, [&](const auto& draw) {
        return draw.model == loading.resident_player().handle;
      });
  if (body == output.draw_commands.end() ||
      body->parameters.local_light_meta[0] == 0 ||
      body->parameters.local_light_meta[0] >
          bw::render::kMaximumWorldLocalLights ||
      domain->snapshot().live_bytes != prepared_bytes) {
    throw std::runtime_error("Approach did not publish prepared source light");
  }
  approach.movement.direction = {0, .5F};
  for (int frame = 0; frame < 180; ++frame) {
    time.advance(inspection, 1.0 / 60, approach);
  }
  require_initial_local_light_clear(output, loading.resident_player().handle);
}

void model_timing_equivalence(
    WorldOutput& world, bw::game::MissionLoading& loading,
    const std::shared_ptr<bw::memory::Domain>& domain,
    const bw::game::PreparedCharacterAudio& prepared_audio) {
  const auto& stage = loading.stage();
  const auto& sky = loading.sky();
  const auto& background = loading.background();
  const auto dependencies = loading.resident_dependencies();
  if (!stage || !sky || !background) {
    throw std::runtime_error(
        "Timing witness requires authored scene resources");
  }
  // Equivalent complete parent/model intervals must expose identical
  // motion/event results whether grouped into a host frame or submitted
  // individually. Both sides use actual stage and animation resources.
  // Each retained scene owns its draw list. The immutable uploaded-resource
  // metadata is shared by value; neither observer owns or retires those assets.
  WorldOutput batched_world, individual_world;
  batched_world.records = individual_world.records = world.records;
  batched_world.model_chunks = individual_world.model_chunks =
      world.model_chunks;
  CorpusAudioOutput batched_audio, individual_audio;
  batched_audio.sealed = individual_audio.sealed = true;
  batched_audio.trace_cues = individual_audio.trace_cues = true;
  bw::game::SourceRandom batched_random, individual_random;
  CorpusWorldTime batched_time, individual_time;
  bw::game::StageInspection batched(
      batched_time.clock, batched_time.control, batched_world,
      {*stage, loading.resident_models(), loading.resident_textures()},
      {*sky, loading.sky_resident_models()}, domain, std::nullopt,
      {&*background, dependencies},
      bw::game::PlayerActorSource{
          loading.resident_player(), loading.resident_weapon(),
          loading.player_support(), loading.weapon_support(),
          kFixtureInitialPose},
      {}, {loading.character_light(), loading.enemy_light()});
  bw::game::StageInspection individual(
      individual_time.clock, individual_time.control, individual_world,
      {*stage, loading.resident_models(), loading.resident_textures()},
      {*sky, loading.sky_resident_models()}, domain, std::nullopt,
      {&*background, dependencies},
      bw::game::PlayerActorSource{
          loading.resident_player(), loading.resident_weapon(),
          loading.player_support(), loading.weapon_support(),
          kFixtureInitialPose},
      {}, {loading.character_light(), loading.enemy_light()});
  batched.bind_audio(prepared_audio, batched_audio, batched_random);
  individual.bind_audio(prepared_audio, individual_audio, individual_random);
  // Both sides start from the original float world publication. Subdivision
  // cannot compare a double-only outer interval against a rounded world frame.
  // These original intervals retain the same coverage. An independently
  // published subdivision can round again at the original float boundary.
  constexpr std::array elapsed{1.0F / 60, 1.0F / 30, .0168F, .031F, .05F, .1F};
  batched_world.pose_publications = individual_world.pose_publications = 0;
  unsigned split_frames = 0, indivisible_frames = 0;
  for (unsigned frame = 0; frame < 120; ++frame) {
    const double seconds = elapsed.at(frame % elapsed.size());
    bw::game::StageTraversalInput input;
    input.movement.direction =
        frame < 60 ? std::array{0.0F, 1.0F} : std::array{0.0F, 0.0F};
    for (auto& slot : batched_audio.slots) slot.active = false;
    for (auto& slot : individual_audio.slots) slot.active = false;
    batched_time.advance(batched, seconds, input);
    // Batching equivalence requires the same intervals at both public
    // boundaries. Balanced actor pieces can contain more precision than a
    // source float world frame (50ms is one such case). Preserve that whole
    // interval in both scenes instead of comparing different quantized inputs.
    bool exact_partition = true;
    double remaining = seconds;
    while (remaining > 0) {
      const auto step = bw::game::player_simulation_step(remaining);
      exact_partition &= step == static_cast<double>(static_cast<float>(step));
      remaining -= step;
    }
    if (exact_partition) {
      ++split_frames;
      remaining = seconds;
      while (remaining > 0) {
        const auto step = bw::game::player_simulation_step(remaining);
        individual_time.advance(individual, step, input);
        remaining -= step;
      }
    } else {
      ++indivisible_frames;
      individual_time.advance(individual, seconds, input);
    }
    const auto* left = batched.player_state();
    const auto* right = individual.player_state();
    const auto* left_body =
        batched.player_animation_state(bw::game::CharacterMotionBank::body);
    const auto* right_body =
        individual.player_animation_state(bw::game::CharacterMotionBank::body);
    if (left->phase != right->phase ||
        left_body->binding != right_body->binding ||
        std::abs(left_body->unwrapped_seconds - right_body->unwrapped_seconds) >
            1e-9 ||
        bw::math::length(bw::math::subtract(left->pose.position,
                                            right->pose.position)) > .00001F ||
        bw::math::length(bw::math::subtract(left->velocity, right->velocity)) >
            .00001F) {
      throw std::runtime_error(
          "Host batching changed complete parent/model root intervals: frame=" +
          std::to_string(frame) + ", seconds=" + std::to_string(seconds) +
          ", phase=" + std::to_string(static_cast<unsigned>(left->phase)) +
          "/" + std::to_string(static_cast<unsigned>(right->phase)) +
          ", binding=" + std::to_string(left_body->binding) + "/" +
          std::to_string(right_body->binding) + ", cursor_ns=" +
          std::to_string(
              (left_body->unwrapped_seconds - right_body->unwrapped_seconds) *
              1e9) +
          ", position=" +
          std::to_string(bw::math::length(
              bw::math::subtract(left->pose.position, right->pose.position))) +
          ", velocity=" +
          std::to_string(bw::math::length(
              bw::math::subtract(left->velocity, right->velocity))));
    }
  }
  if (split_frames == 0 || indivisible_frames == 0 ||
      batched_world.pose_publications != 120 ||
      individual_world.pose_publications <= batched_world.pose_publications) {
    throw std::runtime_error("Model substeps leaked into GPU pose publication");
  }
  if (batched_audio.cue_trace_size == 0 ||
      batched_audio.cue_trace_size != individual_audio.cue_trace_size ||
      !std::ranges::equal(std::span(batched_audio.cue_trace)
                              .first(batched_audio.cue_trace_size),
                          std::span(individual_audio.cue_trace)
                              .first(individual_audio.cue_trace_size)) ||
      batched_random.draws() != individual_random.draws()) {
    throw std::runtime_error(
        "Host batching changed source audio event identities/order");
  }
}
// Extend the provisioned scene with public review requests. E.4 checks failure
// preserves an active preview; MEM.9 checks retained domain footprint across
// consumed frames. Device/audio remain controlled fixtures, not visual proof.
void stage_review_acceptance(
    CorpusWorldTime& time, bw::game::StageInspection& scene,
    bw::game::MissionLoading& loading, WorldOutput& world,
    CorpusAudioOutput& audio,
    const std::shared_ptr<bw::memory::Domain>& domain) {
  if (!time.resources) {
    throw std::logic_error("Actor review requires live resources");
  }
  auto& resource_owner = *time.resources;
  using Kind = bw::game::AnimationReviewKind;
  using Bank = bw::game::CharacterMotionBank;
  auto require = [](bool valid, const char* message) {
    if (!valid) throw std::runtime_error(message);
  };
  auto apply = [&](Kind kind, std::size_t index = 0) {
    const auto result = scene.review({kind, index});
    if (!result.accepted) {
      throw std::runtime_error(std::string("Stage review rejected: ") +
                               std::string(result.message));
    }
  };
  auto run = [&](unsigned frames) {
    time.set_paused(scene, false);
    for (unsigned frame = 0; frame < frames; ++frame) {
      // The controlled output has no sample clock; retire prior voices as a
      // completion observation, while retaining requests/counters for checks.
      for (auto& slot : audio.slots) slot.active = false;
      time.advance(scene, 1.0 / 60.0);
    }
    time.set_paused(scene, true);
  };
  auto clip_index = [&](std::string_view name, bool weapon = false) {
    const auto index = scene.review_catalog(weapon)->find(name);
    if (!index) throw std::runtime_error("Required review publication absent");
    return *index;
  };
  time.set_paused(scene, false);
  require(!scene.review({Kind::shot}).accepted,
          "Running scene admitted a paused menu request");
  run(2);
  require(scene.player_state()->contact.grounded,
          "Review fixture must start on authored ground");
  const auto entry = *scene.player_state();
  auto stop = [&] {
    apply(Kind::stop);
    require(scene.player_state()->pose.position == entry.pose.position &&
                scene.player_state()->orientation == entry.orientation &&
                scene.player_state()->phase == entry.phase &&
                scene.player_state()->velocity == entry.velocity,
            "Stop did not restore the review entry snapshot");
  };
  const auto walk = clip_index("walk_l");
  apply(Kind::body_clip, walk);
  const auto before_memory = domain->snapshot();
  run(12);
  const auto after_memory = domain->snapshot();
  require(scene.player_state()->pose.position == entry.pose.position &&
              scene.player_animation_state(Bank::body)->seconds > 0 &&
              before_memory.live_bytes == after_memory.live_bytes &&
              before_memory.live_allocations == after_memory.live_allocations,
          "Raw clip moved the fixed actor or changed retained storage");
  const auto body_clock = scene.player_animation_state(Bank::body)->seconds;
  const auto body_binding = scene.player_animation_state(Bank::body)->binding;
  apply(Kind::upper_clip, clip_index("reload_shoot_magazine"));
  apply(Kind::additive_clip, clip_index("shoot_stand_fire_add"));
  apply(Kind::weapon_clip, clip_index("stand_l", true));
  require(
      scene.player_animation_state(Bank::body)->seconds == body_clock &&
          scene.player_animation_state(Bank::body)->binding == body_binding &&
          scene.player_animation_state(Bank::upper_body) != nullptr &&
          scene.player_animation_state(Bank::additive) != nullptr,
      "Adding review layers discarded the selected body owner");
  run(2);
  const auto retained_clock = scene.player_animation_state(Bank::body)->seconds;
  for (const auto request :
       {bw::game::AnimationReviewRequest{Kind::body_clip, 10000},
        {Kind::body_clip, clip_index("shoot_stand_fire_add")},
        {Kind::upper_clip, clip_index("shoot_stand_fire_add")},
        {Kind::additive_clip, clip_index("stand_l")},
        {Kind::body_clip, clip_index("turn_around")},
        {Kind::facial, 2},
        {Kind::damage, 12},
        {Kind::death, 11},
        {Kind::spell, 8}}) {
    require(
        !scene.review(request).accepted &&
            scene.player_animation_state(Bank::body)->seconds ==
                retained_clock &&
            scene.player_animation_state(Bank::body)->binding == body_binding,
        "Invalid review request changed the active preview");
  }
  apply(Kind::facial, 0);
  require(scene.player_animation_state(Bank::body)->seconds == retained_clock,
          "Facial preview restarted the body clock");
  run(2);
  stop();
  scene.change_costume(loading, 5);
  apply(Kind::body_clip, walk);
  run(2);
  const auto absent_face_clock =
      scene.player_animation_state(Bank::body)->seconds;
  require(!scene.review({Kind::facial, 0}).accepted &&
              scene.player_animation_state(Bank::body)->seconds ==
                  absent_face_clock &&
              scene.player_animation_state(Bank::body)->binding == body_binding,
          "Absent costume shapes destroyed the active preview");
  stop();
  scene.change_costume(loading, 0);
  apply(Kind::raise_weapon);
  run(45);
  require(scene.player_state()->weapon_raised,
          "Raised weapon preview did not reach raised movement state");
  time.set_paused(scene, false);
  bw::game::StageTraversalInput release_preview_aim;
  release_preview_aim.presentation.aim_mode_pressed = true;
  time.advance(scene, static_cast<double>(1.0F / 60), release_preview_aim);
  run(480);
  require(!scene.player_state()->weapon_raised,
          "Live aim toggle could not release the preview's raised intent");
  apply(Kind::raise_weapon);
  run(45);
  apply(Kind::shot);
  require(scene.player_animation_state(Bank::additive) != nullptr,
          "Shot preview did not submit its authored recoil layer");
  run(4);
  apply(Kind::reload);
  require(scene.player_animation_state(Bank::upper_body) != nullptr &&
              scene.player_animation_state(Bank::weapon) != nullptr,
          "Reload did not reach independent body and weapon owners");
  run(4);
  stop();
  apply(Kind::damage, 0);
  require(scene.player_actions()->kind() == bw::game::AliciaActionKind::damage,
          "Damage preview bypassed the source action owner");
  run(4);
  stop();
  apply(Kind::death, 0);
  require(scene.player_actions()->kind() == bw::game::AliciaActionKind::death,
          "Death preview bypassed the source action owner");
  run(4);
  stop();
  apply(Kind::scripted_jump, 0);
  run(40);
  require(scene.player_state()->pose.position[1] > entry.pose.position[1],
          "Scripted jump preview never left the authored support");
  stop();
  apply(Kind::spell, 7);
  require(scene.player_actions()->kind() == bw::game::AliciaActionKind::magic,
          "Thunder preview did not retain its magic owner");
  const auto aiming = scene.player_animation_state(Bank::body)->binding;
  run(4);
  apply(Kind::next_phase);
  require(scene.player_animation_state(Bank::body)->binding != aiming,
          "Thunder target acknowledgement did not change its animation phase");
  run(4);
  stop();
  require(!scene.review({Kind::next_phase}).accepted,
          "Stopped preview retained a pending spell phase");
  const auto physical = *scene.player_state();
  const auto body_handle = loading.resident_player().handle;
  const auto weapon_handle = loading.resident_weapon().handle;
  world.reject_next_replacement = true;
  bool failed = false;
  try {
    time.change_weapon(scene, loading, 1);
  } catch (const std::runtime_error&) {
    failed = true;
  }
  require(failed && loading.resident_weapon().handle == weapon_handle &&
              loading.current_weapon_class() == 0,
          "Failed live weapon publication changed the current class");
  const auto audio_before = audio.plays;
  for (const unsigned weapon_class : {1U, 2U, 3U, 0U}) {
    const auto camera = scene.camera_state()->pose;
    const std::vector frozen(scene.player_world_pose().begin(),
                             scene.player_world_pose().end());
    time.change_weapon(scene, loading, weapon_class);
    require(loading.current_weapon_class() == weapon_class &&
                loading.resident_player().handle == body_handle &&
                scene.player_state()->pose.position == physical.pose.position &&
                scene.camera_state()->pose.eye == camera.eye &&
                std::ranges::equal(frozen, scene.player_world_pose()),
            "Live weapon publication changed frozen body, position or camera");
    apply(Kind::body_clip, clip_index("walk_l"));
    run(60);
    apply(Kind::reload);
    require(scene.player_animation_state(Bank::weapon)->clip != nullptr,
            "Weapon class did not rebind its prepared motion catalog");
    run(4);
    stop();
  }
  require(audio.plays > audio_before && audio.listener_updates > 0,
          "Audio events did not survive live weapon ownership transfer");
  // The normal B/Q path preserves the active body/event owner and requests
  // the original upper switch, unlike the deliberately neutral menu selector.
  apply(Kind::raise_weapon);
  run(60);
  require(scene.player_state()->weapon_raised,
          "Live switch witness did not establish raised posture");
  time.set_paused(scene, false);
  const auto pause_calls = audio.pause_calls;
  const auto initial_uploads = world.records.size();
  const auto initial_textures = world.texture_identity;
  const auto* retained_body = scene.player_animation_state(Bank::body);
  const auto switch_retained_clock = retained_body->unwrapped_seconds;
  const auto original_weapon = loading.resident_weapon().handle;
  world.reject_next_replacement = true;
  failed = false;
  try {
    time.change_weapon(scene, loading, 1, true);
  } catch (const std::runtime_error&) {
    failed = true;
  }
  require(failed && loading.resident_weapon().handle == original_weapon &&
              loading.current_weapon_class() == 0 &&
              scene.player_animation_state(Bank::body) == retained_body &&
              retained_body->unwrapped_seconds == switch_retained_clock &&
              scene.player_state()->weapon_raised,
          "Failed normal switch disturbed body clocks or raised state");
  for (const unsigned weapon_class : {1U, 2U, 3U, 0U}) {
    const auto* before_body = scene.player_animation_state(Bank::body);
    const auto before_clock = before_body->unwrapped_seconds;
    const auto before_binding = before_body->binding;
    const auto before_pose = scene.player_state()->pose;
    const std::vector frozen(scene.player_world_pose().begin(),
                             scene.player_world_pose().end());
    time.set_paused(scene, false);
    const auto switch_pause_calls = audio.pause_calls;
    time.change_weapon(scene, loading, weapon_class, true);
    require(!scene.paused() && !audio.paused &&
                audio.pause_calls == switch_pause_calls,
            "Ordinary selection paused unrelated simulation or audio");
    const auto* upper = scene.player_animation_state(Bank::upper_body);
    const auto* selected_weapon = scene.player_animation_state(Bank::weapon);
    require(scene.player_animation_state(Bank::body) == before_body &&
                before_body->unwrapped_seconds == before_clock &&
                before_body->binding == before_binding &&
                scene.player_state()->weapon_raised &&
                scene.player_state()->pose.position == before_pose.position &&
                std::ranges::equal(frozen, scene.player_world_pose()) &&
                upper != nullptr &&
                upper->binding == (0x130U | (before_binding & 0xfU)) &&
                selected_weapon != nullptr && selected_weapon->binding == 2,
            "Normal switch did not preserve body and publish raised switch "
            "animation");
    const auto switch_generation = upper->generation;
    run(4);
    upper = scene.player_animation_state(Bank::upper_body);
    require(upper != nullptr && upper->generation == switch_generation &&
                upper->seconds > 0 &&
                scene.player_animation_state(Bank::body)->binding ==
                    upper->binding &&
                std::abs(scene.player_animation_state(Bank::body)->seconds -
                         upper->seconds) < 1e-6,
            "Idle body did not follow the advancing source switch upper phase");
    run(80);
  }
  require(world.records.size() == initial_uploads &&
              world.texture_identity == initial_textures &&
              audio.pause_calls >= pause_calls,
          "Repeated live selection uploaded immutable assets");
  time.set_paused(scene, true);
  stop();
  const auto lowered_body = scene.player_animation_state(Bank::body)->binding;
  time.change_weapon(scene, loading, 1, true);
  require(
      !scene.player_state()->weapon_raised &&
          scene.player_animation_state(Bank::weapon)->binding == 1 &&
          scene.player_animation_state(Bank::upper_body)->binding ==
              (0x130U | (lowered_body & 0xfU)),
      "Lowered normal switch selected the wrong weapon rest or upper motion");
  run(80);
  time.change_weapon(scene, loading, 0);

  // The real raise/turn/fire sequence must retain grounded contact when its
  // authored root interval contains a positive vertical component. Ballistic
  // ascent remains covered separately by the scripted-jump traversal above.
  run(30);
  time.set_paused(scene, false);
  time.advance(scene, 1.0 / 60.0, {.presentation = {.aim_mode_pressed = true}});
  auto grounded_frames = [&](unsigned count,
                             bw::game::StageTraversalInput input) {
    for (unsigned frame = 0; frame < count; ++frame) {
      for (auto& slot : audio.slots) slot.active = false;
      time.advance(scene, 1.0 / 60.0, input);
      require(scene.player_state()->contact.grounded &&
                  scene.player_state()->phase ==
                      bw::game::PlayerMovementPhase::grounded,
              "Stationary raised turn/fire lost authored floor support");
    }
  };
  grounded_frames(89, {});
  grounded_frames(25, {.look = {0, 1}});
  grounded_frames(30, {});
  grounded_frames(50, {.look = {0, -1}});
  grounded_frames(30, {});
  grounded_frames(25, {.look = {0, 1}});
  grounded_frames(30, {});
  const auto ammunition_before_fire =
      resource_owner.snapshot().weapons.at(0).ammunition;
  grounded_frames(30, {.look = {1, 0}, .presentation = {.fire_trigger = 1}});
  grounded_frames(30, {.look = {-1, 0}, .presentation = {.fire_trigger = 1}});
  grounded_frames(25, {.look = {0, 1}, .presentation = {.fire_trigger = 1}});
  grounded_frames(50, {.look = {0, -1}, .presentation = {.fire_trigger = 1}});
  require(resource_owner.snapshot().weapons.at(0).ammunition <
              ammunition_before_fire,
          "Live fire sequence did not consume authoritative ammunition");
  // Stop is scoped to an existing preview, never a general player reset.
  // The preceding live turn/fire path has no preview session to retire.
  time.set_paused(scene, true);
  const auto inactive_clock = scene.player_animation_state(Bank::body)->seconds;
  const auto inactive_state = *scene.player_state();
  apply(Kind::stop);
  require(
      scene.targeting_state().selected() &&
          scene.player_state()->velocity == inactive_state.velocity &&
          scene.player_state()->weapon_raised == inactive_state.weapon_raised &&
          scene.player_animation_state(Bank::body)->seconds == inactive_clock,
      "Inactive Stop reset ordinary player ownership");
  time.set_paused(scene, false);
  std::cerr
      << "Stage review: paused admission, layers, rollback, action phases, "
         "all4 live weapon classes and resumed audio passed\n";
}

// Exercise the reported live action boundary through Stage admission, rather
// than selecting melee directly from a neutral pose. Existing real assets and
// event/audio owners remain provisioned across each interruption.
void lower_melee_acceptance(CorpusWorldTime& time,
                            bw::game::StageInspection& scene,
                            bw::game::MissionLoading& loading,
                            CorpusAudioOutput& audio) {
  using Bank = bw::game::CharacterMotionBank;
  const auto require = [](bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
  };
  const auto tick = [&](bw::game::StageTraversalInput input = {}) {
    for (auto& slot : audio.slots) slot.active = false;
    time.advance(scene, static_cast<double>(1.0F / 60), input);
  };
  for (unsigned weapon = 0; weapon < 4; ++weapon) {
    time.set_paused(scene, true);
    time.change_weapon(scene, loading, weapon);
    time.set_paused(scene, false);
    // Same-class selection is intentionally a no-op; the preceding live test
    // may still own target mode. Exit it through its real input before setup.
    if (scene.targeting_state().selected()) {
      tick({.presentation = {.aim_mode_pressed = true}});
    }
    for (unsigned frame = 0; frame < 60; ++frame) tick();
    tick({.presentation = {.aim_mode_pressed = true}});
    for (unsigned frame = 0; frame < 90; ++frame) tick();
    tick({.presentation = {.aim_mode_pressed = true}});
    bool lowering = false;
    for (unsigned frame = 0; frame < 700; ++frame) {
      tick();
      const auto* upper = scene.player_animation_state(Bank::upper_body);
      if (upper != nullptr && (upper->binding & ~std::size_t{15}) == 0x270) {
        lowering = true;
        break;
      }
    }
    require(lowering && !scene.targeting_state().selected() &&
                !scene.targeting_state().camera_targeted(),
            "RS release failed to reach independent source lowering phase");
    tick({.presentation = {.attack_pressed = true}});
    const auto* body = scene.player_animation_state(Bank::body);
    require(body != nullptr && body->binding == 706 &&
                scene.player_animation_state(Bank::upper_body) == nullptr &&
                !scene.player_state()->weapon_raised &&
                !scene.targeting_state().selected(),
            "Melee retained the lowering layer or raised/targeting owner");
    tick({.presentation = {.aim_mode_pressed = true}});
    require(!scene.targeting_state().selected(),
            "Melee admitted the ordinary Shooter targeting input receiver");
    for (unsigned frame = 0; frame < 100; ++frame) tick();
    // Entry while still targeting must consume the same forced reset message,
    // not rely on the earlier natural expiry having already cleared posture.
    tick({.presentation = {.aim_mode_pressed = true}});
    for (unsigned frame = 0; frame < 90; ++frame) tick();
    require(scene.player_state()->weapon_raised &&
                scene.targeting_state().selected(),
            "Targeted melee setup failed");
    tick({.presentation = {.attack_pressed = true}});
    require(!scene.player_state()->weapon_raised &&
                !scene.targeting_state().selected() &&
                !scene.targeting_state().camera_targeted() &&
                scene.player_animation_state(Bank::upper_body) == nullptr,
            "Melee omitted the forced source posture/camera reset");
    for (unsigned frame = 0; frame < 100; ++frame) tick();
  }
  time.set_paused(scene, true);
  time.change_weapon(scene, loading, 0);
  time.set_paused(scene, false);
  std::cerr << "Live melee: all four lowering interruptions and forced "
               "target/posture exits passed\n";
}

void grounded_action_exit_acceptance(CorpusWorldTime& time,
                                     bw::game::StageInspection& scene,
                                     bw::game::MissionLoading& loading,
                                     CorpusAudioOutput& audio) {
  using bw::game::AcceptedDeathPresentation;
  using bw::game::AliciaActionOutcome;
  using bw::game::AliciaActionUpdate;
  using bw::game::CharacterMotionBank;
  using bw::game::SpellPresentationKind;
  using bw::game::StageTraversalInput;
  const auto require = [](bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
  };
  const auto tick = [&](StageTraversalInput input = {}) {
    for (auto& slot : audio.slots) slot.active = false;
    time.advance(scene, static_cast<double>(1.0F / 60), input);
  };
  // These are accepted external messages, not simulated combat outcomes.
  // A different paused class selection gives each trial neutral ownership.
  for (unsigned family = 0; family < 4; ++family) {
    time.set_paused(scene, true);
    time.change_weapon(scene, loading, (family + 1) % 4);
    time.set_paused(scene, false);
    for (unsigned frame = 0; frame < 60; ++frame) tick();
    tick({.presentation = {.aim_mode_pressed = true}});
    for (unsigned frame = 0; frame < 90; ++frame) tick();
    tick({.presentation = {.aim_mode_pressed = true}});
    bool lowering = false;
    for (unsigned frame = 0; frame < 700; ++frame) {
      tick();
      const auto* upper =
          scene.player_animation_state(CharacterMotionBank::upper_body);
      if (upper != nullptr && (upper->binding & ~std::size_t{15}) == 0x270) {
        lowering = true;
        break;
      }
    }
    require(lowering, "Grounded action exit setup did not reach lowering");
    auto& actions = *scene.player_actions();
    AliciaActionUpdate admitted;
    switch (family) {
      case 0:
        admitted = actions.begin_damage({.direction = {.incoming = {0, 0, 1}}});
        break;
      case 1:
        admitted = actions.begin_magic({.slot = 788,
                                        .complete_with_motion = true,
                                        .shooter_update = false});
        break;
      case 2:
        admitted = actions.begin_spell(SpellPresentationKind::thunder);
        break;
      default: {
        AcceptedDeathPresentation death;
        death.direction.incoming = {0, 0, 1};
        death.positive_weights = {.1F, .1F, .1F};
        death.nonpositive_weights = {.1F, .1F, .1F, .1F, .1F, .1F};
        death.random_unit = .5F;
        admitted = actions.begin_death(death);
        break;
      }
    }
    require(admitted.outcome == AliciaActionOutcome::started &&
                scene.player_animation_state(CharacterMotionBank::upper_body) ==
                    nullptr,
            "Ordinary state exit retained upper during accepted action entry");
    tick({.action = {.death_handoff_blocked = true,
                     .spell = {.camera_owned = true}}});
    if (family == 0 || family == 3) {
      require(!scene.player_state()->weapon_raised &&
                  !scene.targeting_state().selected() &&
                  !scene.targeting_state().camera_targeted(),
              "Damage/death missed their forced source Shooter entry message");
    }
  }
  for (unsigned forced = 0; forced < 2; ++forced) {
    time.set_paused(scene, true);
    time.change_weapon(scene, loading, forced + 1);
    time.set_paused(scene, false);
    for (unsigned frame = 0; frame < 60; ++frame) tick();
    tick({.presentation = {.aim_mode_pressed = true}});
    for (unsigned frame = 0; frame < 90; ++frame) tick();
    require(scene.targeting_state().selected() &&
                scene.player_state()->weapon_raised,
            "Forced damage/death entry setup did not retain targeting");
    AliciaActionUpdate admitted;
    if (forced == 0) {
      admitted = scene.player_actions()->begin_damage(
          {.direction = {.incoming = {0, 0, 1}}});
    } else {
      AcceptedDeathPresentation death;
      death.direction.incoming = {0, 0, 1};
      death.positive_weights = {.1F, .1F, .1F};
      death.nonpositive_weights = {.1F, .1F, .1F, .1F, .1F, .1F};
      death.random_unit = .5F;
      admitted = scene.player_actions()->begin_death(death);
    }
    require(admitted.outcome == AliciaActionOutcome::started,
            "Targeted damage/death entry was not admitted");
    require(!scene.targeting_state().selected() &&
                !scene.targeting_state().camera_targeted() &&
                !scene.player_state()->weapon_raised,
            "Accepted entry did not deliver its effects synchronously");
    const auto generation =
        scene.player_animation_state(CharacterMotionBank::body)->generation;
    time.advance(scene, 0);
    time.set_paused(scene, true);
    time.advance(scene, 1.0 / 60);
    time.set_paused(scene, false);
    require(!scene.targeting_state().selected() &&
                scene.player_animation_state(CharacterMotionBank::body)
                        ->generation == generation,
            "Zero-time/paused publication changed the accepted entry");
    tick({.action = {.death_handoff_blocked = true}});
    require(!scene.targeting_state().selected() &&
                !scene.targeting_state().camera_targeted() &&
                !scene.player_state()->weapon_raised,
            "Source forced entry retained live targeting or raised posture");
    tick({.action = {.death_handoff_blocked = true},
          .presentation = {.fire_trigger = 1, .aim_mode_pressed = true}});
    require(!scene.targeting_state().selected() &&
                !scene.targeting_state().camera_targeted() &&
                !scene.player_state()->weapon_raised &&
                scene.player_animation_state(CharacterMotionBank::upper_body) ==
                    nullptr,
            "Exclusive action consumed ordinary targeting/fire input");
  }
  time.set_paused(scene, true);
  time.change_weapon(scene, loading, 0);
  time.set_paused(scene, false);
  for (unsigned frame = 0; frame < 60; ++frame) tick();
  tick({.presentation = {.aim_mode_pressed = true}});
  for (unsigned frame = 0; frame < 90; ++frame) tick();
  require(scene.player_actions()
                  ->begin_damage({.direction = {.incoming = {0, 0, 1}}})
                  .outcome == AliciaActionOutcome::started,
          "Multiple accepted entries fixture was not admitted");
  require(!scene.targeting_state().selected() &&
              !scene.player_state()->weapon_raised,
          "Damage entry effects were deferred");
  const auto replacement = scene.player_actions()->begin_magic(
      {.slot = 788, .shooter_update = false});
  require(replacement.outcome == AliciaActionOutcome::started &&
              !scene.targeting_state().selected() &&
              !scene.player_state()->weapon_raised,
          "Same-frame replacement lost accepted entry effects or was rejected");
  static_cast<void>(scene.player_actions()->interrupt());
  time.advance(scene, 0);
  require(!scene.targeting_state().selected() &&
              !scene.player_state()->weapon_raised,
          "Interruption undid source entry effects before a model step");
  // Gatling's source .75-second posture lasts less than either spell entry.
  // Check actual typed state registrations while their body actions remain
  // active.
  for (unsigned timed = 0; timed < 2; ++timed) {
    time.set_paused(scene, true);
    time.change_weapon(scene, loading, 0);
    time.change_weapon(scene, loading, 3);
    time.set_paused(scene, false);
    for (unsigned frame = 0; frame < 60; ++frame) tick();
    tick({.presentation = {.aim_mode_pressed = true}});
    for (unsigned frame = 0; frame < 90; ++frame) tick();
    tick({.presentation = {.aim_mode_pressed = true}});
    require(
        !scene.targeting_state().selected() &&
            scene.targeting_state().camera_targeted(),
        "Spell camera fixture needs source pending ordinary-context update");
    auto& actions = *scene.player_actions();
    const auto admitted =
        actions.begin_spell(timed == 0 ? SpellPresentationKind::ancient_wall
                                       : SpellPresentationKind::raven_panic);
    require(admitted.outcome == AliciaActionOutcome::started,
            "Timed spell ownership fixture was not admitted");
    for (unsigned frame = 0; frame < 60; ++frame) {
      tick({.action = {.spell = {.camera_owned = true}}});
      require(scene.player_animation_state(CharacterMotionBank::upper_body) ==
                  nullptr,
              "Exclusive posture countdown published an ordinary lower clip");
    }
    require(scene.player_state()->weapon_raised == (timed == 0) &&
                scene.targeting_state().camera_targeted() == (timed == 0),
            "Source spell timed-update registration was not preserved");
  }
  time.set_paused(scene, true);
  time.change_weapon(scene, loading, 1);
  time.set_paused(scene, false);
  for (unsigned frame = 0; frame < 60; ++frame) tick();
  tick({.presentation = {.aim_mode_pressed = true}});
  for (unsigned frame = 0; frame < 90; ++frame) tick();
  tick({.movement = {.evade_held = true}});
  require(!scene.targeting_state().selected() &&
              !scene.targeting_state().camera_targeted() &&
              !scene.player_state()->weapon_raised &&
              scene.player_animation_state(CharacterMotionBank::upper_body) ==
                  nullptr,
          "Dodge entry omitted ordinary exit or forced Shooter effects");
  tick({.presentation = {.fire_trigger = 1, .aim_mode_pressed = true}});
  require(!scene.targeting_state().selected() &&
              !scene.player_state()->weapon_raised &&
              scene.player_animation_state(CharacterMotionBank::upper_body) ==
                  nullptr,
          "Dodge consumed ordinary Shooter input");
  const auto* body = scene.player_animation_state(CharacterMotionBank::body);
  const auto generation = body->generation;
  const auto rejected = scene.player_actions()->begin_damage(
      {.direction = {.incoming = {0, 0, 1}}});
  require(
      rejected.outcome == AliciaActionOutcome::unsupported_source_state &&
          scene.player_animation_state(CharacterMotionBank::body)->generation ==
              generation,
      "Grounded-only action admission altered an airborne source owner");
  for (unsigned frame = 0; frame < 180; ++frame) tick();
  time.set_paused(scene, true);
  time.change_weapon(scene, loading, 0);
  time.set_paused(scene, false);
  std::cerr << "Grounded action exit: damage, common magic, Thunder, death and "
               "airborne rejection passed\n";
}

class CorpusAudioFactory final : public bw::game::GameAudioFactory {
 public:
  std::shared_ptr<CorpusAudioOutput> output;
  std::shared_ptr<bw::audio::CueOutput> create(
      std::span<const std::uint8_t> settings, bw::audio::Limits,
      const std::shared_ptr<bw::memory::Domain>& domain) override {
    if (settings.size() < 4) throw std::runtime_error("Missing original XGS");
    output = std::allocate_shared<CorpusAudioOutput>(
        bw::memory::Allocator<CorpusAudioOutput>(domain));
    return output;
  }
};
class CorpusAudioEnvironment final
    : public bw::game::CharacterAudioEnvironment {
 public:
  bw::game::SourceRandom random;
  bw::game::CharacterSoundEmission sound_emission(
      bw::game::SoundMotionCommand) const override {
    return {{0, 0, 1}, 3, 0, false};
  }
  bw::math::Vector3<float> bone_position(std::int32_t) const override {
    return {1, 0, 1};
  }
  bw::math::Vector3<float> voice_position() const override { return {0, 0, 1}; }
  std::uint32_t random_below(std::uint32_t upper) override {
    return random.next(upper);
  }
};
void character_audio_presentation(
    bw::resources::ResourceService& resources, const bw::platform::Files& files,
    const std::filesystem::path& sound_directory) {
  using bw::animation::MotionEventPhase;
  using bw::game::CharacterAudioResult;
  const auto require_audio = [](bool valid) {
    if (!valid) {
      throw std::runtime_error("Original character audio policy mismatch");
    }
  };
  CorpusAudioFactory factory;
  unsigned progress = 0;
  bw::game::MissionAudio mission(files, resources.catalogs(), sound_directory,
                                 resources.memory_domain(), factory,
                                 {3, 8, 50'000'000}, [&] { ++progress; });
  require_audio(progress == 3 && factory.output->registrations == 3 &&
                factory.output->sealed);
  std::size_t voices = 0, variations = 0;
  for (std::uint32_t action = 0; action < bw::game::kVoiceActions; ++action) {
    const auto* voice = mission.prepared().voice(action);
    if (voice == nullptr) continue;
    ++voices;
    for (const auto& variant : voice->variants) {
      if (variant.weight == 0) continue;
      require_audio(variant.available);
      ++variations;
    }
  }
  CorpusAudioEnvironment environment;
  bw::game::CharacterAudio owner(mission.prepared(), *mission.output());
  bw::game::CharacterAudioChannel body(
      owner, bw::game::CharacterAudioBank::body, 1, resources.memory_domain());
  bw::game::CharacterAudioChannel upper(
      owner, bw::game::CharacterAudioBank::upper, 1, resources.memory_domain());
  const auto before = resources.memory_domain()->snapshot();
  require_audio(body.voice({8}, environment) == CharacterAudioResult::played);
  const auto first_voice = factory.output->slots.at(0).request.cue;
  require_audio(upper.voice({8}, environment) ==
                CharacterAudioResult::suppressed);
  require_audio(owner.stop_voice() == CharacterAudioResult::stopped);
  require_audio(body.voice({8}, environment) == CharacterAudioResult::played);
  require_audio(factory.output->slots.at(0).request.cue != first_voice);
  require_audio(body.voice({0}, environment) == CharacterAudioResult::played);
  require_audio(body.voice({2}, environment) ==
                CharacterAudioResult::unavailable);
  require_audio(owner.stop_voice() == CharacterAudioResult::stopped);
  bw::animation::MotionEventContext context{0, 0, 0, {0, 20, {}}, 0, 1};
  require_audio(body.sound({0, 10}, MotionEventPhase::enter, context,
                           environment) == CharacterAudioResult::played);
  require_audio(factory.output->slots.at(0).request.cue == 0x1c4);
  require_audio(body.sound({2039, 0}, MotionEventPhase::enter, context,
                           environment) == CharacterAudioResult::suppressed);
  require_audio(upper.sound({2039, 0}, MotionEventPhase::enter, context,
                            environment) == CharacterAudioResult::played);
  require_audio(body.sound({1110, 10}, MotionEventPhase::enter, context,
                           environment) == CharacterAudioResult::played);
  require_audio(body.sound({1110, 10}, MotionEventPhase::enter, context,
                           environment) == CharacterAudioResult::capacity);
  require_audio(upper.sound({1110, 10}, MotionEventPhase::enter, context,
                            environment) == CharacterAudioResult::played);
  require_audio(body.sound({1110, 10}, MotionEventPhase::update, context,
                           environment) == CharacterAudioResult::updated);
  require_audio(factory.output->positions == 1);
  const auto stops = factory.output->stops;
  require_audio(body.reset() == CharacterAudioResult::stopped &&
                factory.output->stops == stops + 1);
  require_audio(upper.sound({1110, 10}, MotionEventPhase::exit, context,
                            environment) == CharacterAudioResult::stopped &&
                factory.output->stops == stops + 2);
  context.current_frame = 14;
  require_audio(body.sound({1110, 10}, MotionEventPhase::enter, context,
                           environment) == CharacterAudioResult::suppressed);
  const auto after = resources.memory_domain()->snapshot();
  require_audio(before.live_bytes == after.live_bytes &&
                before.live_allocations == after.live_allocations);
  std::cerr << "Prepared Alicia audio: " << voices << " voice actions, "
            << variations
            << " weighted variants; priority, history, surface and scoped "
               "lifetime passed\n";
}
// Existing provisioned corpus: prepare every common/voice wave through the
// pinned shared codec; range reads must remain repeatable at loop and EOF
// edges. E.6 scopes bank/sample ownership, MEM.9 keeps native sample reads
// allocation-free.
void prepared_game_audio(bw::resources::ResourceService& resources,
                         const bw::platform::Files& files,
                         const std::filesystem::path& sound_directory) {
  character_audio_presentation(resources, files, sound_directory);
  constexpr std::size_t kSampleBudget = 47105024, kExpectedWaves = 868;
  constexpr std::size_t kProbeFrames = 128;
  std::size_t count = 0, total = 0;
  for (const auto name : {"bw_common", "voice"}) {
    const auto selected =
        resources.catalogs().resolve(std::string("audio.soundbank.") + name);
    const auto& mount =
        resources.catalogs().catalogs().at(selected.catalog_index);
    const auto& resource =
        mount.catalog.resources().at(selected.resource_index);
    const auto archive = bw::content::open_archive(
        files, bw::content::archive_path(files, mount, resource.raw.at(3)),
        resources.memory_domain());
    for (std::size_t index = 0; index < archive.records().size(); ++index) {
      const auto& record = archive.records().at(index);
      if (record.type != "USER" || !record.name.ends_with(".xwb")) continue;
      // A publication archive may hold several banks; match the selected name.
      if (bw::resources::normalize_name(record.name) !=
          std::string(name) + ".xwb") {
        continue;
      }
      const auto bank = bw::formats::WaveBankDefinition::parse(
          archive.payload(index), resources.memory_domain());
      for (const auto& wave : bank.waves()) {
        const auto pcm = bw::audio::PreparedWave::decode(
            wave, kSampleBudget - total, resources.memory_domain());
        total += pcm.samples().size();
        ++count;
        const auto frames = std::min(kProbeFrames, pcm.frames());
        std::vector<float> first(frames * pcm.channels()),
            repeated(first.size());
        for (const auto offset :
             {std::size_t{0}, pcm.frames() - frames,
              std::min<std::size_t>(wave.loop_start, pcm.frames() - frames)}) {
          const auto before = resources.memory_domain()->snapshot();
          if (!pcm.read(offset, first) || !pcm.read(offset, repeated) ||
              first != repeated || pcm.read(pcm.frames() + 1, repeated)) {
            throw std::runtime_error(
                "Prepared game audio range/loop contract mismatch");
          }
          const auto after = resources.memory_domain()->snapshot();
          if (before.live_bytes != after.live_bytes ||
              before.live_allocations != after.live_allocations) {
            throw std::runtime_error(
                "Prepared audio read changed retained storage");
          }
        }
      }
    }
  }
  if (count != kExpectedWaves || total != kSampleBudget) {
    throw std::runtime_error("Original common/voice decode census mismatch");
  }
  std::cerr << "Prepared common/voice audio: " << count << " waves, " << total
            << " samples\n";
}
// Exercise complete weapon-local animation blends against the source's
// identity-root attachment policy, including actual receiver admission. This
// is the existing original-content scenario, not a visual grip-quality oracle.
void discharge_attachment_trajectory(
    const bw::game::PlayerActorSource& source,
    const bw::game::MissionPlayerState& mission,
    const bw::game::PlayerResourceSettings& settings,
    const std::shared_ptr<bw::memory::Domain>& domain) {
  constexpr double kStep = 1.0 / 60;
  constexpr std::array<std::string_view, 7> kWeaponSlots{
      "defaultmotion", "stand_l",   "shoot_l",    "stand_reload",
      "shoot_reload",  "shoot_end", "shoot_after"};
  const auto& model = source.weapon.definition.get();
  const auto& names = source.body.definition.get().bone_names();
  const auto hand = std::ranges::find(names, "RightWeapon",
                                      &bw::formats::ModelBoneName::name);
  const auto support =
      std::ranges::find(names, "LeftHand", &bw::formats::ModelBoneName::name);
  if (hand == names.end() || support == names.end()) {
    throw std::runtime_error("Discharge fixture lacks hand bindings");
  }
  for (const auto stance :
       {bw::game::AliciaStance::standing, bw::game::AliciaStance::crouched}) {
    for (const float movement : {0.0F, .5F, 1.0F}) {
      for (const bool aiming : {false, true}) {
        bw::game::PlayerActor actor(source, domain);
        bw::world::WorldClock clock;
        bw::game::PlayerResources resources(settings, mission, clock);
        auto actions = resources.actions();
        bw::animation::PlaybackRing weapon_clock(4, domain);
        bw::animation::SkeletonPose expected(*model.skeleton(), domain);
        const auto& catalog = actor.weapon_animations().motions();
        const auto request_weapon =
            [&](const bw::game::ActionMotionRequest& request) {
              const auto clip = catalog.find(kWeaponSlots.at(request.slot));
              if (!clip) return;
              weapon_clock.request({.clip = &catalog.clip(*clip),
                                    .binding = request.slot,
                                    .blend_seconds = request.blend_seconds,
                                    .fresh = request.fresh});
            };
        unsigned rest = 1;
        request_weapon({.bank = bw::game::ActionMotionBank::weapon,
                        .slot = rest,
                        .fresh = true});
        const auto apply = [&](const bw::game::ActionMotionPlan& plan) {
          (void)actor.apply_action_plan(plan);
          if (plan.weapon_rest_slot) rest = *plan.weapon_rest_slot;
          for (const auto& request : plan.motions()) {
            if (request.bank == bw::game::ActionMotionBank::weapon) {
              request_weapon(request);
            }
          }
        };
        unsigned accepted_shots = 0;
        for (unsigned frame = 0; frame < 210; ++frame) {
          resources.begin_frame(clock.publish(static_cast<float>(kStep), {}));
          const bool raised = frame >= 30 && frame < 165;
          const auto* body =
              actor.animation_state(bw::game::CharacterMotionBank::body);
          if (frame == 30 || frame == 165) {
            apply(bw::game::change_weapon_raised(
                stance, static_cast<std::uint32_t>(body->binding), !raised,
                raised));
          }
          if (frame >= 60 && frame < 150 &&
              !actor.animation_state(
                  bw::game::CharacterMotionBank::upper_body) &&
              actions.try_fire().status == bw::game::ShotStatus::accepted) {
            apply(bw::game::accepted_shot(
                {stance, static_cast<std::uint32_t>(body->binding),
                 mission.selected_weapon(), true}));
            ++accepted_shots;
          }
          bw::game::GroundMotionInput input{.stance = stance,
                                            .magnitude = movement,
                                            .movement_world = {0, 0, 1},
                                            .weapon_raised = raised};
          if (const auto* upper = actor.animation_state(
                  bw::game::CharacterMotionBank::upper_body)) {
            input.upper_slot = static_cast<std::uint32_t>(upper->binding);
            input.upper_sampled_seconds = upper->seconds;
          }
          bw::game::ActionMotionPlan ground;
          ground.requests[0] = bw::game::select_ground_motion(input).motion;
          ground.count = 1;
          apply(ground);
          actor.set_aim_posture({0, 0, 1}, aiming && raised);
          const auto& current =
              weapon_clock.state(*weapon_clock.current_slot());
          if (const auto completed = bw::game::completed_weapon_motion(
                  current.clip->repeating(), current.completed, rest)) {
            request_weapon(*completed);
          }
          weapon_clock.advance(kStep);
          (void)actor.advance_motion(kStep);
          actor.complete_model_step(source.placement,
                                    bw::animation::kIdentityMatrix, .5F);
          expected.evaluate_layers({weapon_clock.layers(), {}},
                                   actor.body_bones()[hand->bone],
                                   bw::animation::RootPosePolicy::identity);
          for (std::size_t draw = 0; draw < model.draws().size(); ++draw) {
            const auto palette = actor.draw_palette(true, draw);
            const auto& material =
                model.materials().at(model.draws().at(draw).material);
            for (const auto& binding : material.matrices) {
              const auto& reference =
                  expected.skin_transforms()[binding.source_matrix];
              const auto& actual = palette[binding.destination_matrix];
              for (std::size_t row = 0; row < 4; ++row) {
                for (std::size_t column = 0; column < 4; ++column) {
                  if (std::abs(actual.at(row).at(column) -
                               reference.at(column).at(row)) > .00001F) {
                    throw std::runtime_error(
                        "Discharge applies weapon root twice");
                  }
                }
              }
            }
          }
          // Finite combined hand observations detect corrupt output, without
          // inventing a fixed grip invariant for authored recoil motion.
          const auto inverse =
              bw::math::inverse_affine(actor.body_bones()[hand->bone]);
          if (!inverse || !bw::math::all_finite(bw::math::multiply(
                              actor.body_bones()[support->bone], *inverse))) {
            throw std::runtime_error("Discharge hand relationship is singular");
          }
          weapon_clock.finish_frame();
          resources.finish_frame();
        }
        if (accepted_shots == 0) {
          throw std::runtime_error("Discharge trajectory never admitted fire");
        }
      }
    }
  }
}

// The authorized support-arm improvement consumes real weapon control points.
// Verify its world-space binding and the single publication shared with cloth;
// this deliberately tests the new constraint, not parity with the empty PC IK.
void support_arm_content_contracts(
    const bw::game::ResidentModelView& body_source,
    const bw::game::ResidentModelView& weapon_source,
    const std::shared_ptr<bw::memory::Domain>& domain) {
  const auto& body_model = body_source.definition.get();
  const auto& weapon_model = weapon_source.definition.get();
  const auto& body_skeleton = body_model.skeleton();
  const auto& weapon_skeleton = weapon_model.skeleton();
  if (!body_skeleton || !weapon_skeleton) {
    throw std::runtime_error("Support-arm fixture requires both skeletons");
  }
  bw::animation::SkeletonPose body(*body_skeleton, domain);
  bw::animation::SkeletonPose weapon(*weapon_skeleton, domain);
  body.bind_pose(bw::animation::kIdentityMatrix);
  const auto& names = body_model.bone_names();
  const auto index = [&](std::string_view name) {
    const auto found =
        std::ranges::find(names, name, &bw::formats::ModelBoneName::name);
    if (found == names.end()) {
      throw std::runtime_error("Missing support-arm bone");
    }
    return found->bone;
  };
  const std::array chain{index("LeftArm"), index("LeftForeArm"),
                         index("LeftHand")};
  weapon.bind_pose(body.bone_transforms()[index("RightWeapon")]);
  bw::game::AliciaSupportArm arm(
      {.body = body_model,
       .weapon = weapon_model,
       .weapon_control_points = weapon_source.control_points},
      domain);
  bw::game::AliciaSecondaryMotion secondary(body_model,
                                            body_source.control_points, domain);
  secondary.reset(body);
  const auto targets = bw::formats::ControlPointDefinition::parse(
      weapon_source.control_points, weapon_skeleton->bones().size(), domain);
  bw::memory::Vector<bw::animation::Matrix> target_frames(
      targets.frames().size(), bw::animation::kIdentityMatrix,
      bw::memory::Allocator<bw::animation::Matrix>(domain));
  bw::animation::evaluate_control_points(targets, weapon.skin_transforms(),
                                         target_frames);
  const auto before = domain->snapshot();
  if (arm.evaluate({.body = body, .weapon = weapon}, 0)) {
    throw std::runtime_error("Zero grip weight must not publish overrides");
  }
  const auto originals = std::array{body.bone_transforms()[chain[0]],
                                    body.bone_transforms()[chain[1]],
                                    body.bone_transforms()[chain[2]]};
  const auto solved = arm.evaluate({.body = body, .weapon = weapon}, 1);
  if (!solved) {
    throw std::runtime_error("Authored support targets did not solve");
  }
  const auto target = bw::math::translation(target_frames.at(100));
  const auto root = bw::math::translation(originals[0]);
  const auto upper_length = bw::math::length(
      bw::math::subtract(bw::math::translation(originals[1]), root));
  const auto lower_length =
      bw::math::length(bw::math::subtract(bw::math::translation(originals[2]),
                                          bw::math::translation(originals[1])));
  const auto distance = bw::math::length(bw::math::subtract(target, root));
  const auto expected_distance =
      std::clamp(distance, std::abs(upper_length - lower_length),
                 upper_length + lower_length);
  const auto expected_hand =
      bw::math::add(root, bw::math::scale(bw::math::subtract(target, root),
                                          expected_distance / distance));
  if (bw::math::length(bw::math::subtract(
          bw::math::translation(solved->at(2).world), expected_hand)) >
      0.00001F) {
    throw std::runtime_error("Support hand missed authored weapon target100");
  }
  const auto cloth = secondary.advance(1.0F / 60, 0, body);
  std::array<bw::animation::BoneWorldOverride, 128> combined{};
  if (cloth.size() + solved->size() > combined.size()) {
    throw std::runtime_error("Support/cloth fixture batch capacity");
  }
  std::ranges::copy(cloth, combined.begin());
  std::ranges::copy(*solved, std::span(combined).subspan(cloth.size()).begin());
  body.apply_world_overrides(
      std::span(combined).first(cloth.size() + solved->size()));
  const auto after = domain->snapshot();
  if (before.live_allocations != after.live_allocations ||
      before.live_bytes != after.live_bytes) {
    throw std::runtime_error("Support-arm frame allocated retained storage");
  }
  for (const auto& override : cloth) {
    if (body.bone_transforms()[override.bone] != override.world) {
      throw std::runtime_error("Support arm discarded costume correction");
    }
  }
  for (std::size_t segment = 0; segment < 2; ++segment) {
    const auto original_length = bw::math::length(
        bw::math::subtract(bw::math::translation(originals.at(segment + 1)),
                           bw::math::translation(originals.at(segment))));
    const auto final_length = bw::math::length(bw::math::subtract(
        bw::math::translation(body.bone_transforms()[chain.at(segment + 1)]),
        bw::math::translation(body.bone_transforms()[chain.at(segment)])));
    if (std::abs(original_length - final_length) > 0.00001F) {
      throw std::runtime_error("Support-arm constraint stretches a segment");
    }
  }
  for (std::size_t row = 0; row < 3; ++row) {
    if (body.bone_transforms()[chain[2]].at(row) != originals[2].at(row)) {
      throw std::runtime_error(
          "Support-arm constraint changed authored hand orientation");
    }
  }
}
// Observe actual Stage/PlayerController dispatch, resource admission and grip.
// Settled raised frames must reach authored target100 (or its reachable clamp).
// This is the authorized PC improvement, not original-game visual equivalence.
void live_discharge_trajectories(const WorldOutput& original_output,
                                 bw::game::MissionLoading& loading,
                                 bw::resources::ResourceService& resources,
                                 const bw::platform::Files& files,
                                 bool texture_mips) {
  const auto& definition = *loading.stage();
  const auto settings = bw::game::PlayerResourceSettings::parse(
      bw::formats::CustomizationDefinition::parse(
          definition.customization_bytes(), resources.memory_domain()));
  for (unsigned weapon = 0; weapon < 4; ++weapon) {
    WorldOutput output(texture_mips);
    output.records = original_output.records;
    output.model_chunks = original_output.model_chunks;
    bw::game::MissionLoading player_assets(
        files, resources.catalogs(), resources.memory_domain(), &output,
        bw::game::MissionLoadOptions{.texture_mips = texture_mips});
    player_assets.prepare_player(0, weapon);
    support_arm_content_contracts(player_assets.resident_player(),
                                  player_assets.resident_weapon(),
                                  resources.memory_domain());
    const auto& names =
        player_assets.resident_player().definition.get().bone_names();
    const auto support =
        std::ranges::find(names, "LeftHand", &bw::formats::ModelBoneName::name);
    const auto hand = std::ranges::find(names, "RightWeapon",
                                        &bw::formats::ModelBoneName::name);
    if (support == names.end() || hand == names.end()) {
      throw std::runtime_error("Live discharge has no hand bindings");
    }
    const auto shoulder =
        std::ranges::find(names, "LeftArm", &bw::formats::ModelBoneName::name);
    const auto elbow = std::ranges::find(names, "LeftForeArm",
                                         &bw::formats::ModelBoneName::name);
    if (shoulder == names.end() || elbow == names.end()) {
      throw std::runtime_error("Live discharge has no support chain");
    }
    const auto weapon_view = player_assets.resident_weapon();
    const auto& weapon_skeleton = *weapon_view.definition.get().skeleton();
    const auto controls = bw::formats::ControlPointDefinition::parse(
        weapon_view.control_points, weapon_skeleton.bones().size(),
        resources.memory_domain());
    const auto& grip = controls.frames().at(100);
    const auto influences = controls.influences(grip);
    if (influences.size() != 1 || influences[0].bone != 0 ||
        influences[0].weight != 1) {
      throw std::runtime_error(
          "Live grip fixture requires authored root target");
    }
    const auto inverse_bind = bw::math::homogeneous(
        weapon_skeleton.bones().at(0).inverse_bind_matrix);
    for (const bool moving : {false, true}) {
      for (const bool targeting : {false, true}) {
        CorpusWorldTime time;
        time.bind_resources(settings);
        time.player->select_weapon(weapon);
        bw::game::StageInspection scene(
            time.clock, time.control, output,
            {definition, loading.resident_models(),
             loading.resident_textures()},
            {*loading.sky(), loading.sky_resident_models()},
            resources.memory_domain(), std::nullopt,
            {&*loading.background(), loading.resident_dependencies()},
            bw::game::PlayerActorSource{
                player_assets.resident_player(),
                player_assets.resident_weapon(), player_assets.player_support(),
                player_assets.weapon_support(), kFixtureInitialPose},
            {}, {loading.character_light(), loading.enemy_light()});
        scene.bind_action_resources(time.resources->actions());
        const auto initial_ammunition =
            time.resources->snapshot().weapons.at(weapon).ammunition;
        bw::animation::Matrix previous = bw::animation::kIdentityMatrix;
        float maximum_relative_step = 0;
        unsigned verified_grip_frames = 0;
        for (unsigned frame = 0; frame < 240; ++frame) {
          bw::game::StageTraversalInput input;
          input.presentation.aim_mode_pressed = targeting && frame == 1;
          input.presentation.fire_trigger =
              frame >= 90 && frame < 210 ? 1.0F : 0.0F;
          if (moving && frame >= 60) input.movement.direction = {.5F, 0};
          time.advance(scene, 1.0 / 60, input);
          const auto pose = scene.player_world_pose();
          const auto inverse = bw::math::inverse_affine(pose[hand->bone]);
          if (!inverse) throw std::runtime_error("Live attachment is singular");
          const auto relative =
              bw::math::multiply(pose[support->bone], *inverse);
          if (!bw::math::all_finite(relative)) {
            throw std::runtime_error(
                "Live discharge publishes invalid support hand");
          }
          const auto* upper = scene.player_animation_state(
              bw::game::CharacterMotionBank::upper_body);
          const auto upper_slot =
              upper ? std::optional(static_cast<std::uint32_t>(upper->binding))
                    : std::nullopt;
          if (frame >= 170 && frame < 210 &&
              scene.player_state()->weapon_raised &&
              bw::game::permits_support_hand(upper_slot)) {
            const auto target = bw::math::transform_point_basis_first(
                grip.points[0],
                bw::math::multiply(inverse_bind, pose[hand->bone]));
            const auto root = bw::math::translation(pose[shoulder->bone]);
            const auto joint = bw::math::translation(pose[elbow->bone]);
            const auto end = bw::math::translation(pose[support->bone]);
            const auto a = bw::math::length(bw::math::subtract(joint, root));
            const auto b = bw::math::length(bw::math::subtract(end, joint));
            const auto direction = bw::math::subtract(target, root);
            const auto distance = bw::math::length(direction);
            const auto reach = std::clamp(distance, std::abs(a - b), a + b);
            const auto expected = bw::math::add(
                root, bw::math::scale(direction, reach / distance));
            if (bw::math::length(bw::math::subtract(end, expected)) > .0001F) {
              throw std::runtime_error(
                  "Live support hand misses authored reachable grip");
            }
            ++verified_grip_frames;
          }
          if (frame >= 90 && frame < 210) {
            maximum_relative_step = std::max(
                maximum_relative_step, bw::math::length(bw::math::subtract(
                                           bw::math::translation(relative),
                                           bw::math::translation(previous))));
          }
          previous = relative;
        }
        const auto final_ammunition =
            time.resources->snapshot().weapons.at(weapon).ammunition;
        if (final_ammunition >= initial_ammunition ||
            verified_grip_frames == 0) {
          throw std::runtime_error(
              "Live discharge trajectory did not admit firing");
        }
        std::cerr << "Live discharge trajectory: class=" << weapon
                  << " moving=" << moving << " targeted=" << targeting
                  << " ammunition=" << initial_ammunition - final_ammunition
                  << " max_relative_step=" << maximum_relative_step << '\n';
      }
    }
  }
}

// Initial equipment admission belongs to the existing original-root corpus;
// each acquired-class fixture prepares its real owner and verifies attachment.
void initial_weapon_admission(bw::resources::ResourceService& resources,
                              const bw::platform::Files& files,
                              WorldOutput& world,
                              const bw::game::Progression& progression,
                              const bw::game::PlayerResourceSettings& settings,
                              std::size_t retained_models,
                              std::size_t retained_textures,
                              bool texture_mips) {
  // Initial admission uses each original weapon owner; it must not resolve
  // class-specific animation through WA0 or admit a live replacement.
  std::size_t weapon_motion_count = 0;
  for (unsigned weapon_class = 0; weapon_class < 4; ++weapon_class) {
    {
      bw::game::MissionLoading selected(
          files, resources.catalogs(), resources.memory_domain(), &world,
          bw::game::MissionLoadOptions{.texture_mips = texture_mips});
      auto entry_progression = progression;
      entry_progression.levels.weapons.fill(0);
      entry_progression.levels.weapons.at(weapon_class) = 1;
      const bw::game::MissionPlayerState selected_state(
          {entry_progression, std::nullopt, false,
           bw::game::ConceptProfile::saved});
      if (selected_state.selected_weapon() != weapon_class) {
        throw std::runtime_error("Initial mission equipment selection differs");
      }
      selected.prepare_player(0, selected_state.selected_weapon());
      const auto weapon = selected.resident_weapon();
      const auto expected_name =
          std::string("wa") + static_cast<char>('0' + weapon_class);
      const auto models = world.live_models;
      const auto textures = world.live_textures;
      selected.prepare_player(0, weapon_class);
      bool replacement_rejected = false;
      try {
        selected.prepare_player(0, (weapon_class + 1) % 4);
      } catch (const std::logic_error&) {
        replacement_rejected = true;
      }
      if (weapon.name != expected_name || !replacement_rejected ||
          selected.resident_weapon().handle != weapon.handle ||
          world.live_models != models || world.live_textures != textures) {
        throw std::runtime_error(
            "Initial weapon class admission changed owners");
      }
      bw::game::PlayerActor selected_actor(
          {selected.resident_player(), weapon, selected.player_support(),
           selected.weapon_support(), kFixtureInitialPose},
          resources.memory_domain());
      // Both original weapon half-turns cancel before the completed hand
      // attachment. Compare published GPU palettes with that source frame,
      // independently from the actor's attachment implementation.
      const auto& weapon_definition = weapon.definition.get();
      bw::animation::SkeletonPose expected_weapon(
          weapon_definition.skeleton().value(), resources.memory_domain());
      const auto* weapon_clock =
          selected_actor.animation_state(bw::game::CharacterMotionBank::weapon);
      if (weapon_clock == nullptr) {
        throw std::runtime_error("Initial weapon has no motion owner");
      }
      const auto hand = std::ranges::find(
          selected_actor.source().body.definition.get().bone_names(),
          "RightWeapon", &bw::formats::ModelBoneName::name);
      expected_weapon.evaluate(*weapon_clock->clip, weapon_clock->seconds,
                               selected_actor.body_bones()[hand->bone]);
      for (std::size_t draw = 0; draw < weapon_definition.draws().size();
           ++draw) {
        if (!selected_actor.includes_draw(true, draw)) continue;
        const auto palette = selected_actor.draw_palette(true, draw);
        const auto& material = weapon_definition.materials().at(
            weapon_definition.draws().at(draw).material);
        for (const auto& binding : material.matrices) {
          const auto& expected =
              expected_weapon.skin_transforms()[binding.source_matrix];
          const auto& actual = palette[binding.destination_matrix];
          for (std::size_t row = 0; row < 4; ++row) {
            for (std::size_t column = 0; column < 4; ++column) {
              if (std::abs(actual.at(row).at(column) -
                           expected.at(column).at(row)) > .00001F) {
                throw std::runtime_error(
                    "Weapon render conversion reverses its hand frame");
              }
            }
          }
        }
      }
      discharge_attachment_trajectory(selected_actor.source(), selected_state,
                                      settings, resources.memory_domain());
      weapon_motion_count +=
          selected_actor.weapon_animations().motions().size();
    }
    if (world.live_models != retained_models ||
        world.live_textures != retained_textures) {
      throw std::runtime_error("Initial weapon admission retained resources");
    }
  }
  if (weapon_motion_count != 26) {
    throw std::runtime_error(
        "Four weapon catalogs lost original clip coverage");
  }
}
// Called only after the real pause requests Retire and every scene, audio,
// sprite and mission-resource owner above has completed ordinary teardown.
void root_retirement_after_teardown(bw::game::StartupSession& session,
                                    Media& media) {
  const auto prior_movies = media.opened.size();
  session.complete_mission_retirement();
  constexpr unsigned kReturnFrames = 300;
  for (unsigned frame = 0;
       frame < kReturnFrames && media.opened.size() == prior_movies; ++frame) {
    const auto result = session.advance({.1F, 0});
    if (result.state == bw::script::RunState::failed) {
      throw std::runtime_error("Original root retirement failed: " +
                               result.diagnostic);
    }
  }
  if (media.opened.size() != prior_movies + 1 ||
      media.opened.back() != "data/movie/witchLOGO.ssu" ||
      session.mission_status() != 2) {
    throw std::runtime_error(
        "Original root retirement omitted stop/unload/status/title "
        "handoff");
  }
}
void reconstructed_session(bw::resources::ResourceService& resources,
                           const bw::platform::Files& files, bool concept_route,
                           const std::filesystem::path& game,
                           bool texture_mips) {
  Media media;
  SpriteOutput output(texture_mips);
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
  // Existing native storage route now carries actual progression mutations.
  (void)profile.award_result({0, 0, bw::game::MissionRewardMode::campaign});
  if (profile.purchase({bw::game::UpgradeCategory::weapon, 1}) !=
          bw::game::PurchaseResult::purchased ||
      profile.save(*storage).status != bw::platform::SaveStatus::ok) {
    throw std::runtime_error("Corpus progression seed failed");
  }
  const auto expected_progression = profile.progression();
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
        session.gameplay_request()->progression != expected_progression ||
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
      if (!request.checkpoint.empty()) {
        throw std::runtime_error("Concept entry inherited campaign checkpoint");
      }
      bw::game::MissionPlayerState saved_player(
          {request.progression, std::nullopt, true,
           bw::game::ConceptProfile::saved});
      bw::game::MissionPlayerState development_player(
          {request.progression, std::nullopt, true,
           bw::game::ConceptProfile::development_completed});
      if (saved_player.next_weapon() != 1 || saved_player.weapon_available(2) ||
          !development_player.weapon_available(2) ||
          profile.progression() != expected_progression) {
        throw std::runtime_error(
            "Concept profile selection changed inheritance");
      }
      WorldOutput world(texture_mips);
      {
        bw::game::StartupLoadingSurface presentation(resources, output);
        const auto icons =
            files.read(game / "data/menu/9900", resources.memory_domain());
        presentation.prepare(
            surface.selected_loading_background(),
            files.read(game / "data/menu/9951", resources.memory_domain()),
            icons);
        constexpr double kHalfFadeTime = .1;
        if (presentation.show(5, kHalfFadeTime) != .5F ||
            output.latest.front().color.alpha != 1) {
          throw std::runtime_error("Loading fade changed sprite coverage");
        }
        presentation.show(5, 1);
        const auto& portrait = output.latest.front().destination;
        constexpr float kPortraitWidth = 520, kPortraitHeight = 242,
                        kPortraitY = 434;
        constexpr std::size_t kRotatedArt = 13, kLoadingTextures = 8;
        const auto rotated = std::ranges::count_if(
            output.latest,
            [](const auto& sprite) { return sprite.rotation_radians != 0; });
        if (output.live != kLoadingTextures || portrait.x != 0 ||
            portrait.y != kPortraitY || portrait.width != kPortraitWidth ||
            portrait.height != kPortraitHeight ||
            static_cast<std::size_t>(rotated) != kRotatedArt) {
          throw std::runtime_error(
              "Original loading portrait/art publication mismatch");
        }
        bool missing_tip = false;
        try {
          presentation.show(18, 1);
        } catch (const std::invalid_argument&) {
          missing_tip = true;
        }
        if (!missing_tip) {
          throw std::runtime_error("Absent original loading tip accepted");
        }
        presentation.end();
        if (output.live != 0) {
          throw std::runtime_error("Loading presentation retained textures");
        }
      }
      std::size_t loading_pulses = 0;
      bw::game::MissionLoading loading(
          files, resources.catalogs(), resources.memory_domain(), &world,
          bw::game::MissionLoadOptions{game, profile.current_difficulty(),
                                       [&] { ++loading_pulses; }, false,
                                       texture_mips});
      bw::game::MissionControl mission_control;
      bw::game::MissionDriver mission(
          resources, loading, 101, request.script_name,
          resources.memory_domain(), mission_control);
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
      if (loading_pulses <= loading.stage()->textures.size()) {
        throw std::runtime_error(
            "Original mission failed to yield presentation between assets");
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
        const auto settings_tree = bw::formats::CustomizationDefinition::parse(
            stage->customization_bytes(), resources.memory_domain());
        const auto resource_settings =
            bw::game::PlayerResourceSettings::parse(settings_tree);
        constexpr std::array<unsigned, 4> expected_capacities{80, 16, 24, 400};
        constexpr std::array<float, 4> expected_recharge{20, 40, 40, 40};
        if (resource_settings.maximum_health != 5000 ||
            resource_settings.magic_recovery_seconds !=
                std::array{60.0F, 50.0F, 40.0F, 30.0F}) {
          throw std::runtime_error("Original player resource settings differ");
        }
        constexpr std::array<float, 4> expected_rates{10, 1, 1.8F, 60};
        for (unsigned weapon_class = 0; weapon_class < 4; ++weapon_class) {
          const auto settings = bw::game::WeaponPresentationSettings::parse(
              settings_tree, weapon_class);
          const auto expected_scale = weapon_class == 3 ? .9F : 1.0F;
          const auto& weapon_resources =
              resource_settings.weapons.at(weapon_class);
          if (settings.rate_per_second != expected_rates.at(weapon_class) ||
              settings.movement_scale != expected_scale ||
              weapon_resources.rate_per_second !=
                  expected_rates.at(weapon_class) ||
              weapon_resources.capacity !=
                  expected_capacities.at(weapon_class) ||
              weapon_resources.recharge_cost !=
                  expected_recharge.at(weapon_class)) {
            throw std::runtime_error(
                "Original weapon presentation settings differ");
          }
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
        CorpusWorldTime time;
        bw::game::StageInspection inspection(
            time.clock, time.control, world,
            {stage.value(), loading.resident_models(),
             loading.resident_textures()},
            {sky.value(), loading.sky_resident_models()},
            resources.memory_domain(), std::nullopt,
            {background ? &*background : nullptr, background_models},
            std::nullopt, {},
            {loading.character_light(), loading.enemy_light()},
            kFixtureInitialPose);
        std::cerr
            << "Original static inspection: " << inspection.selected_draws()
            << " selected, " << inspection.omitted_draws()
            << " omitted selected-world draws; synthetic device fixture\n";
        // Actual authored labels bind two independent float2 curves. Advancing
        // the existing view updates retained constants without rebuilding it.
        const auto memory_before_animation =
            resources.memory_domain()->snapshot().live_bytes;
        time.advance(inspection, 1.0);
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
        const auto& sky_model = sky->models.front().definition;
        const auto& sky_material =
            sky_model.materials().at(sky_model.draws().front().material);
        const auto expected_sky_selector =
            sky_material.shader_selector[0] | 0x00800200U;
        if (world.sky_shader_flags != expected_sky_selector ||
            world.sky_parameters.irradiance_coefficients != white_irradiance ||
            world.sky_parameters.ambient != std::array<float, 4>{}) {
          throw std::runtime_error(
              "Sky must retain original white harmonics without ambient "
              "substitution");
        }
        // The accepted live AREA producer replaces raw white CSTM ambient
        // at this cave view. The original area-update witness proves zero
        // ambient, exposure .13 and gamma 2.1; sky remains white above. See
        // character-material-brightness-audit.md, runtime environment producer.
        std::array<std::array<float, 4>, 7> cave_irradiance{};
        cave_irradiance.back() = {0, 0, 0, 1};
        if (stage->environment->ambient_color !=
                std::array<float, 3>{1, 1, 1} ||
            world.world_parameters.irradiance_coefficients != cave_irradiance ||
            world.harmonic_draws != world.submitted) {
          throw std::runtime_error(
              "Ordinary stage objects lost source cave environment lighting");
        }
        if (!world.output_parameters.tone_mapping_enabled ||
            world.output_parameters.exposure_key != .13F ||
            world.output_parameters.gamma != 2.1F ||
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
            world.caster_draws != 3529 || world.submitted != 4775) {
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
      live_discharge_trajectories(world, loading, resources, files,
                                  texture_mips);
      initial_weapon_admission(resources, files, world, request.progression,
                               bw::game::PlayerResourceSettings::parse(
                                   bw::formats::CustomizationDefinition::parse(
                                       loading.stage()->customization_bytes(),
                                       resources.memory_domain())),
                               retained_models, retained_textures,
                               texture_mips);
      {
        bw::game::MissionLoading selected(
            files, resources.catalogs(), resources.memory_domain(), &world,
            bw::game::MissionLoadOptions{.texture_mips = texture_mips});
        bool invalid = false;
        try {
          selected.prepare_player(0, 4);
        } catch (const std::invalid_argument&) {
          invalid = true;
        }
        world.reject_next_model = true;
        bool upload_failed = false;
        try {
          selected.prepare_player(0, 3);
        } catch (const std::runtime_error&) {
          upload_failed = true;
        }
        if (!invalid || !upload_failed ||
            world.live_models != retained_models ||
            world.live_textures != retained_textures) {
          throw std::runtime_error(
              "Weapon admission failure retained partial assets");
        }
        selected.prepare_player(0, 3);
        selected.cancel();
        selected.prepare_player(0, 1);
      }
      // Live review resource transaction: candidate owners survive promotion,
      // while preparation, failed upload and rollback preserve active
      // borrowers.
      {
        ObservedFiles observed_files(files);
        bw::game::MissionLoading selected(
            observed_files, resources.catalogs(), resources.memory_domain(),
            &world, bw::game::MissionLoadOptions{.texture_mips = texture_mips});
        selected.prepare_player(0);
        const auto admission_reads = observed_files.reads();
        const auto admission_uploads = world.records.size();
        const auto admission_textures = world.texture_identity;
        const auto body = selected.resident_player();
        const auto* body_support = &selected.player_support();
        bw::game::PlayerActor selected_actor(
            {body, selected.resident_weapon(), selected.player_support(),
             selected.weapon_support(), kFixtureInitialPose},
            resources.memory_domain());
        auto weapon_draws = [](bw::game::ResidentModelView weapon) {
          std::vector<bw::render::WorldColorDraw> draws;
          for (std::size_t draw = 0;
               draw < weapon.definition.get().draws().size(); ++draw) {
            bw::render::WorldColorDraw command;
            command.model = weapon.handle;
            command.draw = draw;
            draws.push_back(command);
          }
          return draws;
        };
        world.set_world_draws(weapon_draws(selected.resident_weapon()));
        selected_actor.select_motion(bw::game::PlayerMotion::walk, 0);
        (void)selected_actor.advance_motion(.2);
        selected_actor.publish_pose(kFixtureInitialPose, .4F, world);
        const auto original_weapon = selected.resident_weapon();
        const auto* original_support = &selected.weapon_support();
        const auto initial_models = world.live_models;
        const auto initial_textures = world.live_textures;
        selected.prepare_weapon_class(0);
        selected.commit_weapon_class();
        world.reject_next_model = true;
        selected.prepare_weapon_class(1);
        if (!world.reject_next_model || selected.current_weapon_class() != 0 ||
            selected.resident_weapon().handle != original_weapon.handle ||
            &selected.weapon_support() != original_support) {
          throw std::runtime_error(
              "Resident selection uploaded or replaced resources");
        }
        world.reject_next_model = false;
        selected.discard_weapon_class();
        bool rejected = false;
        selected.prepare_weapon_class(1);
        rejected = false;
        try {
          selected.prepare_player_costume(1);
        } catch (const std::logic_error&) {
          rejected = true;
        }
        if (!rejected) {
          throw std::runtime_error("Appearance transactions overlapped");
        }
        selected.discard_weapon_class();
        if (world.live_models != initial_models ||
            world.live_textures != initial_textures) {
          throw std::runtime_error(
              "Discarded weapon candidate retained resources");
        }
        for (const unsigned weapon_class : {1U, 2U, 3U, 0U}) {
          const auto current = selected.resident_weapon();
          selected.prepare_weapon_class(weapon_class);
          const auto candidate = selected.candidate_weapon();
          const auto* candidate_definition = &candidate.definition.get();
          const auto* candidate_support = &selected.candidate_weapon_support();
          if (selected.resident_weapon().handle != current.handle) {
            throw std::runtime_error(
                "Weapon preparation replaced the active owner");
          }
          const auto old_seconds =
              selected_actor
                  .animation_state(bw::game::CharacterMotionBank::body)
                  ->seconds;
          bw::game::PlayerActor candidate_actor(
              {body, candidate, selected.player_support(), *candidate_support,
               selected_actor.source().placement},
              selected_actor, resources.memory_domain(),
              bw::game::PlayerAppearanceChange::weapon);
          if (!std::ranges::equal(selected_actor.body_bones(),
                                  candidate_actor.body_bones()) ||
              selected_actor
                      .animation_state(bw::game::CharacterMotionBank::body)
                      ->seconds != old_seconds ||
              candidate_actor.pending_root_displacement() !=
                  bw::math::Vector3<float>{}) {
            throw std::runtime_error(
                "Weapon candidate changed frozen body or retained old root");
          }
          world.replace_world_model_draws(current.handle,
                                          weapon_draws(candidate));
          selected_actor.adopt_appearance(std::move(candidate_actor));
          selected.commit_weapon_class();
          if (selected_actor
                      .animation_state(bw::game::CharacterMotionBank::body)
                      ->seconds != 0 ||
              selected_actor.source().weapon.name != candidate.name) {
            throw std::runtime_error("Weapon publication lost neutral owner");
          }
          (void)selected_actor.advance_motion(1.0 / 60.0);
          selected_actor.publish_pose(kFixtureInitialPose, .4F, world);
          if (selected.current_weapon_class() != weapon_class ||
              selected.resident_weapon().handle != candidate.handle ||
              &selected.resident_weapon().definition.get() !=
                  candidate_definition ||
              &selected.weapon_support() != candidate_support ||
              selected.resident_player().handle != body.handle ||
              &selected.resident_player().definition.get() !=
                  &body.definition.get() ||
              &selected.player_support() != body_support) {
            throw std::runtime_error(
                "Weapon promotion invalidated stable borrowing");
          }
        }
        if (world.live_models != initial_models ||
            world.live_textures != initial_textures ||
            observed_files.reads() != admission_reads ||
            world.records.size() != admission_uploads ||
            world.texture_identity != admission_textures) {
          throw std::runtime_error(
              "Weapon selection repeated acquisition or changed residency");
        }
        world.clear_world_draws();
      }
      if (world.live_models != retained_models ||
          world.live_textures != retained_textures) {
        throw std::runtime_error(
            "Weapon transaction teardown retained resources");
      }
      // Actor admission extends the same mission lifetime; pose changes update
      // retained palettes without publishing another geometry/texture owner.
      loading.prepare_player(0);
      const auto actor_models = world.live_models;
      const auto actor_textures = world.live_textures;
      loading.prepare_player(0);
      const auto initial_player_handle = loading.resident_player().handle;
      const auto* initial_player_definition =
          &loading.resident_player().definition.get();
      world.reject_next_model = true;
      bool costume_failed = false;
      try {
        loading.prepare_player_costume(5);
      } catch (const std::runtime_error&) {
        costume_failed = true;
      }
      if (!costume_failed || world.live_models != actor_models ||
          world.live_textures != actor_textures ||
          &loading.resident_player().definition.get() !=
              initial_player_definition) {
        throw std::runtime_error(
            "Failed costume upload changed current assets");
      }
      loading.prepare_player_costume(5);
      const auto* candidate_definition =
          &loading.candidate_player().definition.get();
      if (loading.resident_player().handle != initial_player_handle ||
          loading.current_player_costume() != 0 ||
          candidate_definition == initial_player_definition) {
        throw std::runtime_error(
            "Costume preparation changed the active appearance");
      }
      loading.discard_player_costume();
      if (world.live_models != actor_models ||
          world.live_textures != actor_textures ||
          &loading.resident_player().definition.get() !=
              initial_player_definition) {
        throw std::runtime_error("Costume rollback lost current residency");
      }
      loading.prepare_player_costume(5);
      const auto promoted = loading.candidate_player();
      loading.commit_player_costume();
      if (&loading.resident_player().definition.get() !=
              &promoted.definition.get() ||
          loading.resident_player().handle != promoted.handle ||
          loading.current_player_costume() != 5) {
        throw std::runtime_error(
            "Costume promotion invalidated prepared borrowing");
      }
      loading.prepare_player_costume(0);
      loading.commit_player_costume();
      if (world.live_models != actor_models ||
          world.live_textures != actor_textures) {
        throw std::runtime_error(
            "Repeated costume replacement accumulated residency");
      }
      {
        const auto dependencies = loading.resident_dependencies();
        const auto& stage = loading.stage();
        const auto& sky = loading.sky();
        const auto& background = loading.background();
        if (!stage || !sky || !background) {
          throw std::runtime_error(
              "Actor scene requires retained stage inputs");
        }
        CorpusAudioFactory scene_audio_factory;
        bw::game::MissionAudio scene_audio(
            files, resources.catalogs(), game / "data/sound",
            resources.memory_domain(), scene_audio_factory,
            {3, 128, 50'000'000});
        bw::game::SourceRandom scene_random;
        placed_light_publication(world, loading, resources.memory_domain());
        CorpusWorldTime time;
        bw::game::StageInspection inspection(
            time.clock, time.control, world,
            {*stage, loading.resident_models(), loading.resident_textures()},
            {*sky, loading.sky_resident_models()}, resources.memory_domain(),
            std::nullopt, {&*background, dependencies},
            bw::game::PlayerActorSource{
                loading.resident_player(), loading.resident_weapon(),
                loading.player_support(), loading.weapon_support(),
                kFixtureInitialPose},
            {}, {loading.character_light(), loading.enemy_light()});
        const auto resource_settings = bw::game::PlayerResourceSettings::parse(
            bw::formats::CustomizationDefinition::parse(
                stage->customization_bytes(), resources.memory_domain()));
        time.bind_resources(resource_settings);
        inspection.bind_audio(scene_audio.prepared(), *scene_audio.output(),
                              scene_random);
        mission_gameplay_composition(
            time, inspection, loading, scene_random,
            {resources, output, menu.surface_, files, game, profile,
             *scene_audio_factory.output, *storage});
        require_initial_local_light_clear(world,
                                          loading.resident_player().handle);
        // All four families share a forward raw root curve; the authored
        // endpoint reference bone must produce distinct actor-local travel.
        {
          bw::game::PlayerActor directions(
              {loading.resident_player(), loading.resident_weapon(),
               loading.player_support(), loading.weapon_support(),
               kFixtureInitialPose},
              resources.memory_domain());
          // Source parent movement consumes the preceding model interval;
          // selection changes its live reference, not the retained raw delta.
          {
            bw::game::PlayerActor phase(directions.source(), directions,
                                        resources.memory_domain());
            if (bw::math::length(phase.pending_root_displacement()) != 0 ||
                phase.pending_root_seconds() != 0 ||
                phase.relative_root_rotation() !=
                    bw::animation::kIdentityMatrix) {
              throw std::runtime_error(
                  "Fresh actor did not retain identity root");
            }
            phase.select_motion(bw::game::PlayerMotion::walk, 0);
            const auto* facing_clip =
                phase.animation_state(bw::game::CharacterMotionBank::body);
            const auto raw_reference = bw::animation::transform_matrix(
                facing_clip->clip->local_transform(1, facing_clip->seconds));
            if (phase.authored_facing_reference() != raw_reference ||
                phase.authored_facing_reference() ==
                    phase.movement_reference()) {
              throw std::runtime_error(
                  "Facing reused the corrected displacement reference");
            }

            if (bw::math::length(phase.pending_root_displacement()) != 0) {
              throw std::runtime_error(
                  "Selection advanced a fresh root interval");
            }
            (void)phase.advance_motion(.2);
            const auto pending = phase.pending_root_displacement();
            const auto inverse =
                bw::math::inverse_affine(phase.movement_reference());
            if (!inverse || bw::math::length(pending) <= 0) {
              throw std::runtime_error(
                  "Walk failed to publish a root interval");
            }
            if (phase.pending_root_seconds() != .2) {
              throw std::runtime_error(
                  "Root interval lost its producer duration");
            }
            const auto raw =
                bw::math::transform_direction<float>(pending, *inverse);
            const auto rotation = phase.relative_root_rotation();
            phase.select_motion(bw::game::PlayerMotion::evade_left_air, 0);
            const auto expected = bw::math::transform_direction<float>(
                raw, phase.movement_reference());
            if (bw::math::length(bw::math::subtract(
                    phase.pending_root_displacement(), expected)) > .00001F ||
                bw::math::length(bw::math::subtract(expected, pending)) <
                    .01F ||
                phase.relative_root_rotation() != rotation) {
              throw std::runtime_error(
                  "Selection lost raw interval or used its previous reference");
            }
            if (phase.pending_root_seconds() != .2) {
              throw std::runtime_error(
                  "Selection changed retained root duration");
            }
            const auto clock =
                phase.animation_state(bw::game::CharacterMotionBank::body)
                    ->unwrapped_seconds;
            bw::game::PlayerActor appearance(phase.source(), phase,
                                             resources.memory_domain());
            if (appearance.animation_state(bw::game::CharacterMotionBank::body)
                        ->unwrapped_seconds != clock ||
                appearance.pending_root_displacement() !=
                    phase.pending_root_displacement() ||
                appearance.relative_root_rotation() != rotation) {
              throw std::runtime_error(
                  "Appearance preview advanced pending motion");
            }
            if (appearance.pending_root_seconds() !=
                phase.pending_root_seconds()) {
              throw std::runtime_error(
                  "Appearance preview changed root duration");
            }
          }
          const auto& body_catalog = directions.body_animations();
          const auto& prepared = body_catalog.motions();
          std::size_t resolved_slots = 0;
          for (std::size_t slot = 0; slot < prepared.slot_count(); ++slot) {
            if (prepared.alias(slot).clip) ++resolved_slots;
          }
          const auto delta_count =
              std::ranges::count(body_catalog.pose_semantics(),
                                 bw::game::MotionPoseSemantics::additive);
          const auto ordinary_clip = prepared.find("stand_l");
          const auto unbound_delta = prepared.find("shoot_stand_fire_add");
          const auto bound_delta = prepared.alias(916).clip;
          if (delta_count != 17 ||
              std::ranges::count(body_catalog.pose_semantics(),
                                 bw::game::MotionPoseSemantics::ordinary) !=
                  135 ||
              std::ranges::count(body_catalog.pose_semantics(),
                                 bw::game::MotionPoseSemantics::unknown) != 9 ||
              !ordinary_clip || !unbound_delta || !bound_delta ||
              body_catalog.pose_semantics(*ordinary_clip) !=
                  bw::game::MotionPoseSemantics::ordinary ||
              body_catalog.pose_semantics(*unbound_delta) !=
                  bw::game::MotionPoseSemantics::additive ||
              body_catalog.pose_semantics(*bound_delta) !=
                  bw::game::MotionPoseSemantics::additive ||
              std::ranges::count(
                  directions.weapon_animations().pose_semantics(),
                  bw::game::MotionPoseSemantics::ordinary) != 6) {
            throw std::runtime_error(
                "Prepared pose semantics confused absolute poses and deltas");
          }
          unknown_pose_admission(
              loading.player_support(),
              loading.resident_player().definition.get().skeleton().value(),
              resources.memory_domain());
          const auto fall_clip = prepared.alias(721).clip;
          if (prepared.size() != 161 || resolved_slots != 415 ||
              body_catalog.parameters().size() != 164 ||
              body_catalog.shapes().size() != 6 ||
              directions.weapon_animations().motions().size() != 6 ||
              !fall_clip || fall_clip != prepared.alias(953).clip ||
              prepared.alias(721).requested_repeat ||
              !prepared.clip(*fall_clip).repeating() ||
              body_catalog.parameter("end04") == nullptr) {
            throw std::runtime_error(
                "Prepared character catalog lost source identity");
          }
          {
            bw::game::PlayerActor candidate(directions.source(), directions,
                                            resources.memory_domain());
            if (&candidate.body_animations() != &body_catalog ||
                &candidate.weapon_animations() !=
                    &directions.weapon_animations()) {
              throw std::runtime_error(
                  "Appearance duplicated immutable motion catalogs");
            }
          }
          directions.select_motion(bw::game::PlayerMotion::walk, 0);
          const auto walking_root = directions.advance_motion(0.2);
          directions.select_motion(bw::game::PlayerMotion::stand, 0.3F);
          const auto stopped_root = directions.advance_motion(0.1);
          if (bw::math::length(walking_root) <= 0 ||
              bw::math::length(stopped_root) > 0.000001F) {
            throw std::runtime_error(
                "Outgoing visual blend leaked into selected root movement");
          }
          // Body transitions do not reset an independently owned weapon clock.
          const auto weapon_time =
              directions.animation_state(bw::game::CharacterMotionBank::weapon)
                  ->unwrapped_seconds;
          directions.select_motion(bw::game::PlayerMotion::walk, 0);
          (void)directions.advance_motion(0.125);
          const auto* body_state =
              directions.animation_state(bw::game::CharacterMotionBank::body);
          const auto* weapon_state =
              directions.animation_state(bw::game::CharacterMotionBank::weapon);
          if (body_state == nullptr || weapon_state == nullptr ||
              std::abs(body_state->unwrapped_seconds - 0.125) > 0.000001 ||
              std::abs(weapon_state->unwrapped_seconds - weapon_time - 0.125) >
                  0.000001) {
            throw std::runtime_error("Body selection reset the weapon clock");
          }
          // The actual INT parameter changes entry blending, while source
          // phase-preserving migrations deliberately retain caller blending.
          const auto* attack_parameter = body_catalog.parameter("at_stagger");
          if (attack_parameter == nullptr || !attack_parameter->blend_frames ||
              attack_parameter->blend_frames->entry != 10 ||
              attack_parameter->blend_frames->exit != 0) {
            throw std::runtime_error("Authored attack transition is absent");
          }
          bw::game::ActionMotionPlan attack;
          attack.requests[0] = {
              .slot = 706, .blend_seconds = .6, .fresh = true};
          attack.count = 1;
          if (directions.apply_action_plan(attack).admitted != 1) {
            throw std::runtime_error("Source attack slot was not admitted");
          }
          (void)directions.advance_motion(5.0 / 60.0);
          if (std::abs(directions
                           .animation_state(bw::game::CharacterMotionBank::body)
                           ->weight -
                       .5F) > .0001F) {
            throw std::runtime_error(
                "INT entry duration did not override blend");
          }
          attack.requests[0].start =
              bw::game::ActionMotionStart::preserve_phase;
          (void)directions.apply_action_plan(attack);
          (void)directions.advance_motion(.3);
          if (std::abs(directions
                           .animation_state(bw::game::CharacterMotionBank::body)
                           ->weight -
                       .5F) > .0001F) {
            throw std::runtime_error(
                "Phase-preserving request used INT override");
          }
          // Accepted reload presentation owns separate upper and weapon clocks;
          // reaching its upper endpoint clears that complete pose owner.
          const auto reload =
              bw::game::accepted_reload({.body_slot = 689, .weapon_class = 0});
          if (directions.apply_action_plan(reload).admitted != reload.count) {
            throw std::runtime_error(
                "Reload presentation lost authored bindings");
          }
          const auto* upper = directions.animation_state(
              bw::game::CharacterMotionBank::upper_body);
          if (upper == nullptr) {
            throw std::runtime_error(
                "Reload did not create upper motion owner");
          }
          const auto duration = upper->clip->duration_seconds();
          (void)directions.advance_motion(duration + 1.0 / 60.0);
          if (directions.animation_state(
                  bw::game::CharacterMotionBank::upper_body) != nullptr) {
            throw std::runtime_error(
                "Completed upper owner retained pose layers");
          }
          bw::game::ActionMotionPlan frame_entry;
          frame_entry.count = 1;
          frame_entry.requests[0] = {
              .slot = 788,
              .start = bw::game::ActionMotionStart::frames,
              .start_frames = 1,
              .fresh = true};
          (void)directions.apply_action_plan(frame_entry);
          const auto* frame_state =
              directions.animation_state(bw::game::CharacterMotionBank::body);
          if (frame_state == nullptr ||
              std::abs(frame_state->seconds *
                           frame_state->clip->frames_per_second() -
                       1) > .0001) {
            throw std::runtime_error(
                "Source frame entry was treated as seconds");
          }
          directions.advance(0, world);
          constexpr std::array motions{
              bw::game::PlayerMotion::evade_forward_air,
              bw::game::PlayerMotion::evade_backward_air,
              bw::game::PlayerMotion::evade_left_air,
              bw::game::PlayerMotion::evade_right_air};
          for (std::size_t direction = 0; direction < motions.size();
               ++direction) {
            directions.select_motion(motions.at(direction), 0);
            const auto delta = directions.advance_motion(0.25);
            directions.advance(0, world);
            // The same authored direction remains available to root motion,
            // but the pre-hierarchy modifier removes it from displayed facing.
            const auto bones = directions.body_bones();
            for (std::size_t row = 0; row < 3; ++row) {
              for (std::size_t column = 0; column < 3; ++column) {
                if (std::abs(bones[1].at(row).at(column) -
                             bones[0].at(row).at(column)) > 0.0001F) {
                  throw std::runtime_error(
                      "Movement reference leaked into displayed facing");
                }
              }
            }
            const float projected = direction < 2 ? delta[2] : delta[0];
            const bool positive = direction == 0 || direction == 3;
            if (delta[1] <= 0 || (positive ? projected <= 0 : projected >= 0)) {
              throw std::runtime_error(
                  "Authored evade reference did not preserve direction/lift");
            }
          }
          // Exercise the full immutable catalog through production actor pose
          // and relative-root paths. This validates data/consumer admission,
          // not gameplay-family or native visual equivalence.
          for (std::size_t index = 0; index < prepared.size(); ++index) {
            directions.request_animation(
                bw::game::CharacterMotionBank::body,
                {.clip = index,
                 .binding = index,
                 .parameter = body_catalog.parameter_for_clip(index),
                 .fresh = true});
            (void)directions.advance_motion(1.0 / 60.0);
            directions.advance(0, world);
          }
        }
        model_timing_equivalence(world, loading, resources.memory_domain(),
                                 scene_audio.prepared());
        const auto initial_frame = time.clock.publish(1.0F / 60, {});
        inspection.advance(initial_frame);
        bw::world::WorldClock foreign_clock;
        for (const auto rejected :
             {initial_frame, foreign_clock.publish(0, {})}) {
          bool rejected_frame = false;
          try {
            inspection.advance(rejected);
          } catch (const std::invalid_argument&) {
            rejected_frame = true;
          }
          if (!rejected_frame) {
            throw std::runtime_error(
                "Scene accepted repeated or foreign world time");
          }
        }
        for (unsigned frame = 0; frame < 30; ++frame) {
          time.advance(inspection, 1.0 / 60.0);
        }
        time.advance(inspection, 0.05, {.movement = {.crouch_pressed = true}});
        if (inspection.player_state()->stance !=
            bw::game::AliciaStance::crouched) {
          throw std::runtime_error(
              "Crouch edge did not survive bounded movement substeps");
        }
        time.advance(inspection, 1.0 / 60.0,
                     {.movement = {.crouch_pressed = true}});
        if (inspection.player_state()->stance !=
            bw::game::AliciaStance::standing) {
          throw std::runtime_error(
              "Second source crouch edge did not restore standing");
        }
        auto* actions = inspection.player_actions();
        if (actions == nullptr ||
            actions->begin_damage({.direction = {.incoming = {0, 0, 1}}})
                    .outcome != bw::game::AliciaActionOutcome::started) {
          throw std::runtime_error("Ordinary player damage was not admitted");
        }
        for (unsigned frame = 0; frame < 4; ++frame) {
          time.advance(inspection, 1.0 / 60.0,
                       {.movement = {.direction = {1, 0}}});
        }
        if (actions->kind() != bw::game::AliciaActionKind::damage) {
          throw std::runtime_error(
              "Locomotion replaced an active damage owner");
        }
        for (unsigned frame = 0; frame < 180 && actions->blocks_locomotion();
             ++frame) {
          time.advance(inspection, 1.0 / 60.0);
        }
        if (actions->blocks_locomotion()) {
          throw std::runtime_error(
              "Ordinary damage did not release body ownership");
        }
        const auto before_unavailable = inspection.player_world_pose()[2];
        const auto unavailable = actions->begin_sustained_damage(
            {.direction = {.incoming = {0, 0, 1}},
             .recovery_seconds = .3F,
             .random_unit = .5F});
        if (unavailable.outcome !=
                bw::game::AliciaActionOutcome::unavailable_motion ||
            actions->blocks_locomotion() ||
            inspection.player_world_pose()[2] != before_unavailable) {
          throw std::runtime_error(
              "Absent sustained damage changed active pose");
        }
        const auto magic = actions->begin_magic({.slot = 788,
                                                 .complete_with_motion = true,
                                                 .shooter_update = false});
        if (magic.outcome != bw::game::AliciaActionOutcome::started ||
            !magic.acquire_magic_scope) {
          throw std::runtime_error(
              "Accepted magic lost scoped presentation owner");
        }
        unsigned scope_releases = 0;
        for (unsigned frame = 0; frame < 180; ++frame) {
          time.advance(inspection, 1.0 / 60.0);
          scope_releases += inspection.player_action_status()
                                ->release_magic_scope.has_value();
        }
        if (actions->blocks_locomotion() || scope_releases != 1) {
          throw std::runtime_error("Magic scope did not release exactly once");
        }
        // The fixture acknowledges the preceding device voice's completion;
        // it does not invent a cue duration or weaken equal-priority admission.
        for (auto& slot : scene_audio_factory.output->slots) {
          if (slot.request.bank == 4 || slot.request.bank == 50) {
            slot.active = false;
          }
        }
        const auto thunder =
            actions->begin_spell(bw::game::SpellPresentationKind::thunder);
        if (thunder.outcome != bw::game::AliciaActionOutcome::started ||
            !thunder.acquire_spell_scope) {
          throw std::runtime_error(
              "Thunder presentation entry was not admitted");
        }
        time.advance(inspection, 0);
        const auto waiting_pose = inspection.player_world_pose()[2];
        const auto waiting_camera = inspection.camera_state()->pose.eye;
        time.advance(inspection, 1.0 / 60.0);
        if (inspection.player_action_status()->outcome !=
                bw::game::AliciaActionOutcome::observation_required ||
            inspection.player_world_pose()[2] != waiting_pose ||
            inspection.camera_state()->pose.eye != waiting_camera) {
          throw std::runtime_error(
              "Missing spell observation consumed simulation");
        }
        // Thunder's original voice event is at frame115 of its start motion.
        // Target acceptance is external; retain aim until that event crosses.
        for (unsigned frame = 0; frame < 120; ++frame) {
          time.advance(inspection, 1.0 / 60.0,
                       {.action = {.spell = {.camera_owned = true}}});
        }
        (void)actions->accept_thunder_target();
        unsigned thunder_releases = 0;
        // The installed strike clip spans 666 source frames; cinematic owner
        // signals alone must not shorten its terminal animation lifetime.
        constexpr unsigned kThunderObservationFrames = 720;
        for (unsigned frame = 0; frame < kThunderObservationFrames; ++frame) {
          time.advance(
              inspection, 1.0 / 60.0,
              {.action = {.spell = {.cinematic_segment_finished = true}}});
          thunder_releases += inspection.player_action_status()
                                  ->release_spell_scope.has_value();
        }
        if (actions->blocks_locomotion() || thunder_releases != 1) {
          throw std::runtime_error("Thunder phase scope did not release once");
        }
        const auto jump_start_y = inspection.player_state()->pose.position[1];
        if (inspection
                .begin_player_scripted_jump({.captured_velocity = {0, 4.2F, 0}})
                .outcome != bw::game::AliciaActionOutcome::started) {
          throw std::runtime_error("Accepted scripted jump was not admitted");
        }
        bool scripted_airborne = false, scripted_recovered = false;
        float scripted_height = 0;
        for (unsigned frame = 0; frame < 180; ++frame) {
          time.advance(inspection, 1.0 / 60.0);
          const auto* player = inspection.player_state();
          const auto height = player->pose.position[1] - jump_start_y;
          if (player->phase == bw::game::PlayerMovementPhase::takeoff &&
              std::abs(height) > .001F) {
            throw std::runtime_error(
                "Scripted start consumed pose-root motion");
          }
          scripted_airborne |=
              player->phase == bw::game::PlayerMovementPhase::airborne;
          scripted_height = std::max(scripted_height, height);
          if (scripted_airborne &&
              player->phase == bw::game::PlayerMovementPhase::grounded) {
            scripted_recovered = true;
            break;
          }
        }
        if (!scripted_airborne || !scripted_recovered ||
            scripted_height < .5F) {
          throw std::runtime_error(
              "Scripted impulse did not traverse flight and landing");
        }
        // Actual player evade waits for the 15-frame start, then consumes
        // authored XYZ motion. Landing cancels at frame 25 played at 2x.
        unsigned takeoff_frames = 0, landing_frames = 0;
        bool airborne = false, recovered = false;
        for (unsigned frame = 0; frame < 180; ++frame) {
          time.advance(inspection, 1.0 / 60.0,
                       {.movement = {.evade_held = frame == 0}});
          const auto phase = inspection.player_state()->phase;
          takeoff_frames += phase == bw::game::PlayerMovementPhase::takeoff;
          landing_frames += phase == bw::game::PlayerMovementPhase::landing;
          airborne |= phase == bw::game::PlayerMovementPhase::airborne;
          if (landing_frames != 0 &&
              phase == bw::game::PlayerMovementPhase::grounded) {
            recovered = true;
            break;
          }
        }
        if (!airborne || !recovered || takeoff_frames < 15 ||
            takeoff_frames > 17 || landing_frames < 13 || landing_frames > 15) {
          throw std::runtime_error(
              "Original evade phase mismatch: takeoff=" +
              std::to_string(takeoff_frames) +
              ", landing=" + std::to_string(landing_frames) +
              ", airborne=" + std::to_string(airborne) +
              ", recovered=" + std::to_string(recovered));
        }
        model_cancellation_substeps(time, inspection);
        const auto start = inspection.player_state()->pose.position;
        for (unsigned frame = 0; frame < 90; ++frame) {
          time.advance(inspection, 1.0 / 60.0, {.movement = {{0, 1}, false}});
        }
        if (!world.view_update ||
            inspection.player_state()->pose.position == start) {
          throw std::runtime_error(
              "Production traversal did not move actor and view");
        }
        if (scene_audio_factory.output->bank_plays.at(0) == 0 ||
            scene_audio_factory.output->bank_plays.at(50) == 0 ||
            scene_random.draws() == 0 ||
            scene_audio_factory.output->listener_updates == 0) {
          throw std::runtime_error(
              "Production motion audio mismatch: common=" +
              std::to_string(scene_audio_factory.output->bank_plays.at(0)) +
              ", voice=" +
              std::to_string(scene_audio_factory.output->bank_plays.at(4)) +
              ", stream=" +
              std::to_string(scene_audio_factory.output->bank_plays.at(50)) +
              ", random=" + std::to_string(scene_random.draws()) +
              ", listener=" +
              std::to_string(scene_audio_factory.output->listener_updates));
        }
        time.set_paused(inspection, true);
        time.advance(inspection, 1.0 / 60.0, {}, true);
        const auto paused_world_seconds = time.clock.simulation_seconds();
        const auto paused_audio_updates = scene_audio_factory.output->updates;
        const auto paused_audio_plays = scene_audio_factory.output->plays;
        const auto paused_pose = inspection.player_state()->pose.position;
        const auto paused_camera = inspection.camera_state()->pose.eye;
        time.advance(inspection, 0.1,
                     {.movement = {{1, 0}, true}, .look = {1, 1}});
        if (time.clock.simulation_seconds() != paused_world_seconds ||
            !inspection.paused() || !scene_audio_factory.output->paused ||
            scene_audio_factory.output->updates != paused_audio_updates ||
            scene_audio_factory.output->plays != paused_audio_plays ||
            inspection.player_state()->pose.position != paused_pose ||
            inspection.camera_state()->pose.eye != paused_camera) {
          throw std::runtime_error("Traversal pause advanced actor or camera");
        }
        // Appearance publication is transactional while traversal is frozen.
        // A renderer failure must retire the candidate and retain the old
        // actor.
        const auto before_handle = loading.resident_player().handle;
        const auto before_phase = inspection.player_state()->phase;
        const auto before_velocity = inspection.player_state()->velocity;
        const auto before_yaw = inspection.player_state()->pose.yaw_degrees;
        const auto before_hips = inspection.player_world_pose()[2];
        world.reject_next_replacement = true;
        bool publication_failed = false;
        try {
          inspection.change_costume(loading, 5);
        } catch (const std::runtime_error&) {
          publication_failed = true;
        }
        if (!publication_failed ||
            loading.resident_player().handle != before_handle ||
            world.live_models != actor_models ||
            world.live_textures != actor_textures ||
            inspection.player_world_pose()[2] != before_hips) {
          throw std::runtime_error(
              "Failed appearance publication changed the scene");
        }
        for (const unsigned costume : {1U, 2U, 3U, 4U, 5U, 0U}) {
          inspection.change_costume(loading, costume);
          time.advance(inspection, 0.1,
                       {.movement = {{1, 0}, true}, .look = {1, 1}});
          if (loading.current_player_costume() != costume ||
              !inspection.paused() ||
              inspection.player_state()->pose.position != paused_pose ||
              inspection.player_state()->pose.yaw_degrees != before_yaw ||
              inspection.player_state()->phase != before_phase ||
              inspection.player_state()->velocity != before_velocity ||
              inspection.camera_state()->pose.eye != paused_camera ||
              inspection.player_world_pose()[2] != before_hips ||
              world.live_models != actor_models) {
            throw std::runtime_error(
                "Costume replacement changed frozen traversal state");
          }
        }
        time.set_paused(inspection, false);
        time.advance(inspection, 1.0 / 60.0, {}, true);
        if (inspection.paused() || scene_audio_factory.output->paused) {
          throw std::runtime_error("Traversal and audio did not resume");
        }
        stage_review_acceptance(time, inspection, loading, world,
                                *scene_audio_factory.output,
                                resources.memory_domain());
        lower_melee_acceptance(time, inspection, loading,
                               *scene_audio_factory.output);
        grounded_action_exit_acceptance(time, inspection, loading,
                                        *scene_audio_factory.output);
        if (inspection.player_draws() != 52 || world.submitted != 4827 ||
            world.live_models != actor_models ||
            world.live_textures != actor_textures) {
          throw std::runtime_error(
              "Player pose publication changed mission residency");
        }
        mission_gameplay_composition(
            time, inspection, loading, scene_random,
            {resources, output, menu.surface_, files, game, profile,
             *scene_audio_factory.output, *storage},
            CorpusPauseRoute::retirement);
      }
      if (world.submitted != 0) {
        throw std::runtime_error("Player scene retained draws after teardown");
      }
      mission.cancel();
      if (mission.progress().phase != bw::game::MissionPhase::cancelled ||
          loading.sky() || resources.acquire("script.cm01_00.dpk") ||
          world.live_models != 0 || world.live_textures != 0) {
        throw std::runtime_error(
            "Mission cancellation retained script or definition ownership");
      }
    }
    if (concept_route) root_retirement_after_teardown(session, media);
    bw::game::Profile reloaded;
    if (reloaded.load(*storage).status != bw::platform::SaveStatus::ok ||
        reloaded.costume() != 5 ||
        reloaded.progression() != expected_progression ||
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
    const auto arguments = std::span(argv, static_cast<std::size_t>(argc));
    if ((arguments.size() != 2 && arguments.size() != 3) ||
        (arguments.size() == 3 &&
         std::string_view(arguments[2]) != "--texture-mips")) {
      throw std::runtime_error(
          "Usage: startup-corpus GAME_DIRECTORY [--texture-mips]");
    }
    const bool texture_mips = arguments.size() == 3;
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
      prepared_game_audio(resources, files, game / "data" / "sound");
      reconstructed_session(resources, files, false, game, texture_mips);
      reconstructed_session(resources, files, true, game, texture_mips);
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
