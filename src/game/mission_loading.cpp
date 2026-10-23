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
#include "bw/formats/world_texture.hpp"
namespace bw::game {
MissionLoading::MissionLoading(const platform::Files& files,
                               const resources::CatalogSet& catalogs,
                               std::shared_ptr<memory::Domain> domain,
                               render::WorldOutput* renderer,
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
    render::WorldOutput& output, const content::StageDefinition& definition,
    const std::shared_ptr<memory::Domain>& domain)
    : renderer(output),
      models(memory::Allocator<render::WorldModelHandle>(domain)),
      textures(memory::Allocator<render::WorldTextureHandle>(domain)) {
  models.reserve(definition.models.size());
  textures.reserve(definition.textures.size());
  try {
    // Decode original pixel formats before constructing device-owned textures.
    for (std::size_t index = 0; index < definition.textures.size(); ++index) {
      const auto pixels = formats::decode_world_texture(
          {definition.texture_bytes(index), definition.texture_metadata(index),
           definition.textures.at(index).cube
               ? formats::WorldTextureShape::cube
               : formats::WorldTextureShape::two_dimensional},
          memory::Allocator<std::uint8_t>(domain));
      textures.push_back(renderer.upload_world_texture(pixels));
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
    }
  } catch (...) {
    release();
    throw;
  }
}
MissionLoading::ResidentDefinition::~ResidentDefinition() { release(); }
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
  loading_scene_ = {item, true, false};
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
  auto decoded =
      content::load_stage_definition(files_, catalogs_, stage, memory_);
  stage_name_ = stage;
  stage_ = std::move(decoded);
  if (renderer_ != nullptr) {
    stage_residency_.emplace(*renderer_, stage_.value(), memory_);
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
          sky_residency_.emplace(*renderer_, sky_.value(), memory_);
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
