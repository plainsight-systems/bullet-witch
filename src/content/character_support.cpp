// Compose original motion-list and USER support publications. Construction
// retains each archive once and resolves ordered references into owned
// metadata; no sampling, model rendering or actor state is implied. Both-phase
// I.11/MEM.6 contracts and source evidence accompany character_support.hpp.
#include "bw/content/character_support.hpp"

#include <algorithm>
#include <map>

namespace bw::content {
namespace {
using resources::Error;
using resources::ErrorCode;
using Key = std::pair<std::size_t, std::size_t>;
using CacheEntry = std::pair<const Key, std::size_t>;
class Builder {
 public:
  Builder(const platform::Files& files, const resources::CatalogSet& catalogs,
          const std::shared_ptr<memory::Domain>& domain)
      : files_(&files),
        catalogs_(&catalogs),
        domain_(domain),
        result_(domain),
        cache_(std::less<>{}, memory::Allocator<CacheEntry>(domain)) {}
  bool optional(std::string_view name);
  CharacterSupport finish() { return std::move(result_); }

 private:
  CharacterPublication publication(std::string_view name);
  std::size_t user(std::string_view name);
  std::size_t sequence(std::string_view name, bool shape);
  const platform::Files* files_;
  const resources::CatalogSet* catalogs_;
  std::shared_ptr<memory::Domain> domain_;
  CharacterSupport result_;
  std::map<Key, std::size_t, std::less<>, memory::Allocator<CacheEntry>> cache_;
};
CharacterPublication Builder::publication(std::string_view name) {
  const auto normalized =
      resources::normalize_name(name, memory::Allocator<char>(domain_));
  const auto selected = catalogs_->resolve(normalized);
  const auto& mount = catalogs_->catalogs().at(selected.catalog_index);
  const auto file =
      mount.catalog.resources().at(selected.resource_index).raw.at(3);
  const Key key{selected.catalog_index, file};
  auto found = cache_.find(key);
  if (found == cache_.end()) {
    const auto index = result_.archives.size();
    result_.archives.push_back(
        open_archive(*files_, archive_path(*files_, mount, file), domain_));
    found = cache_.emplace(key, index).first;
  }
  const auto& archive = result_.archives.at(found->second);
  std::optional<std::size_t> match;
  for (std::size_t index = 0; index < archive.records().size(); ++index) {
    const auto& record = archive.records().at(index);
    if (record.publication_name && *record.publication_name == normalized) {
      if (match) {
        throw Error(ErrorCode::ambiguous, "Duplicate character publication");
      }
      match = index;
    }
  }
  if (!match) {
    throw Error(ErrorCode::not_found,
                "Missing character publication: " + std::string(name));
  }
  return {normalized,
          {found->second, *match},
          memory::Allocator<std::byte>(domain_)};
}
std::size_t Builder::user(std::string_view name) {
  auto location = publication(name);
  if (result_.archives.at(location.archive)
          .records()
          .at(location.record)
          .type != "USER") {
    throw Error(
        ErrorCode::unsupported,
        "Character support requires USER publication: " + std::string(name));
  }
  for (std::size_t index = 0; index < result_.publications.size(); ++index) {
    const auto& prior = result_.publications.at(index);
    if (prior.archive == location.archive && prior.record == location.record) {
      return index;
    }
  }
  result_.publications.push_back(std::move(location));
  return result_.publications.size() - 1;
}
std::size_t Builder::sequence(std::string_view name, bool shape) {
  auto location = publication(name);
  const auto& archive = result_.archives.at(location.archive);
  const auto& record = archive.records().at(location.record);
  if (record.type != "CALL" ||
      record.name != (shape ? "vfx.shape.create" : "vfx.motion.create")) {
    throw Error(ErrorCode::unsupported,
                "Unsupported character motion constructor");
  }
  const auto argument =
      std::ranges::find_if(record.arguments, [shape](const auto& arg) {
        return arg.name && *arg.name == (shape ? "shape.a" : "motion.a") &&
               arg.string_value;
      });
  if (argument == record.arguments.end() || !argument->string_value) {
    throw Error(ErrorCode::malformed, "Motion constructor has no source");
  }
  const auto& source_name = argument->string_value;
  if (!source_name) {
    throw Error(ErrorCode::malformed, "Missing motion source name");
  }
  std::optional<std::size_t> source;
  for (std::size_t index = 0; index < archive.records().size(); ++index) {
    if (archive.records().at(index).name == *source_name) {
      if (source) {
        throw Error(ErrorCode::ambiguous, "Duplicate local motion source");
      }
      source = index;
    }
  }
  if (!source || archive.records().at(*source).type != "USER") {
    throw Error(ErrorCode::malformed, "Missing local motion payload");
  }
  if (shape) {
    auto definition = formats::ShapeAnimationDefinition::parse(
        archive.payload(*source), domain_);
    result_.shapes.push_back({std::move(location), std::move(definition)});
    return result_.shapes.size() - 1;
  }
  auto definition =
      formats::MotionDefinition::parse(archive.payload(*source), domain_);
  result_.motions.push_back({std::move(location), std::move(definition)});
  return result_.motions.size() - 1;
}
bool Builder::optional(std::string_view name) {
  try {
    (void)catalogs_->resolve(name);
  } catch (const Error& error) {
    if (error.code == ErrorCode::not_found) return false;
    throw;
  }
  const auto location = publication(name);
  // Copy argument metadata: acquiring dependencies may move the archive array.
  const auto record =
      result_.archives.at(location.archive).records().at(location.record);
  if (record.type == "USER") {
    (void)user(name);
    return true;
  }
  const bool motions = record.name == "vfx.motion.list.create";
  const bool shapes = record.name == "vfx.shape.list.create";
  const bool parameters = record.name == "vfx.sqparam.list";
  if (record.type != "CALL" || (!motions && !shapes && !parameters)) {
    throw Error(
        ErrorCode::unsupported,
        "Unsupported character support constructor: " + std::string(name));
  }
  for (const auto& argument : record.arguments) {
    if (!argument.name || !argument.string_value) {
      throw Error(ErrorCode::malformed, "Invalid character list argument");
    }
    if (motions && *argument.name == "bonelist") {
      result_.bone_names = user(*argument.string_value);
    } else if (motions && *argument.name == "motion.l") {
      result_.motion_order.push_back(sequence(*argument.string_value, false));
    } else if (shapes && *argument.name == "shape.l") {
      result_.shape_order.push_back(sequence(*argument.string_value, true));
    } else if (parameters && *argument.name == "sqparam") {
      result_.parameter_order.push_back(user(*argument.string_value));
    } else {
      throw Error(ErrorCode::unsupported, "Unmapped character list argument");
    }
  }
  return true;
}
}  // namespace
CharacterSupport load_character_support(
    const platform::Files& files, const resources::CatalogSet& catalogs,
    std::string_view base_name, const std::shared_ptr<memory::Domain>& domain) {
  const memory::Allocator<char> allocator(domain);
  auto alias = resources::normalize_name(base_name, allocator);
  if (alias.starts_with("ea")) {
    alias = "ea0";
  } else if (alias.starts_with("pw") || alias == "m09") {
    alias = "pw0";
  }
  const auto prefix = memory::String("model.", allocator) + alias;
  Builder builder(files, catalogs, domain);
  for (const auto type :
       {std::string_view("motion"), std::string_view("shape")}) {
    for (const auto encoding :
         {std::string_view("compress"), std::string_view("nocompress")}) {
      const auto name = prefix + "." + memory::String(type, allocator) +
                        ".list." + memory::String(encoding, allocator);
      if (builder.optional(name)) break;
    }
  }
  (void)builder.optional(prefix + ".motionparam.list");
  return builder.finish();
}
}  // namespace bw::content
