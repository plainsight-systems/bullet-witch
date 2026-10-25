// Mip preparation consumes an immutable launch policy and creates independent
// CPU candidates before native publication. Both-phase E.6 (#re-raii), I.11
// (#ri-raw), COPY.5 (guidelines/copy-move/
// COPY.5-resource-owning-types-are-move-only.md) preserve rollback and
// borrowing. Immutable weapon acquisition belongs to mission admission; action
// selection retains a prepared owner and recreates only its selected model
// state. Both phases R.20/I.11/CDSA.32/MEM.9 apply as detailed in
// mission_loading.hpp. Initial weapon admission: both-phase R.20/COPY.5
// preserve the existing RAII rollback scope and unique device owners; selection
// adds no copy path. Candidate body ownership uses domain-backed stable
// storage. Decode/upload completes before publication; RAII rolls failures
// back, noexcept promotion retires prior resources only after the caller
// replaces all borrowers. Both phases Core E.6 (#re-raii), C.83 (#rc-swap),
// performance MEM.9 (guidelines/memory/MEM.9-embedded-allocate-at-init.md).
// Checkpoint placement uses explicit input and a returned pose (both-phase
// Core I.1, #ri-explicit). The unchanged authored threshold lookup performs
// constant work with fixed values and no allocation (both-phase MEM.9,
// guidelines/memory/MEM.9-embedded-allocate-at-init.md).
// Loading presentation uses explicit asset-boundary callbacks (design Core I.1
// #ri-explicit, implementation E.6 #re-raii). Performance GPU.1 retains
// uploads; callbacks pump only completed loading UI and never inspect partial
// owners. Actor preparation: both-phase I.11 (#ri-raw) bounds mission asset
// borrowing; CDSA.32
// (guidelines/cpu-dsa/CDSA.32-transform-static-data-into-the-consumer-layout.md)
// resolves names/attachments once before frame-time pose and palette updates.
// Own serial mission-resource construction and retained package inputs.
// Character-light loads retain original definitions; sky loads construct real
// native mesh/texture objects. Stage CPU constructors and device residency
// are distinct prerequisites. Light definitions retain original input
// lifetime without claiming a lighting evaluator. Loading scene activation is
// an immediate task registration, not a wait or presentation-complete signal.
// Design/implementation E.6 (#re-raii) keeps failures/cancellation bounded;
// cpp-performance MEM.6
// (guidelines/memory/MEM.6-inject-allocators-behind-one-interface.md) passes
// the explicit domain to every retained resource owner. Each request does
// O(input bytes) synchronous setup once; blocked polls perform no asset work.
#include "mission_loading.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

#include "bw/formats/section_table.hpp"
#include "bw/formats/texture_mips.hpp"
#include "bw/formats/world_texture.hpp"
namespace bw::game {
MissionLoading::MissionLoading(const platform::Files& files,
                               const resources::CatalogSet& catalogs,
                               std::shared_ptr<memory::Domain> domain,
                               render::WorldResources* renderer,
                               MissionLoadOptions options)
    : files_(files),
      catalogs_(catalogs),
      memory_(std::move(domain)),
      renderer_(renderer),
      options_(std::move(options)),
      request_name_(memory::Allocator<char>(memory_)),
      stage_name_(memory::Allocator<char>(memory_)),
      dependency_definitions_(
          memory::Allocator<content::StageDefinition>(memory_)),
      dependency_residencies_(memory::Allocator<ResidentDefinition>(memory_)),
      dependency_names_(memory::Allocator<memory::String>(memory_)),
      character_support_(memory::Allocator<content::CharacterSupport>(memory_)),
      support_names_(memory::Allocator<memory::String>(memory_)),
      palette_(memory::Allocator<NpcPaletteBinding>(memory_)),
      text_override_(memory::Allocator<std::uint8_t>(memory_)),
      cover_points_(memory::Allocator<CoverPointDefinition>(memory_)) {}
MissionLoading::ResidentDefinition::ResidentDefinition(
    render::WorldResources& output, const content::StageDefinition& definition,
    const std::shared_ptr<memory::Domain>& domain,
    const content::LoadProgress& progress, bool texture_mips)
    : renderer(output),
      models(memory::Allocator<render::WorldModelHandle>(domain)),
      textures(memory::Allocator<render::WorldTextureHandle>(domain)) {
  models.reserve(definition.models.size());
  textures.reserve(definition.textures.size());
  try {
    // Decode original pixel formats before constructing device-owned textures.
    for (std::size_t index = 0; index < definition.textures.size(); ++index) {
      auto pixels = formats::decode_world_texture(
          {definition.texture_bytes(index), definition.texture_metadata(index),
           definition.textures.at(index).cube
               ? formats::WorldTextureShape::cube
               : formats::WorldTextureShape::two_dimensional},
          memory::Allocator<std::uint8_t>(domain));
      if (texture_mips) {
        pixels = formats::generate_texture_mips(
            pixels, memory::Allocator<std::uint8_t>(domain));
      }
      textures.push_back(renderer.upload_world_texture(pixels));
      // Only completed uploads yield to loading presentation. E.6 preserves
      // partial-owner cleanup; GPU.1 leaves all admitted pixels resident.
      if (progress) progress();
    }
    memory::Vector<render::WorldTextureHandle> table{
        memory::Allocator<render::WorldTextureHandle>(domain)};
    for (const auto& model : definition.models) {
      const auto handle = renderer.upload_model(model.definition);
      models.push_back(handle);
      table.clear();
      for (const auto index : model.textures) {
        table.push_back(textures.at(index));
      }
      renderer.bind_model_textures(handle, table);
      if (!renderer.geometry_status(handle).texture_bindings_ready) {
        throw std::runtime_error(
            "World model texture publication did not become ready");
      }
      if (progress) progress();
    }
  } catch (...) {
    release();
    throw;
  }
}
MissionLoading::ResidentDefinition::~ResidentDefinition() { release(); }
// Stage load readiness and actor admission remain separate. Any failed actor
// upload retires its partial device owners without replacing mission state.
void MissionLoading::prepare_player(unsigned costume, unsigned weapon_class) {
  constexpr unsigned kMaximumCostume = 5, kWeaponClasses = 4;
  if (costume > kMaximumCostume || weapon_class >= kWeaponClasses ||
      renderer_ == nullptr) {
    throw std::invalid_argument(
        "Player assets require a valid costume, weapon class and renderer");
  }
  if (player_costume_) {
    if (*player_costume_ != costume ||
        (!player_weapon_ || player_weapon_->weapon_class != weapon_class)) {
      throw std::logic_error(
          "Player assets already admitted for another costume or weapon class");
    }
    return;
  }
  const auto name = memory::String("pw", memory::Allocator<char>(memory_)) +
                    static_cast<char>('0' + costume);
  try {
    player_body_ = std::allocate_shared<PlayerBody>(
        memory::Allocator<PlayerBody>(memory_),
        content::load_model_definition(files_, catalogs_, name, memory_),
        costume);
    for (unsigned index = 0; index < prepared_weapons_.size(); ++index) {
      prepared_weapons_.at(index) = prepare_weapon(index, true);
    }
    player_weapon_ = prepared_weapons_.at(weapon_class);
    player_support_ =
        content::load_character_support(files_, catalogs_, name, memory_);
    if (player_body_->definition.models.size() != 1) {
      throw std::runtime_error(
          "Player or weapon model association is ambiguous");
    }
    player_body_->residency.emplace(*renderer_, player_body_->definition,
                                    memory_, options_.progress,
                                    options_.texture_mips);
    player_costume_ = costume;
  } catch (...) {
    player_weapon_.reset();
    prepared_weapons_.fill({});
    player_body_.reset();
    player_support_.reset();
    candidate_weapon_.reset();
    candidate_body_.reset();
    throw;
  }
}
ResidentModelView MissionLoading::PlayerBody::view() const {
  if (!residency) throw std::logic_error("Player residency is not available");
  const auto& model = definition.models.front();
  std::span<const std::uint8_t> control_points;
  if (model.control_points) {
    const auto& location = *model.control_points;
    control_points =
        definition.archives.at(location.archive).payload(location.record);
  }
  return {model.name, model.definition, residency->models.front(),
          {},         nullptr,          control_points};
}
ResidentModelView MissionLoading::resident_player() const {
  if (!player_body_) throw std::logic_error("Player assets are not admitted");
  return player_body_->view();
}
unsigned MissionLoading::current_player_costume() const {
  if (!player_body_) throw std::logic_error("Player assets are not admitted");
  return player_body_->costume;
}
// Keep both CPU archives and native handles isolated until actor preparation
// succeeds. A local owner also rolls back a partial upload automatically.
void MissionLoading::prepare_player_costume(unsigned costume) {
  constexpr unsigned kMaximumCostume = 5;
  if (!player_body_ || renderer_ == nullptr || costume > kMaximumCostume ||
      costume == player_body_->costume) {
    throw std::invalid_argument(
        "Costume replacement needs a different admitted body");
  }
  if (candidate_body_ || candidate_weapon_) {
    throw std::logic_error("Appearance candidate already prepared");
  }
  const auto name = memory::String("pw", memory::Allocator<char>(memory_)) +
                    static_cast<char>('0' + costume);
  auto candidate = std::allocate_shared<PlayerBody>(
      memory::Allocator<PlayerBody>(memory_),
      content::load_model_definition(files_, catalogs_, name, memory_),
      costume);
  if (candidate->definition.models.size() != 1) {
    throw std::runtime_error("Costume model association is ambiguous");
  }
  candidate->residency.emplace(*renderer_, candidate->definition, memory_,
                               content::LoadProgress{}, options_.texture_mips);
  candidate_body_ = std::move(candidate);
}
ResidentModelView MissionLoading::candidate_player() const {
  if (!candidate_body_) {
    throw std::logic_error("Costume candidate is not prepared");
  }
  return candidate_body_->view();
}
void MissionLoading::commit_player_costume() noexcept {
  if (!candidate_body_) return;
  player_body_.swap(candidate_body_);
  player_costume_ = player_body_->costume;
  candidate_body_.reset();
}
void MissionLoading::discard_player_costume() noexcept {
  candidate_body_.reset();
}
ResidentModelView MissionLoading::PlayerWeapon::view() const {
  if (!residency) throw std::logic_error("Weapon residency is not available");
  const auto& model = definition.models.front();
  return {model.name, model.definition, residency->models.front(), {}};
}
std::shared_ptr<MissionLoading::PlayerWeapon> MissionLoading::prepare_weapon(
    unsigned weapon_class, bool report_progress) {
  const auto name = memory::String("wa", memory::Allocator<char>(memory_)) +
                    static_cast<char>('0' + weapon_class);
  auto weapon = std::allocate_shared<PlayerWeapon>(
      memory::Allocator<PlayerWeapon>(memory_),
      content::load_model_definition(files_, catalogs_, name, memory_),
      content::load_character_support(files_, catalogs_, name, memory_),
      weapon_class);
  if (weapon->definition.models.size() != 1) {
    throw std::runtime_error("Weapon model association is ambiguous");
  }
  weapon->residency.emplace(
      *renderer_, weapon->definition, memory_,
      report_progress ? options_.progress : std::function<void()>{},
      options_.texture_mips);
  return weapon;
}
// Keep the active model/support at stable addresses until the actor and
// renderer finish preparing their matching class-specific replacement.
void MissionLoading::prepare_weapon_class(unsigned weapon_class) {
  constexpr unsigned kWeaponClasses = 4;
  if (!player_costume_ || !player_weapon_ || renderer_ == nullptr ||
      weapon_class >= kWeaponClasses) {
    throw std::invalid_argument(
        "Weapon replacement needs an admitted player and class");
  }
  if (candidate_weapon_ || candidate_body_) {
    throw std::logic_error("Appearance candidate already prepared");
  }
  if (weapon_class == player_weapon_->weapon_class) return;
  candidate_weapon_ = prepared_weapons_.at(weapon_class);
  if (!candidate_weapon_) {
    throw std::logic_error("Weapon resource was not admitted with the mission");
  }
}
ResidentModelView MissionLoading::candidate_weapon() const {
  if (!candidate_weapon_) {
    throw std::logic_error("Weapon candidate is not prepared");
  }
  return candidate_weapon_->view();
}
const content::CharacterSupport& MissionLoading::candidate_weapon_support()
    const {
  if (!candidate_weapon_) {
    throw std::logic_error("Weapon candidate is not prepared");
  }
  return candidate_weapon_->support;
}
unsigned MissionLoading::current_weapon_class() const {
  if (!player_weapon_ || !player_costume_) {
    throw std::logic_error("Player assets are not admitted");
  }
  return player_weapon_->weapon_class;
}
void MissionLoading::commit_weapon_class() noexcept {
  if (!candidate_weapon_) return;
  player_weapon_.swap(candidate_weapon_);
  candidate_weapon_.reset();
}
void MissionLoading::discard_weapon_class() noexcept {
  candidate_weapon_.reset();
}
ResidentModelView MissionLoading::resident_weapon() const {
  if (!player_costume_ || !player_weapon_) {
    throw std::logic_error("Player assets are not admitted");
  }
  return player_weapon_->view();
}
const content::CharacterSupport& MissionLoading::player_support() const {
  if (!player_costume_) {
    throw std::logic_error("Player assets are not admitted");
  }
  if (!player_support_) {
    throw std::logic_error("Player support is not available");
  }
  return *player_support_;
}
const content::CharacterSupport& MissionLoading::weapon_support() const {
  if (!player_costume_ || !player_weapon_) {
    throw std::logic_error("Player assets are not admitted");
  }
  return player_weapon_->support;
}
void MissionLoading::ResidentDefinition::release() noexcept {
  for (const auto model : models) {
    renderer.release_model(model);
  }
  models.clear();
  for (const auto texture : textures) {
    renderer.release_world_texture(texture);
  }
  textures.clear();
}
void MissionLoading::retain_package(resources::NamedFilePackage package) {
  if (package_) {
    throw std::logic_error("Mission package already retained");
  }
  package_ = std::move(package);
}
const MissionPackageInput& MissionLoading::package_input(
    MissionLoadKind kind) const {
  switch (kind) {
    case MissionLoadKind::score:
      return score_;
    case MissionLoadKind::sound_environment:
      return sound_environment_;
    case MissionLoadKind::sound_reverb:
      return sound_reverb_;
    case MissionLoadKind::magic:
      return magic_;
    case MissionLoadKind::paths:
      return paths_;
    default:
      throw std::invalid_argument("Request is not an optional package input");
  }
}
MissionPackageInput& MissionLoading::mutable_package_input(
    MissionLoadKind kind) {
  switch (kind) {
    case MissionLoadKind::score:
      return score_;
    case MissionLoadKind::sound_environment:
      return sound_environment_;
    case MissionLoadKind::sound_reverb:
      return sound_reverb_;
    case MissionLoadKind::magic:
      return magic_;
    case MissionLoadKind::paths:
      return paths_;
    default:
      throw std::invalid_argument("Request is not an optional package input");
  }
}
void MissionLoading::load_package_input(MissionLoadKind kind) {
  auto member = request_name_;
  member += kind == MissionLoadKind::paths ? ".arc" : ".settbl";
  if (!package_) {
    throw std::logic_error("Mission package is not retained");
  }
  const auto& package = package_.value();
  auto& retained = mutable_package_input(kind);
  // Native workers only replace a pointer on successful lookup. Fresh mission
  // reset starts these optional inputs empty; absent lookup stays
  // empty.
  for (const auto& entry : package.members()) {
    if (entry.name == member) {
      retained.bytes = package.member(member);
      retained.present = true;
      break;
    }
  }
  retained.requested = true;
}
void MissionLoading::begin_loading_scene(unsigned item) {
  constexpr unsigned kLoadingItems = 19;
  if (item >= kLoadingItems) {
    throw std::invalid_argument("Loading scene item out of range");
  }
  loading_scene_ = {item, true, options_.loading_presentation_ready};
  if (options_.progress) options_.progress();
}
void MissionLoading::request(MissionLoadKind kind, std::string_view name) {
  if (pending_ || report_.state == MissionLoadState::blocked) {
    throw std::logic_error("Mission load request overlaps pending native work");
  }
  request_name_ = name;
  request_kind_ = kind;
  pending_ = true;
  report_ = {MissionLoadState::pending, {}};
}
void MissionLoading::prepare_stage(std::string_view stage) {
  if (stage_ && stage_name_ == stage &&
      (renderer_ == nullptr || stage_residency_)) {
    return;
  }
  // Publish the complete CPU snapshot atomically; exceptions retain old state.
  auto decoded = content::load_stage_definition(files_, catalogs_, stage,
                                                memory_, options_.progress);
  stage_name_ = stage;
  stage_ = std::move(decoded);
  if (renderer_ != nullptr) {
    stage_residency_.emplace(*renderer_, stage_.value(), memory_,
                             options_.progress, options_.texture_mips);
  }
}
MissionLoadReport MissionLoading::poll() {
  if (!pending_) return report_;
  pending_ = false;
  try {
    if (!request_kind_) {
      throw std::logic_error("Pending mission request has no kind");
    }
    const auto kind = request_kind_.value();
    switch (kind) {
      case MissionLoadKind::character_light: {
        auto prefix =
            memory::String("charalight.", memory::Allocator<char>(memory_));
        prefix += request_name_;
        auto light = content::load_user_definition(files_, catalogs_,
                                                   prefix + "_cl", memory_);
        auto enemy = content::load_user_definition(files_, catalogs_,
                                                   prefix + "_el", memory_);
        character_light_ = std::move(light);
        enemy_light_ = std::move(enemy);
        report_ = {MissionLoadState::ready, {}};
        break;
      }
      case MissionLoadKind::sky:
        sky_ = content::load_model_definition(files_, catalogs_, request_name_,
                                              memory_);
        if (renderer_ != nullptr) {
          sky_residency_.emplace(*renderer_, sky_.value(), memory_,
                                 options_.progress, options_.texture_mips);
          report_ = {MissionLoadState::ready, {}};
        } else {
          report_ = {MissionLoadState::blocked,
                     "Sky CPU definitions retained; native mesh/texture output "
                     "is unavailable"};
        }
        break;
      case MissionLoadKind::stage:
        prepare_stage(request_name_);
        if (!stage_) {
          throw std::logic_error("Stage definitions were not retained");
        }
        if (const auto pending = std::ranges::find_if(
                stage_->inputs,
                [](const auto& input) { return !input.definition_ready; });
            pending != stage_->inputs.end()) {
          report_ = {
              MissionLoadState::blocked,
              "Stage constructor pending: " + std::string(pending->name) +
                  ": " + std::string(pending->unresolved)};
        } else if (!stage_residency_) {
          report_ = {
              MissionLoadState::blocked,
              "Stage definitions constructed; device output unavailable"};
        } else {
          report_ = {MissionLoadState::ready, {}};
        }
        break;
      case MissionLoadKind::sound:
        load_sound();
        report_ = {MissionLoadState::ready, {}};
        break;
      case MissionLoadKind::placements:
        load_placements();
        report_ = {renderer_ != nullptr ? MissionLoadState::ready
                                        : MissionLoadState::blocked,
                   renderer_ != nullptr ? ""
                                        : "Placement definitions constructed; "
                                          "device output unavailable"};
        break;
      case MissionLoadKind::background_objects:
        load_background();
        report_ = {
            renderer_ != nullptr ? MissionLoadState::ready
                                 : MissionLoadState::blocked,
            renderer_ != nullptr
                ? ""
                : "Background definitions retained; device output unavailable"};
        break;
      case MissionLoadKind::text:
        load_text();
        report_ = {MissionLoadState::ready, {}};
        break;
      case MissionLoadKind::hiding_places:
        load_cover_points();
        report_ = {MissionLoadState::ready, {}};
        break;
      case MissionLoadKind::score:
      case MissionLoadKind::sound_environment:
      case MissionLoadKind::sound_reverb:
      case MissionLoadKind::magic:
      case MissionLoadKind::paths:
        load_package_input(kind);
        report_ = {MissionLoadState::ready, {}};
        break;
      default:
        report_ = {MissionLoadState::blocked,
                   "Mission native load consumer is not implemented: " +
                       std::string(request_name_)};
        break;
    }
  } catch (const resources::Error& failure) {
    report_ = {failure.code == resources::ErrorCode::unsupported
                   ? MissionLoadState::blocked
                   : MissionLoadState::failed,
               failure.what()};
  } catch (const std::exception& failure) {
    report_ = {MissionLoadState::failed, failure.what()};
  }
  return report_;
}
void MissionLoading::cancel() noexcept {
  pending_ = false;
  request_kind_.reset();
  loading_scene_ = {};
  player_costume_.reset();
  candidate_weapon_.reset();
  player_weapon_.reset();
  prepared_weapons_.fill({});
  player_body_.reset();
  player_support_.reset();
  candidate_body_.reset();
  palette_.clear();
  palette_residency_.reset();
  palette_definition_.reset();
  character_support_.clear();
  support_names_.clear();
  dependency_residencies_.clear();
  dependency_definitions_.clear();
  dependency_names_.clear();
  stage_residency_.reset();
  sky_residency_.reset();
  sky_.reset();
  stage_.reset();
  character_light_.reset();
  enemy_light_.reset();
  cover_points_.clear();
  score_ = {};
  sound_environment_ = {};
  sound_reverb_ = {};
  magic_ = {};
  paths_ = {};
  background_.reset();
  for (auto& placement : placements_) placement.reset();
  callbacks_.fill({});
  text_table_.reset();
  text_override_.clear();
  sound_bank_.reset();
  sound_archive_.reset();
  sound_slot_.reset();
  package_.reset();
}
}  // namespace bw::game
