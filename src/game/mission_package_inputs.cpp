// Construct original mission package consumers: text registration, cover-point
// definitions and background model preloads. Package spans borrow
// MissionLoading through cancellation. Dependency owners retain injected
// domains and device handles; no actor simulation is started. Original
// source/schema and costs are recorded in mission-load-consumers.md.
// Design/implementation E.6 (#re-raii) handles partial owner cleanup; SL.con.3
// (#rsl-bounds) checks every encoded offset. MEM.6
// (guidelines/memory/MEM.6-inject-allocators-behind-one-interface.md)
// propagates the mission domain. These rules were read in both phases.
// Background placement uses both-phase C.32 (#rc-dtor-ptr) for borrowed
// package names and MEM.9 (guidelines/memory/
// MEM.9-embedded-allocate-at-init.md) for load-only metadata.
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

#include "../formats/reader.hpp"
#include "bw/formats/section_table.hpp"
#include "mission_loading.hpp"
namespace bw::game {
std::span<const std::uint8_t> MissionLoading::optional_member(
    std::string_view name) const {
  if (!package_) throw std::logic_error("Mission package is not retained");
  for (const auto& entry : package_->members()) {
    if (entry.name == name) return package_->member(name);
  }
  return {};
}
void MissionLoading::load_cover_points() {
  const auto bytes = optional_member(request_name_ + ".hide");
  memory::Vector<CoverPointDefinition> result{
      memory::Allocator<CoverPointDefinition>(memory_)};
  if (!bytes.empty()) {
    const auto table = formats::SectionTable::parse(bytes, memory_);
    const auto section = table.find("kakure");
    if (!section) throw std::runtime_error("Cover-point section is missing");
    const auto& layout = table.sections().at(*section);
    constexpr std::size_t kMaximumPoints = 256, kCoordinates = 6;
    if (layout.rows > kMaximumPoints || layout.columns < kCoordinates) {
      throw std::runtime_error("Cover-point dimensions exceed native manager");
    }
    result.reserve(layout.rows);
    for (std::size_t row = 0; row < layout.rows; ++row) {
      CoverPointDefinition point;
      for (std::size_t column = 0; column < kCoordinates; ++column) {
        point.values.at(column) =
            table.number({*section, row, column}).value_or(0.0F);
      }
      result.push_back(point);
    }
  }
  // Rebuild the original manager's active prefix; no AI behavior is implied.
  cover_points_ = std::move(result);
}
void MissionLoading::load_text() {
  const auto member = request_name_ + ".bin";
  auto bytes = optional_member(member);
  if (bytes.empty()) return;
  // Steam substitutes a retained loose table when present. Retain that owner
  // directly; never patch native pointers into immutable package bytes.
  resources::ArchiveBytes override{memory::Allocator<std::uint8_t>(memory_)};
  if (!options_.game_directory.empty()) {
    if (request_name_.find_first_of("/\\") != memory::String::npos ||
        request_name_ == "..") {
      throw std::invalid_argument("Mission text name must be a filename");
    }
    const auto path =
        options_.game_directory / "data/text" / std::string(member);
    if (std::filesystem::exists(path)) {
      override = files_.read(path, memory_);
      bytes = override;
    }
  }
  auto table = formats::TextTable::parse(bytes);
  text_override_ = std::move(override);
  text_table_ = table;
}

void MissionLoading::preload_model(std::string_view name) {
  // The native loader checks up to four authored breakage variants. A missing
  // primary collision publication skips this object; missing later variants
  // do not prevent subsequent variants from being considered.
  constexpr std::array<std::string_view, 4> suffixes{"", "_w", "_w1", "_w2"};
  for (const auto suffix : suffixes) {
    auto model = memory::String(name, memory::Allocator<char>(memory_));
    model += suffix;
    try {
      (void)catalogs_.resolve(
          memory::String("model.", memory::Allocator<char>(memory_)) + model +
          ".chit");
    } catch (const resources::Error& error) {
      if (error.code != resources::ErrorCode::not_found) throw;
      if (suffix.empty()) break;
      continue;
    }
    if (std::ranges::find(dependency_names_, model) !=
        dependency_names_.end()) {
      continue;
    }
    auto definition =
        content::load_model_definition(files_, catalogs_, model, memory_);
    if (const auto pending = std::ranges::find_if(
            definition.inputs,
            [](const auto& input) { return !input.definition_ready; });
        pending != definition.inputs.end()) {
      throw resources::Error(resources::ErrorCode::unsupported,
                             "Model dependency constructor pending: " +
                                 std::string(pending->name));
    }
    dependency_definitions_.push_back(std::move(definition));
    if (renderer_ != nullptr) {
      dependency_residencies_.emplace_back(
          *renderer_, dependency_definitions_.back(), memory_,
          options_.progress, options_.texture_mips);
    }
    dependency_names_.push_back(std::move(model));
  }
}
memory::Vector<ResidentModelView> MissionLoading::resident_dependencies()
    const {
  memory::Vector<ResidentModelView> result{
      memory::Allocator<ResidentModelView>(memory_)};
  if (dependency_residencies_.size() != dependency_definitions_.size()) {
    throw std::logic_error(
        "Dependency models require complete device residency");
  }
  auto resident = dependency_residencies_.begin();
  for (const auto& definition : dependency_definitions_) {
    if (resident->models.size() != definition.models.size()) {
      throw std::logic_error("Dependency model/device associations differ");
    }
    for (std::size_t index = 0; index < definition.models.size(); ++index) {
      const auto& model = definition.models.at(index);
      std::span<const std::int32_t> body_joints;
      const formats::PhysicsCollisionDefinition* physics_collision = nullptr;
      for (const auto& input : definition.inputs) {
        constexpr std::string_view kModelPrefix = "model.";
        std::string_view name = input.name;
        if (!input.physics_collision || !name.starts_with(kModelPrefix)) {
          continue;
        }
        name.remove_prefix(kModelPrefix.size());
        if (name.starts_with(model.name) &&
            name.substr(model.name.size()) == ".phit") {
          body_joints = input.physics_collision->body_joints();
          physics_collision = &*input.physics_collision;
          break;
        }
      }
      // Control-point bytes share the retained dependency archive lifetime.
      // The placed receiver prepares only its consumed control-frame origin.
      std::span<const std::uint8_t> control_points;
      if (model.control_points) {
        const auto& location = *model.control_points;
        control_points =
            definition.archives.at(location.archive).payload(location.record);
      }
      result.push_back({model.name, model.definition,
                        resident->models.at(index), body_joints,
                        physics_collision, control_points});
    }
    ++resident;
  }
  return result;
}
void MissionLoading::load_background() {
  const auto bytes = optional_member(request_name_ + ".bgb");
  if (bytes.empty()) return;
  auto definition =
      formats::BackgroundPlacementDefinition::parse(bytes, memory_);
  for (const auto name : definition.names()) {
    if (!name.starts_with("musi")) preload_model(name);
  }
  // Publish placement metadata only after all requested model dependencies are
  // constructed. Names borrow the retained package until cancellation.
  background_ = std::move(definition);
}
void MissionLoading::load_sound() {
  if (sound_bank_) {
    throw std::logic_error("Mission sound-bank slot is occupied");
  }
  const auto logical = resources::normalize_name(
      memory::String("audio.soundbank.", memory::Allocator<char>(memory_)) +
          request_name_,
      memory::Allocator<char>(memory_));
  const auto selected = catalogs_.resolve(logical);
  const auto& mount = catalogs_.catalogs().at(selected.catalog_index);
  const auto& resource = mount.catalog.resources().at(selected.resource_index);
  auto archive = content::open_archive(
      files_, content::archive_path(files_, mount, resource.raw.at(3)),
      memory_);
  std::optional<std::string_view> sound_name, wave_name;
  std::optional<std::uint32_t> slot;
  for (const auto& record : archive.records()) {
    if (!record.publication_name || *record.publication_name != logical) {
      continue;
    }
    if (record.type != "CALL" ||
        record.name != "audio.soundbank.create_memory") {
      throw resources::Error(
          resources::ErrorCode::unsupported,
          "Selected sound bank requires an unsupported constructor");
    }
    for (const auto& argument : record.arguments) {
      if (argument.name == "bank" && argument.raw.at(1) == 0) {
        slot = argument.raw.at(2);
      }
      if (argument.name == "csb" && argument.string_value) {
        sound_name = *argument.string_value;
      }
      if (argument.name == "xwb" && argument.string_value) {
        wave_name = *argument.string_value;
      }
    }
  }
  if (!slot || !sound_name || !wave_name) {
    throw std::runtime_error("Sound bank CALL lacks required arguments");
  }
  std::optional<std::size_t> sound, wave;
  for (std::size_t index = 0; index < archive.records().size(); ++index) {
    const auto& record = archive.records().at(index);
    if (record.type != "USER") continue;
    if (record.name == *sound_name) {
      if (sound) throw std::runtime_error("Duplicate sound bank payload");
      sound = index;
    }
    if (record.name == *wave_name) {
      if (wave) throw std::runtime_error("Duplicate wave bank payload");
      wave = index;
    }
  }
  if (!sound || !wave) {
    throw std::runtime_error("Sound bank dependency is missing");
  }
  auto bank = formats::AudioBankDefinition::parse(
      {archive.payload(*sound), archive.payload(*wave)}, memory_);
  // Register only after both complete bank definitions validate. Voices and
  // cue playback are separate consumers, matching the original load callback.
  sound_archive_ = std::move(archive);
  sound_bank_ = std::move(bank);
  sound_slot_ = slot;
}

void MissionLoading::preload_character(std::string_view name) {
  auto model = memory::String(name, memory::Allocator<char>(memory_));
  if (name.starts_with("ea")) {
    constexpr std::size_t kStageTens = 2, kStageOnes = 3;
    if (stage_name_.size() <= kStageOnes || stage_name_[kStageTens] < '0' ||
        stage_name_[kStageTens] > '9' || stage_name_[kStageOnes] < '0' ||
        stage_name_[kStageOnes] > '9') {
      throw std::runtime_error(
          "Character model requires a valid stage identifier");
    }
    constexpr unsigned kDecimalBase = 10;
    const auto stage =
        static_cast<unsigned>(stage_name_[kStageTens] - '0') * kDecimalBase +
        static_cast<unsigned>(stage_name_[kStageOnes] - '0');
    model += "_" + std::to_string(stage);
  }
  if (std::ranges::find(dependency_names_, model) == dependency_names_.end()) {
    auto definition =
        content::load_model_definition(files_, catalogs_, model, memory_);
    for (const auto& input : definition.inputs) {
      if (!input.definition_ready) {
        throw resources::Error(
            resources::ErrorCode::unsupported,
            "Character model constructor pending: " + std::string(input.name));
      }
    }
    dependency_definitions_.push_back(std::move(definition));
    if (renderer_ != nullptr) {
      dependency_residencies_.emplace_back(
          *renderer_, dependency_definitions_.back(), memory_,
          options_.progress, options_.texture_mips);
    }
    dependency_names_.push_back(std::move(model));
  }
  // Character families share support clips while retaining distinct meshes.
  const auto alias = name.starts_with("ea") ? std::string_view("ea0")
                     : (name.starts_with("pw") || name == "m09")
                         ? std::string_view("pw0")
                         : name;
  if (std::ranges::find(support_names_, alias) == support_names_.end()) {
    character_support_.push_back(
        content::load_character_support(files_, catalogs_, alias, memory_));
    support_names_.emplace_back(alias, memory::Allocator<char>(memory_));
  }
}
void MissionLoading::load_npc_palette() {
  auto definition = content::load_texture_definition(
      files_, catalogs_, "NPC_coller_variation", memory_);
  palette_definition_ = std::move(definition);
  if (renderer_ != nullptr) {
    if (!palette_definition_) {
      throw std::logic_error("NPC texture definition disappeared");
    }
    palette_residency_.emplace(*renderer_, palette_definition_.value(), memory_,
                               options_.progress, options_.texture_mips);
    palette_ = make_npc_palette(palette_definition_.value(),
                                palette_residency_->textures, memory_);
  }
}
void MissionLoading::load_placements() {
  constexpr std::array<std::string_view, 2> suffixes{".settbl", "_ene.settbl"};
  constexpr std::size_t kFlagsOffset = 36, kHealthColumn = 3;
  constexpr std::size_t kCellBytes = 8, kValueOffset = 4, kByteBits = 8;
  for (std::size_t index = 0; index < suffixes.size(); ++index) {
    const auto source = optional_member(
        request_name_ +
        memory::String(suffixes.at(index), memory::Allocator<char>(memory_)));
    if (source.empty()) continue;
    PlacementDefinition definition(memory_);
    definition.bytes.assign(source.begin(), source.end());
    definition.table = formats::SectionTable::parse(definition.bytes, memory_);
    if (!definition.table) {
      throw std::logic_error("Placement table construction failed");
    }
    const auto& table = definition.table.value();
    const auto characters = table.find("chara");
    const auto levels = table.find("level");
    if (characters && levels && (definition.bytes.at(kFlagsOffset) & 1U) == 0) {
      float factor = 1.0F;
      const auto& level_rows = table.sections().at(*levels);
      for (std::size_t row = 0; row < level_rows.rows; ++row) {
        if (table.integer({*levels, row, 0}).value_or(0) ==
            static_cast<std::int32_t>(options_.difficulty)) {
          factor = table.number({*levels, row, 1}).value_or(0.0F);
          break;
        }
      }
      if (!std::isfinite(factor)) {
        throw std::runtime_error("Nonfinite placement difficulty factor");
      }
      if (factor <= 0.0F) factor = 1.0F;
      constexpr float kNativeHealthScale = 0.5F;
      factor *= kNativeHealthScale;
      const auto& rows = table.sections().at(*characters);
      for (std::size_t row = 0; row < rows.rows; ++row) {
        const auto name = table.text({*characters, row, 1});
        if (!name) {
          throw std::runtime_error("Placement character has no model name");
        }
        // Native classification excludes player and human character families.
        if (name->starts_with("pw") || name->starts_with("hm") ||
            name->starts_with("hw") || name->starts_with("hr")) {
          continue;
        }
        auto health =
            table.integer({*characters, row, kHealthColumn}).value_or(0);
        if (health > 1) {
          const auto scaled = static_cast<float>(health) * factor;
          if (!std::isfinite(scaled) ||
              static_cast<double>(scaled) >
                  std::numeric_limits<std::int32_t>::max()) {
            throw std::runtime_error(
                "Placement scaled health exceeds native range");
          }
          health = static_cast<std::int32_t>(scaled);
        }
        const auto at = rows.cells +
                        (row * rows.columns + kHealthColumn) * kCellBytes +
                        kValueOffset;
        for (std::size_t byte = 0; byte < kValueOffset; ++byte) {
          definition.bytes.at(at + byte) = static_cast<std::uint8_t>(
              static_cast<std::uint32_t>(health) >> (byte * kByteBits));
        }
      }
      definition.bytes.at(kFlagsOffset) |= 1U;
    }
    placements_.at(index) = std::move(definition);
  }
  // Table registration resets the original event callback registry. These
  // inactive script bindings do not run until later gameplay emits an event.
  callbacks_.fill({});
  constexpr std::int32_t kClearEvent = 1001, kGameOverEvent = 1000;
  callbacks_.at(0) = {kClearEvent, "clear", false};
  callbacks_.at(1) = {kGameOverEvent, "gameover", false};
  memory::Vector<std::string_view> characters{
      memory::Allocator<std::string_view>(memory_)};
  memory::Vector<std::string_view> weapons{
      memory::Allocator<std::string_view>(memory_)};
  for (const auto& placement : placements_) {
    if (!placement || !placement->table) continue;
    const auto& table = placement->table.value();
    const auto section = table.find("chara");
    if (!section) continue;
    constexpr std::size_t kWeaponColumn = 14;
    for (std::size_t row = 0; row < table.sections().at(*section).rows; ++row) {
      const auto name = table.text({*section, row, 1});
      if (name) characters.push_back(*name);
      weapons.push_back(
          table.text({*section, row, kWeaponColumn}).value_or("wg1"));
    }
  }
  std::ranges::sort(characters);
  characters.erase(std::unique(characters.begin(), characters.end()),
                   characters.end());
  std::ranges::sort(weapons);
  weapons.erase(std::unique(weapons.begin(), weapons.end()), weapons.end());
  for (const auto name : characters) {
    if (name == "vt0" || name == "vh0") {
      preload_model(name);
      if (name == "vh0") preload_character("lo0");
    } else {
      preload_character(name);
      if (name == "ec0" || name == "eg0") {
        preload_character("ee0");
        preload_character("es0");
        if (name == "eg0") preload_character("ba0e");
      }
    }
  }
  for (const auto name : weapons) preload_character(name);
  load_npc_palette();
}

}  // namespace bw::game
