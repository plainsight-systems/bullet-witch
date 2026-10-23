// Game mission load ownership for the current static-definition slice. Borrow
// immutable catalogs/files through destruction; retain decoded CPU inputs using
// the resource Domain. Single main-thread caller. Requests are serial and
// execute once when polled; pending/blocked/failed are distinct from successful
// load. Loading-scene activation retains its original item, but its animated
// renderer remains an explicit presentation gap. Sky/stage CPU definitions can
// be inspected separately from actor construction and physics simulation.
// No gameplay start is fabricated. Cancellation drops retained definitions and
// requests. Algorithm: copy request -> construct owned definitions -> expose
// supported native readiness or exact remaining consumer -> retain until
// cancellation. Work follows asset bytes/records; no file reads or decode
// repeated while blocked. Design/implementation E.6 (#re-raii) provides partial
// construction cleanup; cpp-performance MEM.6
// (guidelines/memory/MEM.6-inject-allocators-behind-one-interface.md) retains
// the injected allocator across all owned decoded data.
// Background placement additions apply both-phase C.32 (#rc-dtor-ptr) to
// package-borrowed names and MEM.9 (guidelines/memory/
// MEM.9-embedded-allocate-at-init.md) to load-time metadata.
#pragma once
#include <array>
#include <list>

#include "bw/content/character_support.hpp"
#include "bw/content/stage.hpp"
#include "bw/formats/audio_bank.hpp"
#include "bw/formats/background_placement.hpp"
#include "bw/formats/section_table.hpp"
#include "bw/formats/text_table.hpp"
#include "bw/render/world.hpp"
#include "mission_driver.hpp"
#include "npc_palette.hpp"
#include "resident_model.hpp"
namespace bw::game {
struct LoadingSceneState {
  unsigned item{};
  bool active{};
  bool presentation_ready{};
};
struct MissionPackageInput {
  bool requested{};
  bool present{};
  std::span<const std::uint8_t> bytes;
};
// Original cover manager owns at most 256 active six-scalar definitions. The
// AI interpretation belongs to later gameplay; load construction is complete.
struct CoverPointDefinition {
  static constexpr std::size_t kCoordinateCount = 6;
  std::array<float, kCoordinateCount> values{};
};
struct MissionCallbackDefinition {
  std::int32_t event{-1};
  std::string_view function;
  bool active{};
};
struct MissionLoadOptions {
  std::filesystem::path game_directory;
  unsigned difficulty{};
};
struct PlacementDefinition {
  explicit PlacementDefinition(const std::shared_ptr<memory::Domain>& domain)
      : bytes(memory::Allocator<std::uint8_t>(domain)) {}
  resources::ArchiveBytes bytes;
  std::optional<formats::SectionTable> table;
};
class MissionLoading final : public MissionLoadServices {
 public:
  MissionLoading(const platform::Files& files,
                 const resources::CatalogSet& catalogs,
                 std::shared_ptr<memory::Domain> domain,
                 render::WorldOutput* renderer = nullptr,
                 MissionLoadOptions options = {});
  ~MissionLoading() override = default;
  MissionLoading(const MissionLoading&) = delete;
  MissionLoading& operator=(const MissionLoading&) = delete;
  MissionLoading(MissionLoading&&) = delete;
  MissionLoading& operator=(MissionLoading&&) = delete;
  void retain_package(resources::NamedFilePackage package) override;
  void begin_loading_scene(unsigned item) override;
  void request(MissionLoadKind kind, std::string_view name) override;
  MissionLoadReport poll() override;
  void cancel() noexcept override;
  // Prepare shared stage owners once; poll separately gates constructor
  // readiness.
  void prepare_stage(std::string_view stage);
  // Handles align with their retained definition model vectors and borrow this
  // owner until cancellation. CPU definition presence does not imply residency.
  std::span<const render::WorldModelHandle> resident_models() const {
    return stage_residency_ ? std::span<const render::WorldModelHandle>(
                                  stage_residency_->models)
                            : std::span<const render::WorldModelHandle>{};
  }
  std::span<const render::WorldModelHandle> sky_resident_models() const {
    return sky_residency_ ? std::span<const render::WorldModelHandle>(
                                sky_residency_->models)
                          : std::span<const render::WorldModelHandle>{};
  }
  const std::optional<content::StageDefinition>& stage() const {
    return stage_;
  }
  const std::optional<content::StageDefinition>& sky() const { return sky_; }
  const std::optional<formats::AudioBankDefinition>& sound_bank() const {
    return sound_bank_;
  }
  std::span<const MissionCallbackDefinition> callbacks() const {
    return callbacks_;
  }
  // Borrow authored placements through cancellation. This definition does not
  // execute _SetBgObj or own a simulated destructible actor.
  const std::optional<formats::BackgroundPlacementDefinition>& background()
      const {
    return background_;
  }
  // Snapshot stable model/device associations once after loading. Returned
  // metadata uses the mission domain and borrows until cancellation.
  memory::Vector<ResidentModelView> resident_dependencies() const;
  std::size_t npc_palette_bindings() const { return palette_.size(); }
  std::size_t character_support_sets() const {
    return character_support_.size();
  }
  std::size_t preloaded_models() const { return dependency_names_.size(); }
  const std::optional<formats::TextTable>& text_table() const {
    return text_table_;
  }
  std::span<const CoverPointDefinition> cover_points() const {
    return cover_points_;
  }
  LoadingSceneState loading_scene() const { return loading_scene_; }
  // Borrow the retained optional package member until cancellation.
  const MissionPackageInput& package_input(MissionLoadKind kind) const;

 private:
  MissionPackageInput& mutable_package_input(MissionLoadKind kind);
  void load_package_input(MissionLoadKind kind);
  std::span<const std::uint8_t> optional_member(std::string_view name) const;
  void load_cover_points();
  void load_text();
  void load_sound();
  void load_background();
  void load_placements();
  void preload_character(std::string_view name);
  void load_npc_palette();
  void preload_model(std::string_view name);
  struct ResidentDefinition {
    ResidentDefinition(render::WorldOutput& renderer,
                       const content::StageDefinition& definition,
                       const std::shared_ptr<memory::Domain>& domain);
    ~ResidentDefinition();
    ResidentDefinition(const ResidentDefinition&) = delete;
    ResidentDefinition& operator=(const ResidentDefinition&) = delete;
    ResidentDefinition(ResidentDefinition&&) = delete;
    ResidentDefinition& operator=(ResidentDefinition&&) = delete;
    void release() noexcept;
    render::WorldOutput& renderer;
    memory::Vector<render::WorldModelHandle> models;
    memory::Vector<render::WorldTextureHandle> textures;
  };
  const platform::Files& files_;
  const resources::CatalogSet& catalogs_;
  std::shared_ptr<memory::Domain> memory_;
  render::WorldOutput* renderer_{};
  MissionLoadOptions options_;
  memory::String request_name_, stage_name_;
  std::optional<MissionLoadKind> request_kind_;
  bool pending_{};
  MissionLoadReport report_;
  LoadingSceneState loading_scene_;
  std::optional<content::UserDefinition> character_light_, enemy_light_;
  std::optional<content::StageDefinition> sky_, stage_;
  std::optional<ResidentDefinition> sky_residency_, stage_residency_;
  memory::Vector<content::StageDefinition> dependency_definitions_;
  std::list<ResidentDefinition, memory::Allocator<ResidentDefinition>>
      dependency_residencies_;
  memory::Vector<memory::String> dependency_names_;
  memory::Vector<content::CharacterSupport> character_support_;
  memory::Vector<memory::String> support_names_;
  std::optional<content::StageDefinition> palette_definition_;
  std::optional<ResidentDefinition> palette_residency_;
  memory::Vector<NpcPaletteBinding> palette_;
  std::optional<formats::BackgroundPlacementDefinition> background_;
  std::optional<resources::NamedFilePackage> package_;
  std::optional<resources::ResourceArchive> sound_archive_;
  std::optional<formats::AudioBankDefinition> sound_bank_;
  std::optional<std::uint32_t> sound_slot_;
  std::array<std::optional<PlacementDefinition>, 2> placements_;
  static constexpr std::size_t kMaximumCallbacks = 64;
  std::array<MissionCallbackDefinition, kMaximumCallbacks> callbacks_{};
  resources::ArchiveBytes text_override_;
  std::optional<formats::TextTable> text_table_;
  memory::Vector<CoverPointDefinition> cover_points_;
  MissionPackageInput score_, sound_environment_, sound_reverb_, magic_, paths_;
};
}  // namespace bw::game
