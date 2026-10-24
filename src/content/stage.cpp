// Loading presentation uses explicit asset-boundary callbacks (design Core I.1
// #ri-explicit, implementation E.6 #re-raii). Performance GPU.1 retains
// uploads; callbacks pump only completed loading UI and never inspect partial
// owners. CPU asset loading contract in bw/content/stage.hpp. Native source
// anchors and selected compatibility subset are recorded in
// stage-content-decoding.md. Resolve immutable catalogs, build a temporary
// owned result, then return only after required mesh/texture associations
// validate. Exceptions unwind all partial acquisitions. This synchronous
// mechanism makes no worker/device-ready claim; caller owns scheduling and
// cancellation/publication. Costs O(catalog entries + records + decoded mesh
// indices + bytes read), with O(log acquired archives) cache lookup.
// Optimization: retain each texture file once per stage; encoded images remain
// borrowed by the native upload consumer. Both design and implementation
// consulted cpp-guidelines SL.con.3
// (#rsl-bounds) and R.2 (#rr-use-ptr), checking record and buffer references
// before spans; cpp-performance MEM.6 (guidelines/memory/
// MEM.6-inject-allocators-behind-one-interface.md) accounts nested owners.
#include "bw/content/stage.hpp"

#include <algorithm>
#include <map>
#include <set>

#include "../formats/reader.hpp"

namespace bw::content {
namespace {
using resources::Error;
using resources::ErrorCode;
enum class PublicationKind : std::uint8_t { user, texture };
struct InputRequest {
  std::string_view name, unresolved;
};
constexpr std::size_t kTextureIndexOffset = 36;
constexpr std::uint64_t kTextureNameBytes = 32;
constexpr std::size_t kMetadataOffsetField = 5;
constexpr std::size_t kMetadataSizeField = 6;
constexpr std::size_t kModelPrefixBytes = 6;
constexpr std::size_t kModelSuffixBytes = 5;
using Key = std::pair<std::size_t, std::size_t>;
using CacheValue = std::pair<const Key, std::size_t>;
using ArchiveCache =
    std::map<Key, std::size_t, std::less<>, memory::Allocator<CacheValue>>;
// Primary stage names carrying an en/ds token and two decimal digits use a
// separate behavior constructor. Other names create ordinary static objects;
// detail and background suffixes are not LOD exclusions.
bool ordinary_stage_model(std::string_view name) {
  constexpr std::size_t kBehaviorTokenBytes = 4;
  constexpr std::size_t kBehaviorPrefixBytes = 2;
  while (true) {
    const auto separator = name.find('_');
    if (separator == std::string_view::npos) {
      return true;
    }
    name.remove_prefix(separator + 1);
    if (name.size() < kBehaviorTokenBytes) {
      return true;
    }
    if ((name.starts_with("en") || name.starts_with("ds")) &&
        std::ranges::all_of(
            name.substr(kBehaviorPrefixBytes, kBehaviorPrefixBytes),
            [](char value) { return value >= '0' && value <= '9'; })) {
      return false;
    }
  }
}
std::optional<resources::Resolution> optional_resolution(
    const resources::CatalogSet& catalogs, std::string_view name) {
  try {
    return catalogs.resolve(name);
  } catch (const Error& error) {
    if (error.code != ErrorCode::not_found) {
      throw;
    }
    return std::nullopt;
  }
}
std::size_t publication(const resources::ResourceArchive& archive,
                        std::string_view name, PublicationKind kind) {
  std::optional<std::size_t> found;
  for (std::size_t i = 0; i < archive.records().size(); ++i) {
    const auto& record = archive.records().at(i);
    if (record.publication_name && *record.publication_name == name) {
      if (found) {
        throw Error(ErrorCode::ambiguous,
                    "Duplicate asset publication: " + std::string(name));
      }
      if ((kind == PublicationKind::user && record.type != "USER") ||
          (kind == PublicationKind::texture && record.type != "DDS " &&
           record.type != "DDSC")) {
        throw Error(ErrorCode::unsupported,
                    "Unsupported asset type: " + std::string(name));
      }
      found = i;
    }
  }
  if (!found) {
    throw Error(ErrorCode::not_found,
                "Missing asset publication: " + std::string(name));
  }
  return *found;
}
resources::ResourceArchive acquire(
    const platform::Files& files, const resources::CatalogSet& catalogs,
    resources::Resolution selected,
    const std::shared_ptr<memory::Domain>& domain) {
  const auto& mount = catalogs.catalogs().at(selected.catalog_index);
  const auto& resource = mount.catalog.resources().at(selected.resource_index);
  return open_archive(files, archive_path(files, mount, resource.raw.at(3)),
                      domain);
}
class Builder {
 public:
  Builder(const platform::Files& files, const resources::CatalogSet& catalogs,
          std::shared_ptr<memory::Domain> domain)
      : files_(&files),
        catalogs_(&catalogs),
        domain_(std::move(domain)),
        result_(domain_),
        cache_(std::less<>{}, memory::Allocator<CacheValue>(domain_)) {}
  StageModel& model(std::string_view name);
  void scene_settings(std::string_view stage);
  void texture_set(std::string_view name) {
    load_texture_set(name, result_.texture_table, true);
  }
  std::size_t input(InputRequest request);
  void auxiliaries(std::string_view name);
  void environment(std::string_view name, std::size_t slot) {
    if (optional_resolution(*catalogs_, name)) {
      result_.environment_textures.at(slot) = texture(name);
    }
  }
  StageDefinition finish() { return std::move(result_); }

 private:
  std::size_t archive(resources::Resolution selected);
  std::size_t texture(std::string_view publication_name);
  void load_texture_set(std::string_view name,
                        memory::Vector<std::size_t>& slots, bool required);
  const platform::Files* files_;
  const resources::CatalogSet* catalogs_;
  std::shared_ptr<memory::Domain> domain_;
  StageDefinition result_;
  ArchiveCache cache_;
};
std::size_t Builder::archive(resources::Resolution selected) {
  const auto& mount = catalogs_->catalogs().at(selected.catalog_index);
  const auto& resource = mount.catalog.resources().at(selected.resource_index);
  const Key key{selected.catalog_index, resource.raw.at(3)};
  if (const auto it = cache_.find(key); it != cache_.end()) {
    return it->second;
  }
  const auto index = result_.archives.size();
  result_.archives.push_back(acquire(*files_, *catalogs_, selected, domain_));
  cache_.emplace(key, index);
  return index;
}
// Resolve authored stage settings once, retaining the customization owner.
void Builder::scene_settings(std::string_view stage) {
  constexpr std::string_view kSettings = "customizer.default";
  const auto owner = archive(catalogs_->resolve(kSettings));
  const auto record =
      publication(result_.archives.at(owner), kSettings, PublicationKind::user);
  result_.customization = StageRecordLocation{owner, record};
  result_.environment = formats::StageEnvironment::parse(
      result_.archives.at(owner).payload(record), stage, domain_);
}
std::size_t Builder::texture(std::string_view publication_name) {
  const auto selected = catalogs_->resolve(publication_name);
  const auto archive_index = archive(selected);
  const auto record_index =
      publication(result_.archives.at(archive_index), publication_name,
                  PublicationKind::texture);
  const auto& source_record =
      result_.archives.at(archive_index).records().at(record_index);
  if (source_record.type != "DDS " && source_record.type != "DDSC") {
    throw Error(ErrorCode::unsupported, "Unsupported texture publication: " +
                                            std::string(publication_name));
  }
  for (std::size_t i = 0; i < result_.textures.size(); ++i) {
    const auto& texture = result_.textures.at(i);
    if (texture.archive == archive_index && texture.record == record_index) {
      return i;
    }
  }
  result_.textures.emplace_back(
      source_record.name, StageRecordLocation{archive_index, record_index},
      source_record.type == "DDSC", memory::Allocator<std::byte>(domain_));
  return result_.textures.size() - 1;
}
StageModel& Builder::model(std::string_view name) {
  const memory::Allocator<std::byte> allocator(domain_);
  const auto prefix =
      memory::String("model.", allocator) + memory::String(name, allocator);
  const auto mesh_name = prefix + ".mesh";
  auto definition = formats::ModelDefinition::parse(
      acquire(*files_, *catalogs_, catalogs_->resolve(mesh_name), domain_),
      domain_);
  result_.models.emplace_back(name, std::move(definition), allocator);
  auto& model = result_.models.back();
  const auto control_name =
      resources::normalize_name(prefix + ".controlpoint", allocator);
  if (const auto selected = optional_resolution(*catalogs_, control_name)) {
    const auto owner = archive(*selected);
    model.control_points = StageRecordLocation{
        owner, publication(result_.archives.at(owner), control_name,
                           PublicationKind::user)};
  }
  bool required = false;
  for (const auto& material : model.definition.materials()) {
    required |= !material.textures.empty();
  }
  load_texture_set(name, model.textures, required);
  for (const auto& material : model.definition.materials()) {
    for (const auto& reference : material.textures) {
      if (reference.texture >= model.textures.size()) {
        resources::detail::malformed(
            "Material texture reference escapes runtime table");
      }
    }
  }
  return model;
}
void Builder::load_texture_set(std::string_view name,
                               memory::Vector<std::size_t>& slots,
                               bool required) {
  const memory::Allocator<std::byte> allocator(domain_);
  const auto prefix =
      memory::String("model.", allocator) + memory::String(name, allocator);
  const auto set = optional_resolution(*catalogs_, prefix + ".texture.default");
  if (!set) {
    if (required) {
      throw Error(ErrorCode::not_found, "Required texture set is absent");
    }
    return;
  }
  const auto& texture_set = catalogs_->catalogs()
                                .at(set->catalog_index)
                                .catalog.resources()
                                .at(set->resource_index);
  const auto texture_archive = archive(*set);
  const auto& source = result_.archives.at(texture_archive);
  std::optional<std::size_t> table_record;
  for (std::size_t i = 0; i < source.records().size(); ++i) {
    if (source.records().at(i).name == "table._tm") {
      if (table_record) {
        throw Error(ErrorCode::ambiguous, "Duplicate runtime texture table");
      }
      table_record = i;
    }
  }
  if (!table_record) {
    throw Error(ErrorCode::not_found, "Missing runtime texture table");
  }
  // Runtime texture indices belong to table._tm, not the informational TLST.
  // Copy small names before archive-vector growth can move the source owner.
  memory::Vector<memory::String> names(allocator);
  const resources::detail::Reader table{source.payload(*table_record)};
  const auto count = table.u32(0);
  constexpr std::uint64_t kEntryBytes = 40;
  if (table.b.size() != 4 + kEntryBytes * count) {
    resources::detail::malformed("Invalid runtime texture table size");
  }
  for (std::uint32_t i = 0; i < count; ++i) {
    const auto offset = 4 + kEntryBytes * i;
    if (table.u32(offset + kTextureIndexOffset) != i) {
      resources::detail::unsupported(
          "Non-sequential runtime texture table index");
    }
    names.emplace_back(table.text_view({offset + 4, kTextureNameBytes}, 0),
                       allocator);
  }
  for (const auto& raw_name : names) {
    const auto normalized = resources::normalize_name(raw_name, allocator);
    const auto suffix = memory::String(".dds.", allocator) + normalized;
    std::optional<std::string_view> selected;
    for (const auto& dependency : texture_set.dependencies) {
      if (!dependency.name.ends_with(suffix)) {
        continue;
      }
      if (selected) {
        throw Error(
            ErrorCode::ambiguous,
            "Ambiguous model texture dependency: " + std::string(raw_name));
      }
      selected = dependency.name;
    }
    if (!selected) {
      throw Error(ErrorCode::not_found,
                  "Model texture missing from selected texture set: " +
                      std::string(raw_name));
    }
    slots.push_back(texture(*selected));
  }
}
// Original optional requests publish load-time definitions. CALL dispatch is
// explicit: retaining an unrecognized encoded input cannot make it ready.
std::size_t Builder::input(InputRequest request) {
  const auto [requested_name, unresolved] = request;
  const memory::Allocator<std::byte> allocator(domain_);
  const auto normalized = resources::normalize_name(requested_name, allocator);
  const std::string_view name = normalized;
  for (std::size_t i = 0; i < result_.inputs.size(); ++i) {
    if (result_.inputs.at(i).name == name) {
      return i;
    }
  }
  const auto index = result_.inputs.size();
  result_.inputs.emplace_back(name, allocator, unresolved);
  const auto selected = optional_resolution(*catalogs_, name);
  if (!selected) {
    result_.inputs.at(index).definition_ready = true;
    result_.inputs.at(index).unresolved.clear();
    return index;
  }
  const auto archive_index = archive(*selected);
  result_.inputs.at(index).archive = archive_index;
  const auto& source = result_.archives.at(archive_index);
  std::optional<std::size_t> record_index;
  for (std::size_t i = 0; i < source.records().size(); ++i) {
    const auto& record = source.records().at(i);
    if (record.publication_name && *record.publication_name == name) {
      if (record_index) {
        throw Error(ErrorCode::ambiguous, "Duplicate auxiliary publication");
      }
      record_index = i;
    }
  }
  if (!record_index) {
    throw Error(ErrorCode::not_found,
                "Missing auxiliary publication: " + std::string(name));
  }
  const auto& record = source.records().at(*record_index);
  auto& destination = result_.inputs.at(index);
  if (record.type == "CALL" &&
      (record.name == "vfx.hit.create2" || record.name == "vfx.phit.create")) {
    std::optional<std::string_view> target;
    for (const auto& argument : record.arguments) {
      if (argument.name &&
          (*argument.name == "cehit" || *argument.name == "phit") &&
          argument.string_value) {
        target = *argument.string_value;
      }
    }
    if (!target) {
      throw Error(ErrorCode::malformed, "Collision CALL has no cehit argument");
    }
    std::optional<std::size_t> payload;
    for (std::size_t i = 0; i < source.records().size(); ++i) {
      if (source.records().at(i).type == "USER" &&
          source.records().at(i).name == *target) {
        if (payload) {
          throw Error(ErrorCode::ambiguous, "Duplicate collision USER");
        }
        payload = i;
      }
    }
    if (!payload) {
      throw Error(ErrorCode::not_found,
                  "Collision CALL references missing USER");
    }
    if (record.name == "vfx.hit.create2") {
      destination.collision = formats::CollisionDefinition::parse(
          source.payload(*payload), domain_);
    } else {
      destination.physics_collision =
          formats::PhysicsCollisionDefinition::parse(source.payload(*payload),
                                                     domain_);
    }
  } else if (record.type == "CALL" &&
             record.name == "vfx.matanim.list.create") {
    // Copy only tiny argument names before recursive acquisition moves vectors.
    memory::Vector<std::pair<bool, memory::String>> bindings(allocator);
    for (const auto& argument : record.arguments) {
      if (!argument.name || !argument.string_value) {
        continue;
      }
      if (*argument.name == "matanim.a" || *argument.name == "matanim.i") {
        bindings.emplace_back(
            *argument.name == "matanim.a",
            memory::String(*argument.string_value, allocator));
      }
    }
    for (const auto& [animation, target] : bindings) {
      const auto child =
          input({target, "Material animation dependency constructor pending"});
      const auto& loaded = result_.inputs.at(child);
      if (!loaded.definition_ready || !loaded.archive ||
          (animation ? !loaded.material_animation.has_value()
                     : !loaded.material_labels.has_value())) {
        throw Error(ErrorCode::unsupported,
                    "Material animation list dependency did not construct");
      }
      auto& list = result_.inputs.at(index);
      (animation ? list.animations : list.labels).push_back(child);
    }
    if (result_.inputs.at(index).animations.size() !=
        result_.inputs.at(index).labels.size()) {
      throw Error(ErrorCode::malformed,
                  "Material animation list bindings disagree");
    }
  } else if (record.type == "USER") {
    const auto bytes = source.payload(*record_index);
    const resources::detail::Reader reader{bytes};
    if (reader.tag_view(0) == "AREA") {
      destination.area = formats::AreaDefinition::parse(bytes, domain_);
    } else if (reader.tag_view(0) == "STBL") {
      destination.table = formats::SectionTable::parse(bytes, domain_);
    } else if (reader.tag_view(0) == "FLAN") {
      destination.scalar_animation =
          formats::ScalarAnimationDefinition::parse(bytes);
    } else if (reader.tag_view(0) == "MTAB") {
      destination.material_animation =
          formats::MaterialAnimationDefinition::parse(bytes, domain_);
    } else if (reader.tag_view(0) == "CILB") {
      destination.material_labels =
          formats::MaterialAnimationLabels::parse(bytes);
    } else if (name.ends_with(".effect.emt") && reader.tag_view(0) == "EVMT") {
      // Retain the immutable effect table; effect emission is a later consumer.
    } else {
      return index;
    }
  } else {
    return index;
  }
  result_.inputs.at(index).definition_ready = true;
  result_.inputs.at(index).unresolved.clear();
  return index;
}
void Builder::auxiliaries(std::string_view name) {
  const memory::Allocator<std::byte> allocator(domain_);
  const auto prefix =
      memory::String("model.", allocator) + memory::String(name, allocator);
  for (const auto suffix :
       {".hit", ".chit", ".ehit", ".phit", ".matanim.list.nocompress"}) {
    input({prefix + suffix, "Original auxiliary load constructor unresolved"});
  }
}
bool numbered_variant(std::string_view name) {
  unsigned run = 0;
  for (const char ch : name) {
    run = ch >= '0' && ch <= '9' ? run + 1 : 0;
    if (run >= 4) {
      return true;
    }
  }
  return false;
}
}  // namespace

std::span<const std::uint8_t> StageDefinition::customization_bytes() const {
  if (!customization) {
    throw Error(ErrorCode::malformed, "Stage customization is absent");
  }
  return archives.at(customization->archive).payload(customization->record);
}
std::span<const std::uint8_t> StageDefinition::texture_bytes(
    std::size_t index) const {
  const auto& texture = textures.at(index);
  return archives.at(texture.archive).payload(texture.record);
}
std::span<const std::uint8_t> StageDefinition::texture_metadata(
    std::size_t index) const {
  const auto& texture = textures.at(index);
  const auto& archive = archives.at(texture.archive);
  const auto& record = archive.records().at(texture.record);
  const auto gap = archive.sections().at(3);
  const resources::detail::Reader reader{archive.bytes()};
  reader.within(gap, record.raw.at(kMetadataOffsetField),
                record.raw.at(kMetadataSizeField));
  return archive.bytes().subspan(
      static_cast<std::size_t>(gap.offset +
                               record.raw.at(kMetadataOffsetField)),
      record.raw.at(kMetadataSizeField));
}
UserDefinition load_user_definition(
    const platform::Files& files, const resources::CatalogSet& catalogs,
    std::string_view resource, const std::shared_ptr<memory::Domain>& domain) {
  const auto normalized =
      resources::normalize_name(resource, memory::Allocator<char>(domain));
  auto archive = acquire(files, catalogs, catalogs.resolve(normalized), domain);
  const auto record = publication(archive, normalized, PublicationKind::user);
  return {std::move(archive), record};
}
StageDefinition load_model_definition(
    const platform::Files& files, const resources::CatalogSet& catalogs,
    std::string_view model, const std::shared_ptr<memory::Domain>& domain) {
  Builder builder(files, catalogs, domain);
  builder.model(model);
  builder.auxiliaries(model);
  return builder.finish();
}
StageDefinition load_texture_definition(
    const platform::Files& files, const resources::CatalogSet& catalogs,
    std::string_view model, const std::shared_ptr<memory::Domain>& domain) {
  Builder builder(files, catalogs, domain);
  builder.texture_set(model);
  return builder.finish();
}
StageDefinition load_stage_definition(
    const platform::Files& files, const resources::CatalogSet& catalogs,
    std::string_view stage, const std::shared_ptr<memory::Domain>& domain,
    const LoadProgress& progress) {
  const memory::Allocator<std::byte> allocator(domain);
  const auto normalized = resources::normalize_name(stage, allocator);
  if (normalized.size() < 4 || !normalized.starts_with("st")) {
    throw Error(ErrorCode::unsupported, "Unsupported stage resource prefix");
  }
  Builder builder(files, catalogs, domain);
  builder.scene_settings(normalized);
  std::set<memory::String, std::less<>, memory::Allocator<memory::String>>
      names(std::less<>{}, allocator);
  const auto prefix = memory::String("model.", allocator) + normalized;
  // Collect unique models for the stage. Skip placeholder models and numbered
  // variants unless the requested prefix explicitly selects them.
  for (const auto& mount : catalogs.catalogs()) {
    for (const auto& resource : mount.catalog.resources()) {
      const std::string_view full = resource.full_name;
      if (!full.starts_with(prefix) || !full.ends_with(".mesh")) {
        continue;
      }
      const auto name =
          full.substr(kModelPrefixBytes,
                      full.size() - kModelPrefixBytes - kModelSuffixBytes);
      if (normalized.find("dummy") == std::string_view::npos &&
          name.find("dummy") != std::string_view::npos) {
        continue;
      }
      if (!numbered_variant(normalized) && numbered_variant(name)) {
        continue;
      }
      names.emplace(name, allocator);
    }
  }
  if (names.empty()) {
    throw Error(ErrorCode::not_found, "Stage has no model definitions");
  }
  constexpr std::size_t kNativePrimaryModelLimit = 80;
  if (names.size() > kNativePrimaryModelLimit) {
    throw Error(ErrorCode::unsupported,
                "Stage exceeds native primary model capacity");
  }
  for (const auto& name : names) {
    builder.model(name).static_world_object = ordinary_stage_model(name);
    builder.auxiliaries(name);
    if (progress) progress();
  }
  // Stage construction also loads the matching stage-gimmick namespace;
  // these definitions do not instantiate the later mission actors.
  const auto gimmick_prefix =
      memory::String("model.gm", allocator) + normalized.substr(2);
  std::set<memory::String, std::less<>, memory::Allocator<memory::String>>
      gimmicks(std::less<>{}, allocator);
  for (const auto& mount : catalogs.catalogs()) {
    for (const auto& resource : mount.catalog.resources()) {
      const std::string_view full = resource.full_name;
      if (!full.starts_with(gimmick_prefix) || !full.ends_with(".mesh")) {
        continue;
      }
      gimmicks.emplace(
          full.substr(kModelPrefixBytes,
                      full.size() - kModelPrefixBytes - kModelSuffixBytes),
          allocator);
    }
  }
  constexpr std::size_t kNativeGimmickLimit = 64;
  if (gimmicks.size() > kNativeGimmickLimit) {
    throw Error(ErrorCode::unsupported,
                "Stage exceeds native gimmick capacity");
  }
  for (const auto& name : gimmicks) {
    builder.model(name);
    builder.auxiliaries(name);
    if (progress) progress();
    for (const auto suffix :
         {".motion.list.compress", ".motion.list.nocompress",
          ".motionparam.list", ".motionparam.defaultmotion"}) {
      builder.input({memory::String("model.", allocator) + name + suffix,
                     "Gimmick animation constructor unresolved"});
    }
  }
  constexpr std::size_t kDecimalBase = 10;
  for (std::size_t slot = 0; slot < kStageEnvironmentTextureSlots; ++slot) {
    auto name =
        memory::String("texture.", allocator) + normalized + "_cubemap_";
    name.push_back(static_cast<char>('0' + slot / kDecimalBase));
    name.push_back(static_cast<char>('0' + slot % kDecimalBase));
    builder.environment(name, slot);
  }
  for (const auto suffix : {".areadata.bin", ".areadata.settbl",
                            ".areadata.filani", ".effect.emt"}) {
    builder.input({memory::String("areadata.", allocator) + normalized + suffix,
                   "Area/environment consumer pending"});
  }
  return builder.finish();
}
}  // namespace bw::content
