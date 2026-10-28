// Original ai.add publications reference USER wrappers through their named amb
// argument. Resolve that binding, not a presumed neighboring record. Archives
// precede views in member lifetime and reserve before publication. Both-phase
// Core I.11 (#ri-raw) preserves const borrowing; performance CDSA.32
// (guidelines/cpu-dsa/CDSA.32-transform-static-data-into-the-consumer-layout.md)
// prepares each requested tree once for every actor using it.
#include "mission_behavior_assets.hpp"

#include <algorithm>
#include <stdexcept>

namespace bw::game {
namespace {
std::size_t wrapper(const resources::ResourceArchive& archive,
                    std::string_view publication) {
  const resources::ResourceArchiveRecord* binding = nullptr;
  for (const auto& record : archive.records()) {
    if (record.publication_name && *record.publication_name == publication) {
      if (binding) throw std::invalid_argument("Ambiguous AI publication");
      binding = &record;
    }
  }
  if (!binding || binding->type != "CALL" || binding->name != "ai.add") {
    throw std::invalid_argument("Missing original AI constructor binding");
  }
  std::optional<std::string_view> name;
  for (const auto& argument : binding->arguments) {
    if (argument.name && *argument.name == "amb") {
      if (name || !argument.string_value) {
        throw std::invalid_argument("Invalid AI wrapper argument");
      }
      name = *argument.string_value;
    }
  }
  if (!name) throw std::invalid_argument("AI constructor has no wrapper");
  std::optional<std::size_t> result;
  for (std::size_t i = 0; i < archive.records().size(); ++i) {
    const auto& record = archive.records()[i];
    if (record.name != *name || record.type != "USER") continue;
    if (result) throw std::invalid_argument("Ambiguous AI wrapper");
    result = i;
  }
  if (!result) throw std::invalid_argument("Missing AI wrapper");
  return *result;
}
}  // namespace
MissionBehaviorAssets::MissionBehaviorAssets(
    const platform::Files& files, const resources::CatalogSet& catalogs,
    const std::shared_ptr<memory::Domain>& domain,
    std::span<const std::string_view> names)
    : archives_(memory::Allocator<Archive>(domain)),
      entries_(memory::Allocator<Entry>(domain)) {
  archives_.reserve(names.size());
  entries_.reserve(names.size());
  for (const auto name : names) {
    auto publication = memory::String("ai.", memory::Allocator<char>(domain));
    publication.append(name);
    const auto selected = catalogs.resolve(publication);
    const auto& mount = catalogs.catalogs().at(selected.catalog_index);
    const auto file =
        mount.catalog.resources().at(selected.resource_index).raw.at(3);
    auto owner = std::ranges::find_if(archives_, [&](const auto& candidate) {
      return candidate.catalog == selected.catalog_index &&
             candidate.file == file;
    });
    if (owner == archives_.end()) {
      archives_.push_back(
          {selected.catalog_index, file,
           content::open_archive(
               files, content::archive_path(files, mount, file), domain)});
      owner = std::prev(archives_.end());
    }
    const auto record = wrapper(owner->data, publication);
    const auto& source_name = owner->data.records()[record].name;
    if (!source_name.ends_with(".amb")) {
      throw std::invalid_argument("Original AI wrapper has an invalid suffix");
    }
    const auto canonical =
        std::string_view(source_name).substr(0, source_name.size() - 4);
    if (canonical != name ||
        std::ranges::any_of(entries_, [&](const auto& entry) {
          return entry.name == canonical;
        })) {
      throw std::invalid_argument(
          "AI closure has an ambiguous resource identity");
    }
    entries_.push_back({canonical, formats::BehaviorTreeDefinition::parse(
                                       owner->data.payload(record), domain)});
  }
}
const formats::BehaviorTreeDefinition& MissionBehaviorAssets::definition(
    std::string_view name) const {
  if (name.starts_with("ai.")) name.remove_prefix(3);
  if (name.ends_with(".amb")) name.remove_suffix(4);
  const auto found = std::ranges::find_if(
      entries_, [&](const auto& entry) { return entry.name == name; });
  if (found == entries_.end()) {
    throw std::out_of_range("Behavior is outside the admitted mission closure");
  }
  return found->tree;
}
}  // namespace bw::game
