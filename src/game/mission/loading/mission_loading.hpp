// Mission trap queries borrow both decoded registries as const spans. Design
// R.1 (#rr-raii) retains definitions with their backing bytes; implementation
// R.2 (#rr-use-ptr) preserves bounded views. MEM.9 design and CDSA.32
// (guidelines/cpu-dsa/CDSA.32-transform-static-data-into-the-consumer-layout.md)
// implementation decode once during loading; repeated containment allocates
// nothing and returns only a value. Native callback policy remains game-owned.
// Texture construction policy is captured by value in immutable mission
// options. Preparation alone consumes it; no renderer borrows mutable settings.
// CPU mip candidates and native handles follow E.6 (#re-raii), I.11 (#ri-raw)
// and COPY.5
// (guidelines/copy-move/COPY.5-resource-owning-types-are-move-only.md),
// retrieved for design and implementation: validate then publish, and retain
// current residents on candidate failure. No new frame work or shared mutation.
// Weapon resources are mission-resident, separate from selected model
// instances. Admission prepares all four definitions/support/device owners
// once. Selection only borrows an existing stable entry; publication precedes
// noexcept commit, and discard never retires prepared assets. Cancel requires
// all actor/render borrowers released first. Single main thread, fixed
// four-entry lookup, no I/O or upload during selection. Both-phase Core
// R.20/I.11 and performance CDSA.32/MEM.9 govern ownership, borrowing and
// bounded load preparation. See weapon-selection-residency.md for source
// instance recreation and cost limits. Paused weapon replacement prepares one
// stable candidate before publication. Both phases E.6 (#re-raii) and COPY.5
// preserve rollback and unique device retirement; actor/render borrowers must
// switch before noexcept commit. Initial weapon admission retains RAII
// ownership and never copies device handles (both phases Core R.20 #rr-owner,
// performance COPY.5
// guidelines/copy-move/COPY.5-resource-owning-types-are-move-only.md).
// Appearance replacement prepares a stable-address candidate while current
// views stay valid. The caller publishes replacement draws before noexcept
// commit; discard only retires candidate assets. No frame-time allocations.
// Both-phase Core E.6 (#re-raii), C.83 (#rc-swap) and performance MEM.9
// (guidelines/memory/MEM.9-embedded-allocate-at-init.md) govern rollback,
// publication ordering and user-requested load allocation. See
// research/engine/subsystems/pause-and-costume-switching.md.
// Mission checkpoint placement is an explicit value-returning compatibility
// boundary, independent of reusable player control. Source checkpoint
// thresholds select one of three authored poses with constant work and no
// allocation. Both-phase Core I.1 (#ri-explicit) exposes checkpoint input;
// performance MEM.9 (guidelines/memory/MEM.9-embedded-allocate-at-init.md)
// keeps lookup storage fixed. Actor preparation: both-phase I.11 (#ri-raw)
// bounds mission asset borrowing; CDSA.32
// (guidelines/cpu-dsa/CDSA.32-transform-static-data-into-the-consumer-layout.md)
// resolves names/attachments once before frame-time pose and palette updates.
// Game mission resource and actor-asset ownership. Borrow
// immutable catalogs/files through destruction; retain decoded CPU inputs using
// the resource Domain. Single main-thread caller. Requests are serial and
// execute once when polled; pending/blocked/failed are distinct from successful
// load. Loading-scene activation retains its original item and the injected
// presentation readiness. Safe asset-boundary callbacks keep native loading
// visible without advancing scripts or inspecting a partial world. Sky/stage
// CPU definitions can be inspected separately from actor construction and
// physics simulation. No gameplay start is fabricated. Cancellation drops
// retained definitions and requests. Algorithm: copy request -> construct owned
// definitions -> expose supported native readiness or exact remaining consumer
// -> retain until cancellation. Work follows asset bytes/records; no file reads
// or decode repeated while blocked. Design/implementation E.6 (#re-raii)
// provides partial construction cleanup; cpp-performance MEM.6
// (guidelines/memory/MEM.6-inject-allocators-behind-one-interface.md) retains
// the injected allocator across all owned decoded data.
// Background placement additions apply both-phase C.32 (#rc-dtor-ptr) to
// package-borrowed names and MEM.9 (guidelines/memory/
// MEM.9-embedded-allocate-at-init.md) to load-time metadata.
#pragma once
#include <array>
#include <list>

#include "bw/content/formats/audio_bank.hpp"
#include "bw/content/formats/background_placement.hpp"
#include "bw/content/formats/section_table.hpp"
#include "bw/content/formats/text_table.hpp"
#include "bw/render/scene/world.hpp"
#include "combat_effect_assets.hpp"
#include "game/combat/damage_definition.hpp"
#include "game/combat/weapon_audio.hpp"
#include "game/combat/weapon_definition.hpp"
#include "game/combat/weapon_presentation.hpp"
#include "game/content/original/character_physics_recipe.hpp"
#include "game/content/original/character_support.hpp"
#include "game/content/original/mission_placement.hpp"
#include "game/content/original/npc_palette.hpp"
#include "game/content/original/resident_model.hpp"
#include "game/content/original/stage.hpp"
#include "game/mission/mission_driver.hpp"
#include "mission_behavior_assets.hpp"
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
  // Borrowed callback target must outlive this owner. Invoked on its calling
  // thread at completed asset boundaries, never during native encoding.
  content::LoadProgress progress{};
  bool loading_presentation_ready{};
  bool texture_mips{};
};
struct MissionPhysicsRecipes {
  ScenePhysicsRecipe scene;
  CharacterPhysicsRecipe player, soldier;
};
struct MissionCombatDefinitions {
  StraightWeaponDefinition player, soldier;
  ActorDamageDefinition player_damage, soldier_damage;
  WeaponSoundProfile player_sound, soldier_sound;
  WeaponPresentationDefinition player_presentation, soldier_presentation;
  std::size_t source_query_budget{};
};
struct PlacementDefinition {
  explicit PlacementDefinition(const std::shared_ptr<memory::Domain>& domain)
      : bytes(memory::Allocator<std::uint8_t>(domain)) {}
  resources::ArchiveBytes bytes;
  std::optional<formats::SectionTable> table;
  std::optional<content::MissionPlacementDefinition> population;
};
class MissionLoading final : public MissionLoadServices {
 public:
  MissionLoading(const platform::Files& files,
                 const resources::CatalogSet& catalogs,
                 std::shared_ptr<memory::Domain> domain,
                 render::WorldResources* renderer = nullptr,
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
  // Explicit actor asset admission after mission loading. Selected costume
  // catalogs must already be mounted; immutable assets remain mission-owned.
  // Initial weapon class0..3 selects the original wa0..3 resource. Repeating
  // the same admission is harmless; replacement uses the explicit candidate
  // transaction below. Failed admission owns
  // no player assets; all borrowed views remain tied to this owner.
  void prepare_player(unsigned costume, unsigned weapon_class = 0);
  // Load copied scene/body recipes once from the purchased executable during
  // admission. No executable code is run or retained. The result belongs to
  // this mission until cancel; worlds borrow it only while constructing.
  void prepare_physics();
  const MissionPhysicsRecipes& physics_recipes() const;
  void prepare_combat();
  const MissionCombatDefinitions& combat_definitions() const;
  const MissionBehaviorAssets& combat_behaviors() const;
  const formats::CollisionDefinition& player_hit_definition() const;
  std::span<const CombatEffectPack> combat_effect_packs() const;
  std::span<const std::uint32_t> combat_effect_ids() const;
  ResidentModelView resident_player() const;
  // Requires an admitted player and a different valid costume. Failures leave
  // current views valid. Candidate views survive successful promotion.
  void prepare_player_costume(unsigned costume);
  ResidentModelView candidate_player() const;
  // Call only after all current body borrowers/draws have been replaced.
  // Missing candidate is harmless; no allocations or exceptions occur.
  void commit_player_costume() noexcept;
  void discard_player_costume() noexcept;
  unsigned current_player_costume() const;
  // Select an already admitted immutable weapon resource. Same-class requests
  // are harmless; body and weapon candidates cannot overlap. No reads, decode
  // or upload occur. Candidate/current views remain valid through cancel.
  void prepare_weapon_class(unsigned weapon_class);
  ResidentModelView candidate_weapon() const;
  const content::CharacterSupport& candidate_weapon_support() const;
  unsigned current_weapon_class() const;
  // Only after actor/catalog/palette/draw borrowers switch successfully. No
  // allocation or exceptions; discard drops only candidate selection. No ammo,
  // action clocks, body pose or camera state is owned by this transaction.
  void commit_weapon_class() noexcept;
  void discard_weapon_class() noexcept;
  ResidentModelView resident_weapon() const;
  const content::CharacterSupport& player_support() const;
  const content::CharacterSupport& weapon_support() const;
  // Population preparation borrows already resident family models and support.
  // Resolve once per archetype at setup, never per frame or actor admission.
  // Missing prepared identities throw; these calls perform no I/O or uploads.
  // Setup-only runtime table snapshot. Names borrow this loader; handles
  // observe existing residency and confer no resource mutation authority.
  memory::Vector<ResidentTextureSlot> npc_texture_slots(
      const ResidentModelView& model) const;
  ResidentModelView resident_character(std::string_view family) const;
  const content::CharacterSupport& character_support(
      std::string_view owner) const;
  const formats::ModelDefinition& shared_motion_model(
      std::string_view owner) const;
  std::span<const NpcPaletteBinding> npc_palettes() const { return palette_; }
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
  // Scene reflection bindings use this stable texture array through mission
  // cancellation. The array follows StageDefinition::textures order.
  std::span<const render::WorldTextureHandle> resident_textures() const {
    return stage_residency_ ? std::span<const render::WorldTextureHandle>(
                                  stage_residency_->textures)
                            : std::span<const render::WorldTextureHandle>{};
  }
  // Immutable authored light data borrows the mission residency lifetime.
  const content::UserDefinition* character_light() const noexcept {
    return character_light_ ? &*character_light_ : nullptr;
  }
  const content::UserDefinition* enemy_light() const noexcept {
    return enemy_light_ ? &*enemy_light_ : nullptr;
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
  // Immutable registration data, never live actors. Backing bytes and decoded
  // names remain stable until cancellation; consumers retire before this owner.
  const content::MissionPlacementDefinition* population_definition() const {
    const auto& enemy = placements_.at(1);
    return enemy && enemy->population ? &*enemy->population : nullptr;
  }
  // Read both native placement registries for a player trap. No mutable
  // table capability escapes; absent tables contribute no containing shape.
  bool contains_mission_trap(std::int32_t event,
                             const math::Vector3<float>& position) const;
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
  void preload_shared_motion(std::string_view owner);
  void load_npc_palette();
  void preload_model(std::string_view name);
  struct ResidentDefinition {
    ResidentDefinition(render::WorldResources& renderer,
                       const content::StageDefinition& definition,
                       const std::shared_ptr<memory::Domain>& domain,
                       const content::LoadProgress& progress = {},
                       bool texture_mips = false);
    ~ResidentDefinition();
    ResidentDefinition(const ResidentDefinition&) = delete;
    ResidentDefinition& operator=(const ResidentDefinition&) = delete;
    ResidentDefinition(ResidentDefinition&&) = delete;
    ResidentDefinition& operator=(ResidentDefinition&&) = delete;
    void release() noexcept;
    render::WorldResources& renderer;
    memory::Vector<render::WorldModelHandle> models;
    memory::Vector<render::WorldTextureHandle> textures;
  };
  const platform::Files& files_;
  const resources::CatalogSet& catalogs_;
  std::shared_ptr<memory::Domain> memory_;
  render::WorldResources* renderer_{};
  const MissionLoadOptions options_;
  std::optional<MissionPhysicsRecipes> physics_recipes_;
  std::optional<MissionCombatDefinitions> combat_definitions_;
  std::optional<MissionBehaviorAssets> combat_behaviors_;
  std::optional<content::StageDefinition> player_hit_definition_;
  std::optional<CombatEffectAssets> combat_effect_assets_;
  std::array<std::uint32_t, 2 * (WeaponEffects::kSurfaceCount + 4) + 1>
      combat_effect_ids_{};
  std::size_t combat_effect_count_{};
  memory::String request_name_, stage_name_;
  std::optional<MissionLoadKind> request_kind_;
  bool pending_{};
  MissionLoadReport report_;
  LoadingSceneState loading_scene_;
  std::optional<content::UserDefinition> character_light_, enemy_light_;
  std::optional<content::StageDefinition> sky_, stage_;
  std::optional<ResidentDefinition> sky_residency_, stage_residency_;
  struct PlayerBody {
    content::StageDefinition definition;
    std::optional<ResidentDefinition> residency;
    unsigned costume{};
    explicit PlayerBody(content::StageDefinition value, unsigned selected)
        : definition(std::move(value)), costume(selected) {}
    ResidentModelView view() const;
  };
  std::shared_ptr<PlayerBody> player_body_, candidate_body_;
  struct PlayerWeapon {
    content::StageDefinition definition;
    content::CharacterSupport support;
    std::optional<ResidentDefinition> residency;
    unsigned weapon_class{};
    PlayerWeapon(content::StageDefinition model,
                 content::CharacterSupport motions, unsigned selected)
        : definition(std::move(model)),
          support(std::move(motions)),
          weapon_class(selected) {}
    ResidentModelView view() const;
  };
  std::array<std::shared_ptr<PlayerWeapon>, 4> prepared_weapons_;
  std::shared_ptr<PlayerWeapon> player_weapon_, candidate_weapon_;
  std::shared_ptr<PlayerWeapon> prepare_weapon(unsigned weapon_class,
                                               bool report_progress);
  std::optional<content::CharacterSupport> player_support_;
  std::optional<unsigned> player_costume_;
  memory::Vector<content::StageDefinition> dependency_definitions_;
  std::list<ResidentDefinition, memory::Allocator<ResidentDefinition>>
      dependency_residencies_;
  memory::Vector<memory::String> dependency_names_;
  memory::Vector<content::CharacterSupport> character_support_;
  memory::Vector<memory::String> support_names_;
  struct MotionModel {
    memory::String owner;
    formats::ModelDefinition definition;
  };
  memory::Vector<MotionModel> motion_models_;
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
