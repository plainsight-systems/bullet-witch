// Integration test: bw/content_converter/conversion.hpp's run_conversion
// over the player's install. pw0_counts reads the PW0 cache the CTest
// fixture pw0_cache converts once with the content_converter executable;
// the others convert a copy of part of the install into a cache under
// <scratch>/<scenario>.tmp, removed before and after. Nothing is written
// under the install.
#include "bw/animation/skeleton_asset.hpp"
#include "bw/content/asset_id.hpp"
#include "bw/content/cooked_asset.hpp"
#include "bw/content/install.hpp"
#include "bw/content/layers.hpp"
#include "bw/content_converter/conversion.hpp"
#include "bw/content_converter/cpr_container.hpp"
#include "bw/content_converter/material_mapping.hpp"
#include "bw/content_converter/rat_index.hpp"
#include "bw/content_converter/texture_reader.hpp"
#include "bw/rendering/geometry/mesh_asset.hpp"
#include "bw/rendering/materials/material_asset.hpp"
#include "bw/testing/harness.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <map>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace {

namespace fs = std::filesystem;
namespace cc = bw::content_converter;
namespace content = bw::content;
using bw::testing::require;
using Bytes = std::vector<std::byte>;

struct Arguments {
  fs::path scratch{};
  fs::path install{};
  // The cache the pw0_cache fixture converted (prefix model.pw0.).
  fs::path pw0_cache{};
};

constexpr std::string_view kPw0Prefix = "model.pw0.";
constexpr std::string_view kPw0Mesh = "model.pw0.mesh";
constexpr std::string_view kPw0ModelContainer =
    "data/data/stable/graphics/chara/PW0/model/mesh.cpr";
constexpr std::string_view kPw0OwnTextures = "model.pw0.texture.";
constexpr std::string_view kPw0Directory = "data/data/stable/graphics/chara/PW0";
constexpr std::string_view kStable = "data/data/stable";

// The scenario's own directory, empty on entry and removed on exit.
class ScratchTree {
public:
  ScratchTree(const fs::path& scratch, std::string_view scenario)
      : root_(scratch / (std::string(scenario) + ".tmp")) {
    fs::remove_all(root_);
    fs::create_directories(root_);
  }
  ScratchTree(const ScratchTree&) = delete;
  ScratchTree& operator=(const ScratchTree&) = delete;
  ScratchTree(ScratchTree&&) = delete;
  ScratchTree& operator=(ScratchTree&&) = delete;
  ~ScratchTree() {
    std::error_code ec;
    fs::remove_all(root_, ec);
  }
  [[nodiscard]] const fs::path& root() const noexcept { return root_; }

private:
  fs::path root_;
};

Bytes read_bytes(const fs::path& path) {
  std::ifstream in(path, std::ios::binary);
  require(in.good(), "readable: " + path.string());
  const std::vector<char> chars((std::istreambuf_iterator<char>(in)),
                                std::istreambuf_iterator<char>());
  Bytes bytes(chars.size());
  std::transform(chars.begin(), chars.end(), bytes.begin(),
                 [](char c) { return static_cast<std::byte>(c); });
  return bytes;
}

void write_bytes(const fs::path& path, std::span<const std::byte> bytes) {
  fs::create_directories(path.parent_path());
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  out.write(reinterpret_cast<const char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
  require(out.good(), "written: " + path.string());
}

cc::ConvertOptions options_for(const fs::path& cache, std::string_view prefix,
                               std::uint32_t threads,
                               const cc::TexturePipelineOptions& texture = {}) {
  cc::ConvertOptions options{};
  options.output = cache;
  options.only_prefix = std::string(prefix);
  options.threads = threads;
  options.texture = texture;
  return options;
}

// A run that must succeed with no failed asset.
cc::ConvertResult convert(const content::InstallLocation& install, const fs::path& cache,
                          std::string_view prefix, std::uint32_t threads,
                          const cc::TexturePipelineOptions& texture = {}) {
  cc::ConvertResult result =
      cc::run_conversion(options_for(cache, prefix, threads, texture), install);
  require(result.ok(), "the conversion runs: " + result.detail);
  if (!result.report.failed.empty()) {
    std::cerr << cc::render_report(result.report);
  }
  require(result.report.failed.empty(), "no asset fails");
  return result;
}

// Every cooked asset of a cache, read back by its header.
struct Census {
  std::map<content::AssetKind, std::set<std::string>> ids{};
  // Texture ids named by materials (textures and the unmapped TR slots).
  std::set<std::string> named_textures{};
  std::uint64_t slots{};
};

// Materials' slots: every texture record, plus each unmapped field named
// "TR.<tag>" (a second slot of a tag), whose value is the slot's id.
void count_material(std::span<const std::byte> payload, Census& census, std::string_view id) {
  bw::materials::MaterialAssetView view;
  const bw::materials::MaterialAssetFailure failure =
      bw::materials::decode_material_asset(payload, view);
  require(failure.error == bw::materials::MaterialAssetError::None,
          "material decodes: " + std::string(id));
  for (std::uint32_t t = 0; t < view.header().texture_count; ++t) {
    const bw::materials::TextureRecord record = view.texture_record(t);
    census.named_textures.insert(std::string(view.string(record.id_offset, record.id_length)));
    ++census.slots;
  }
  for (std::uint32_t u = 0; u < view.header().unmapped_count; ++u) {
    const bw::materials::UnmappedFieldRecord record = view.unmapped_record(u);
    if (view.string(record.name_offset, record.name_length).starts_with("TR.")) {
      census.named_textures.insert(
          std::string(view.string(record.value_offset, record.value_length)));
      ++census.slots;
    }
  }
}

Census take_census(const fs::path& cache) {
  Census census;
  for (const fs::directory_entry& entry : fs::recursive_directory_iterator(cache)) {
    if (!entry.is_regular_file()) {
      continue;
    }
    const Bytes file = read_bytes(entry.path());
    if (entry.path().filename() == content::kConvertedCacheRecordFile) {
      continue;
    }
    const content::CookedParse parse = content::parse_cooked_header(file);
    require(parse.ok(), "a cooked asset parses: " + entry.path().string());
    require(content::verify_payload_hash(file, parse.header) == content::CookedError::None,
            "its payload hash holds: " + entry.path().string());
    const std::string id(content::cooked_id(file, parse.header));
    require(fs::relative(entry.path(), cache).generic_string() ==
                content::asset_relative_path(content::make_asset_id(id).id),
            "the file sits at its id's path: " + id);
    census.ids[parse.header.kind].insert(id);
    if (parse.header.kind == content::AssetKind::Material) {
      count_material(content::cooked_payload(file, parse.header), census, id);
    }
  }
  return census;
}

std::size_t count(const Census& census, content::AssetKind kind) {
  const auto it = census.ids.find(kind);
  return it == census.ids.end() ? 0U : it->second.size();
}

void require_every_slot_resolved(const Census& census) {
  const auto textures = census.ids.find(content::AssetKind::Texture);
  for (const std::string& id : census.named_textures) {
    require(textures != census.ids.end() && textures->second.contains(id),
            "a texture a material names is in the cache: " + id);
  }
}

// (conversion.hpp, run_conversion: Ids) over PW0's assets as the
// executable converted them: the counts the content holds and every
// material slot resolved. Alone catches a reader or an id rule that stops
// matching the shipped containers.
void pw0_counts(const Arguments& arguments) {
  const fs::path& cache = arguments.pw0_cache;
  const Census census = take_census(cache);

  for (const auto& [kind, ids] : census.ids) {
    for (const std::string& id : ids) {
      require(id.starts_with(kPw0Prefix), "only ids with the prefix are written: " + id);
    }
  }
  require(count(census, content::AssetKind::Mesh) == 1U, "one mesh");
  require(count(census, content::AssetKind::Skeleton) == 1U, "one skeleton");
  require(count(census, content::AssetKind::MorphTargets) == 4U, "four morph sets");

  const Bytes mesh_file =
      read_bytes(cache / content::asset_relative_path(content::make_asset_id(kPw0Mesh).id));
  const content::CookedParse mesh_parse = content::parse_cooked_header(mesh_file);
  require(mesh_parse.ok(), "mesh frame parses");
  bw::geometry::MeshAssetView mesh;
  require(
      bw::geometry::decode_mesh_asset(content::cooked_payload(mesh_file, mesh_parse.header), mesh)
              .error == bw::geometry::MeshAssetError::None,
      "mesh decodes");
  require(mesh.group_count() == 9U, "nine mesh groups");
  require(mesh.bone_map_count() == 46U, "46 bone maps");
  std::set<std::uint32_t> morph_sets;
  for (std::uint32_t g = 0; g < mesh.group_count(); ++g) {
    if (mesh.group(g).morph_set != bw::geometry::kNoMorphSet) {
      morph_sets.insert(mesh.group(g).morph_set);
    }
  }
  require(morph_sets == std::set<std::uint32_t>{0, 1, 2, 3}, "groups name morph sets 0 to 3");
  require(count(census, content::AssetKind::Material) == mesh.header().material_count,
          "one material asset per mesh material");

  const Bytes skeleton_file =
      read_bytes(cache / content::asset_relative_path(
                             content::make_asset_id(std::string(kPw0Mesh) + ".skeleton").id));
  const content::CookedParse skeleton_parse = content::parse_cooked_header(skeleton_file);
  bw::animation::SkeletonAssetView skeleton;
  require(skeleton_parse.ok() &&
              bw::animation::decode_skeleton_asset(
                  content::cooked_payload(skeleton_file, skeleton_parse.header), skeleton)
                      .error == bw::animation::SkeletonAssetError::None,
          "skeleton decodes");
  require(skeleton.bone_count() == 125U, "125 bones");

  std::size_t own = 0;
  for (const std::string& id : census.ids.at(content::AssetKind::Texture)) {
    own += id.starts_with(kPw0OwnTextures) ? 1U : 0U;
  }
  require(own == 14U, "14 own textures");
  require(count(census, content::AssetKind::Texture) == own, "the textures written are PW0's own");
  // PW0's texture table names no shared texture; a shared one is
  // shared_texture_resolves's.
  for (const std::string& id : census.named_textures) {
    require(id.starts_with(kPw0OwnTextures), "a PW0 material names an own texture: " + id);
  }
  require(census.slots > 0U, "the materials name textures");
  require_every_slot_resolved(census);
}

// ---- a partial install ----------------------------------------------------

void append_u32(Bytes& out, std::uint32_t value) {
  for (std::size_t i = 0; i < 4; ++i) {
    out.push_back(static_cast<std::byte>((value >> (8U * i)) & 0xFFU));
  }
}
void put_u32(Bytes& out, std::size_t at, std::uint32_t value) {
  for (std::size_t i = 0; i < 4; ++i) {
    out[at + i] = static_cast<std::byte>((value >> (8U * i)) & 0xFFU);
  }
}

struct CatalogEntry {
  std::string directory;
  std::string file;
};

// A catalog in rat_index.hpp's framing: sections 0 to 3 empty, section 4
// the file entries, section 5 the string pool with the empty string at 0.
Bytes rat_catalog(std::span<const CatalogEntry> entries) {
  Bytes strings{std::byte{0}};
  const auto add = [&strings](std::string_view text) {
    const auto offset = static_cast<std::uint32_t>(strings.size());
    for (const char c : text) {
      strings.push_back(static_cast<std::byte>(c));
    }
    strings.push_back(std::byte{0});
    return offset;
  };
  Bytes files;
  for (const CatalogEntry& entry : entries) {
    append_u32(files, add(entry.directory));
    append_u32(files, add(entry.file));
  }
  Bytes rat(cc::kRatHeaderBytes, std::byte{0});
  for (std::size_t k = 0; k < 4; ++k) {
    rat[k] = static_cast<std::byte>("RAT "[k]);
  }
  const std::array<const Bytes*, 6> sections{nullptr, nullptr, nullptr, nullptr, &files, &strings};
  for (std::size_t s = 0; s < sections.size(); ++s) {
    put_u32(rat, 0x10U + (8U * s), static_cast<std::uint32_t>(rat.size()));
    if (sections[s] != nullptr) {
      put_u32(rat, 0x14U + (8U * s), static_cast<std::uint32_t>(sections[s]->size()));
      rat.insert(rat.end(), sections[s]->begin(), sections[s]->end());
    }
  }
  put_u32(rat, 4, static_cast<std::uint32_t>(rat.size()));
  return rat;
}

// One container of the install copied into a partial install: `from` and
// `to` are relative to data/data/stable ('/' separators); `to` differs from
// `from` when a scenario copies a container under a second name.
struct CopiedContainer {
  std::string_view from;
  std::string_view to;
};

constexpr std::array<CopiedContainer, 3> kPw0Containers{{
    {"graphics/chara/PW0/model/mesh.cpr", "graphics/chara/PW0/model/mesh.cpr"},
    {"graphics/chara/PW0/texture/texture.cpr", "graphics/chara/PW0/texture/texture.cpr"},
    {"graphics/chara/PW0/texture/default.cpr", "graphics/chara/PW0/texture/default.cpr"},
}};

// The containers under `root`, with a stable.rat that lists exactly them.
content::InstallLocation partial_install(const fs::path& install, const fs::path& root,
                                         std::span<const CopiedContainer> containers) {
  std::vector<CatalogEntry> entries;
  for (const CopiedContainer& container : containers) {
    const fs::path to = root / kStable / container.to;
    fs::create_directories(to.parent_path());
    fs::copy_file(install / kStable / container.from, to);
    const std::string_view relative = container.to;
    const std::size_t slash = relative.rfind('/');
    std::string directory(relative.substr(0, slash));
    std::replace(directory.begin(), directory.end(), '/', '\\');
    entries.push_back({std::move(directory), std::string(relative.substr(slash + 1))});
  }
  write_bytes(root / kStable / "stable.rat", rat_catalog(entries));
  content::InstallLocation location{};
  location.path = root;
  location.graphics_containers = static_cast<std::uint32_t>(containers.size());
  return location;
}

content::InstallLocation pw0_install(const fs::path& install, const fs::path& root) {
  return partial_install(install, root, kPw0Containers);
}

// A container's bytes with its parse, for an edit in place.
struct EditedContainer {
  fs::path path{};
  Bytes file{};
  cc::CprContainer container{};
  explicit EditedContainer(fs::path at) : path(std::move(at)), file(read_bytes(path)) {
    require(cc::parse_cpr(file, container) == cc::CprError::None,
            "container parses: " + path.string());
  }
  [[nodiscard]] std::size_t offset_of(std::string_view text) const {
    return static_cast<std::size_t>(reinterpret_cast<const std::byte*>(text.data()) -
                                    container.file().data());
  }
  [[nodiscard]] std::size_t offset_of(std::span<const std::byte> bytes) const {
    return static_cast<std::size_t>(bytes.data() - container.file().data());
  }
  [[nodiscard]] const cc::CprRecord& record_named(std::string_view qualified_name) const {
    const cc::CprRecord* record = container.find_by_qualified_name(qualified_name);
    require(record != nullptr, "record found: " + std::string(qualified_name));
    return *record;
  }
  [[nodiscard]] cc::TextureTable table() const {
    cc::TextureTable table;
    require(cc::texture_table_from_default(container, table) == cc::TextureTableError::None,
            "texture table reads: " + path.string());
    require(!table.names.empty(), "the texture table names textures");
    return table;
  }
  void write() const { write_bytes(path, file); }
};

struct CachedFile {
  Bytes bytes{};
  fs::file_time_type written{};
};
std::map<std::string, CachedFile> snapshot(const fs::path& cache) {
  std::map<std::string, CachedFile> files;
  for (const fs::directory_entry& entry : fs::recursive_directory_iterator(cache)) {
    if (entry.is_regular_file()) {
      files[fs::relative(entry.path(), cache).generic_string()] =
          CachedFile{read_bytes(entry.path()), entry.last_write_time()};
    }
  }
  return files;
}

std::uint32_t crc32(std::span<const std::byte> bytes) {
  std::uint32_t crc = 0xFFFFFFFFU;
  for (const std::byte b : bytes) {
    crc ^= std::to_integer<std::uint32_t>(b);
    for (int bit = 0; bit < 8; ++bit) {
      crc = (crc >> 1U) ^ (0xEDB88320U & (0U - (crc & 1U)));
    }
  }
  return ~crc;
}

std::uint32_t big_endian(std::span<const std::byte> bytes, std::size_t at) {
  std::uint32_t value = 0;
  for (std::size_t i = 0; i < 4; ++i) {
    value = (value << 8U) | std::to_integer<std::uint32_t>(bytes[at + i]);
  }
  return value;
}

// Changes one byte of the pHYs chunk of the named texture record's PNG and
// restores that chunk's CRC, so the PNG still decodes to the same pixels and
// only its bytes differ.
void change_one_png_byte(const fs::path& container_path, std::string_view qualified_name) {
  Bytes file = read_bytes(container_path);
  cc::CprContainer container;
  require(cc::parse_cpr(file, container) == cc::CprError::None, "texture container parses");
  const cc::CprRecord* record = container.find_by_qualified_name(qualified_name);
  require(record != nullptr, "texture record found: " + std::string(qualified_name));
  const auto png_at = static_cast<std::size_t>(record->payload.data() - container.file().data());
  const std::size_t png_size = record->payload.size();
  constexpr std::size_t kSignatureBytes = 8;
  for (std::size_t at = png_at + kSignatureBytes; at + 12U <= png_at + png_size;) {
    const std::uint32_t length = big_endian(file, at);
    const bool phys =
        std::to_integer<char>(file[at + 4]) == 'p' && std::to_integer<char>(file[at + 5]) == 'H' &&
        std::to_integer<char>(file[at + 6]) == 'Y' && std::to_integer<char>(file[at + 7]) == 's';
    if (phys) {
      file[at + 8 + 3] ^= std::byte{1};
      const std::uint32_t crc = crc32(std::span(file).subspan(at + 4, 4U + length));
      for (std::size_t i = 0; i < 4; ++i) {
        file[at + 8 + length + i] = static_cast<std::byte>((crc >> (8U * (3U - i))) & 0xFFU);
      }
      write_bytes(container_path, file);
      return;
    }
    at += 12U + length;
  }
  require(false, "the PNG has a pHYs chunk");
}

// Flips one bit of the first byte of PW0's first vertex buffer record; the
// reader copies buffers out, so the mesh still reads.
void change_vertex_buffer_byte(const fs::path& install) {
  EditedContainer mesh(install / kPw0Directory / "model/mesh.cpr");
  const cc::CprRecord* record = mesh.container.find_record("vertex_000.bin");
  require(record != nullptr && record->tag == cc::RecordTag::Dxvb && !record->payload.empty(),
          "vertex_000.bin is a vertex buffer record");
  mesh.file[mesh.offset_of(record->payload)] ^= std::byte{1};
  mesh.write();
}

// Flips the case of the first upper-case letter of the texture table's first
// name: the hash input changes, the name still resolves (compared ignoring
// case) and its id folds to the same text.
void change_table_name_case(const fs::path& install) {
  EditedContainer table(install / kPw0Directory / "texture/default.cpr");
  const std::string_view name = table.table().names[0];
  const std::size_t letter = name.find_first_of("ABCDEFGHIJKLMNOPQRSTUVWXYZ");
  require(letter != std::string_view::npos, "the table name has an upper-case letter");
  const std::size_t at = table.offset_of(name) + letter;
  table.file[at] = static_cast<std::byte>(std::to_integer<char>(table.file[at]) - 'A' + 'a');
  table.write();
}

// The cached file of the one asset whose identity matches.
fs::path cached_path(const fs::path& cache, std::string_view id) {
  return cache / content::asset_relative_path(content::make_asset_id(id).id);
}

struct Identity {
  std::string id{};
  content::AssetKind kind{};
};

// (conversion.hpp, run_conversion: Determinism, Parallel, Incremental, Write;
// Input hash) over a partial install of PW0's containers: two fresh caches
// converted with one worker and with the hardware's count are byte-identical
// file by file, record included; a rerun skips every asset; then each row
// changes one input and the next run rewrites exactly the assets whose hash
// covers it, leaving every other file untouched (same bytes, same write
// time): a PNG byte (that texture), a vertex buffer byte (the mesh, not its
// materials), the case of a texture-table name (every material, not the
// mesh), a cached file carrying another format version or not parsing
// (that file), and each texture pipeline option (every texture).
void incremental_reconverts_dependents(const Arguments& arguments) {
  const ScratchTree tree(arguments.scratch, "incremental_reconverts_dependents");
  const content::InstallLocation install = pw0_install(arguments.install, tree.root() / "install");
  const fs::path cache_a = tree.root() / "cache_a";
  const fs::path cache_b = tree.root() / "cache_b";
  const cc::ConvertResult first = convert(install, cache_a, {}, 1);
  convert(install, cache_b, {}, 0);
  const std::map<std::string, CachedFile> a = snapshot(cache_a);
  const std::map<std::string, CachedFile> b = snapshot(cache_b);
  require(a.size() == b.size() && a.size() > 1U, "both caches hold the same number of files");
  for (const auto& [path, file] : a) {
    const auto other = b.find(path);
    require(other != b.end(), "both caches hold " + path);
    require(other->second.bytes == file.bytes, "byte-identical across worker counts: " + path);
  }
  const std::uint64_t assets = first.report.converted;
  require(assets == a.size() - 1U, "every file but the record is a converted asset");
  std::map<std::string, Identity> identities;
  for (const auto& [path, file] : a) {
    if (path != content::kConvertedCacheRecordFile) {
      const content::CookedParse parse = content::parse_cooked_header(file.bytes);
      require(parse.ok(), "a cached asset parses: " + path);
      identities[path] = {std::string(content::cooked_id(file.bytes, parse.header)),
                          parse.header.kind};
    }
  }

  const cc::ConvertResult rerun = convert(install, cache_a, {}, 0);
  require(rerun.report.converted == 0U && rerun.report.skipped_unchanged == assets,
          "an unchanged rerun skips every asset");

  constexpr std::string_view kChangedTexture = "model.pw0.texture.dds.pw0_magazine_psd";
  const std::string skeleton_id = std::string(kPw0Mesh) + ".skeleton";
  const std::string shapes_id = std::string(kPw0Mesh) + ".shapes.0";
  cc::TexturePipelineOptions uber{};
  uber.bc7_uber_level = uber.bc7_uber_level == 0U ? 1U : uber.bc7_uber_level - 1U;
  cc::TexturePipelineOptions uber_and_green = uber;
  uber_and_green.normal_green = cc::NormalMapConvention::FlipGreen;

  struct Change {
    std::string_view name;
    std::function<void()> apply;
    std::function<bool(const Identity&)> rewrites;
    cc::TexturePipelineOptions texture;
  };
  const std::vector<Change> changes{
      {"a PNG byte",
       [&] {
         change_one_png_byte(install.path / kPw0Directory / "texture/texture.cpr", kChangedTexture);
       },
       [&](const Identity& asset) { return asset.id == kChangedTexture; },
       {}},
      {"a vertex buffer byte",
       [&] { change_vertex_buffer_byte(install.path); },
       [](const Identity& asset) { return asset.id == kPw0Mesh; },
       {}},
      {"a texture-table name's case",
       [&] { change_table_name_case(install.path); },
       [](const Identity& asset) { return asset.kind == content::AssetKind::Material; },
       {}},
      {"a cached file of another format version",
       [&] {
         Bytes file = read_bytes(cached_path(cache_a, skeleton_id));
         put_u32(file, 4, content::kCookedFormatVersion + 1U);
         write_bytes(cached_path(cache_a, skeleton_id), file);
       },
       [&](const Identity& asset) { return asset.id == skeleton_id; },
       {}},
      {"a cached file that does not parse",
       [&] {
         Bytes file = read_bytes(cached_path(cache_a, shapes_id));
         file.resize(10);
         write_bytes(cached_path(cache_a, shapes_id), file);
       },
       [&](const Identity& asset) { return asset.id == shapes_id; },
       {}},
      {"the BC7 uber level", [] {},
       [](const Identity& asset) { return asset.kind == content::AssetKind::Texture; }, uber},
      {"the green convention", [] {},
       [](const Identity& asset) { return asset.kind == content::AssetKind::Texture; },
       uber_and_green},
  };
  for (std::size_t row = 0; row < changes.size(); ++row) {
    const Change& change = changes[row];
    change.apply();
    const std::map<std::string, CachedFile> before = snapshot(cache_a);
    const cc::ConvertResult result = convert(install, cache_a, {}, 0, change.texture);
    const std::map<std::string, CachedFile> after = snapshot(cache_a);
    require(after.size() == before.size(), row, "no file appears or disappears");
    std::uint64_t expected = 0;
    for (const auto& [path, identity] : identities) {
      const CachedFile& was = before.at(path);
      const CachedFile& now = after.at(path);
      if (change.rewrites(identity)) {
        ++expected;
        const content::CookedParse parse = content::parse_cooked_header(now.bytes);
        require(parse.ok() && now.written != was.written, row,
                std::string(change.name) + " rewrites " + identity.id);
      } else {
        require(now.bytes == was.bytes && now.written == was.written, row,
                std::string(change.name) + " leaves untouched " + identity.id);
      }
    }
    require(expected > 0U, row, "the change concerns an asset");
    require(result.report.converted == expected &&
                result.report.skipped_unchanged == assets - expected,
            row, std::string(change.name) + ": the report counts the rewritten and the skipped");
  }
}

// (conversion.hpp, Ids: a texture record by its qualified name; material
// slots resolved through every record the run indexed) over a partial
// install of M05's model and texture table and the shared container its
// table names, converted with no prefix: the shared textures are written
// and every slot resolves to one of them.
void shared_texture_resolves(const Arguments& arguments) {
  const ScratchTree tree(arguments.scratch, "shared_texture_resolves");
  constexpr std::array<CopiedContainer, 3> kM05{{
      {"graphics/chara/M05/model/mesh.cpr", "graphics/chara/M05/model/mesh.cpr"},
      {"graphics/chara/M05/texture/default.cpr", "graphics/chara/M05/texture/default.cpr"},
      {"graphics/chara/shared_texture/Crow_Share.cpr",
       "graphics/chara/shared_texture/Crow_Share.cpr"},
  }};
  const content::InstallLocation install =
      partial_install(arguments.install, tree.root() / "install", kM05);
  const fs::path cache = tree.root() / "cache";
  convert(install, cache, {}, 0);
  const Census census = take_census(cache);
  require(count(census, content::AssetKind::Mesh) == 1U, "M05's mesh is written");
  require(census.slots > 0U, "M05's materials name textures");
  for (const std::string& id : census.named_textures) {
    require(id.starts_with("shared_texture.crow_share."),
            "an M05 material names a shared texture: " + id);
  }
  require_every_slot_resolved(census);
}

// ---- the executable --------------------------------------------------------

// (conversion.hpp, Ids: collision; Report: failed with id and reason) over
// PW0's containers and a second model container deriving the same mesh id:
// the first converts, every asset of the second is a failed row naming the
// container that holds the id first. Alone catches a second container
// overwriting the first without a failure.
void collision_fails_second_container(const Arguments& arguments) {
  const ScratchTree tree(arguments.scratch, "collision_fails_second_container");
  std::vector<CopiedContainer> containers(kPw0Containers.begin(), kPw0Containers.end());
  containers.push_back({"graphics/chara/PW0/model/mesh.cpr", "graphics/chara/PX0/model/mesh.cpr"});
  const content::InstallLocation install =
      partial_install(arguments.install, tree.root() / "install", containers);
  const std::string prefix = std::string(kPw0Mesh) + ".material";
  const cc::ConvertResult result =
      cc::run_conversion(options_for(tree.root() / "cache", prefix, 0), install);
  require(result.ok(), "the run with a colliding model completes: " + result.detail);
  require(result.report.converted > 1U, "the first model's materials convert");
  require(result.report.failed.size() == result.report.converted,
          "every material of the second model is a failed row");
  for (const cc::FailedAsset& failed : result.report.failed) {
    require(failed.id.starts_with(prefix) &&
                failed.reason.find(kPw0ModelContainer) != std::string::npos,
            "a failed row names the container holding the id first: " + failed.reason);
  }
}

using Scenario = bw::testing::Scenario<Arguments>;
constexpr std::array kScenarios{
    Scenario{"pw0_counts", &pw0_counts},
    Scenario{"incremental_reconverts_dependents", &incremental_reconverts_dependents},
    Scenario{"shared_texture_resolves", &shared_texture_resolves},
    Scenario{"collision_fails_second_container", &collision_fails_second_container},
};

} // namespace

int main(int argc, char** argv) {
  return bw::testing::run_scenario(
      argc, argv, kScenarios, "<scratch directory> <install root> <PW0 cache>",
      [](std::span<char* const> rest, Arguments& arguments) {
        if (rest.size() != 3U || std::string_view(rest[0]).empty() ||
            std::string_view(rest[1]).empty() || std::string_view(rest[2]).empty()) {
          return false;
        }
        arguments.scratch = rest[0];
        arguments.install = rest[1];
        arguments.pw0_cache = rest[2];
        return true;
      });
}
