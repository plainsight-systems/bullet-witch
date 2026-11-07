// Integration test: bw/content_converter/conversion.hpp's run_conversion.
// pw0_counts reads the PW0 cache the CTest fixture pw0_cache converts once
// with the content_converter executable from the player's install; the
// others convert a synthetic install of containers built from the readers'
// stated framing (bw/testing/cpr_fixture.hpp) into <scratch>/<scenario>.tmp,
// removed before and after, and need no game data. Nothing is written under
// the player's install.
#include "bw/animation/skeleton_asset.hpp"
#include "bw/content/asset_id.hpp"
#include "bw/content/cooked_asset.hpp"
#include "bw/content/install.hpp"
#include "bw/content/layers.hpp"
#include "bw/content_converter/conversion.hpp"
#include "bw/content_converter/cpr_container.hpp"
#include "bw/content_converter/material_mapping.hpp"
#include "bw/content_converter/mesh_reader.hpp"
#include "bw/content_converter/rat_index.hpp"
#include "bw/content_converter/texture_reader.hpp"
#include "bw/rendering/geometry/mesh_asset.hpp"
#include "bw/rendering/materials/material_asset.hpp"
#include "bw/testing/cpr_fixture.hpp"
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
namespace fx = bw::testing::cpr;
using bw::testing::require;
using Bytes = std::vector<std::byte>;

struct Arguments {
  fs::path scratch{};
  // Empty when no game data is configured; only pw0_counts reads them.
  fs::path install{};
  // The cache the pw0_cache fixture converted (prefix model.pw0.).
  fs::path pw0_cache{};
};

constexpr std::string_view kPw0Prefix = "model.pw0.";
constexpr std::string_view kPw0Mesh = "model.pw0.mesh";
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

bool is_unmapped_file(const fs::path& path) {
  return path.string().ends_with(cc::kUnmappedFieldsSuffix);
}

// Each unmapped field named "TR.<tag>" (a slot no lit parameter takes, or a
// second slot of a tag), whose value is the slot's id.
void count_unmapped_file(const fs::path& path, Census& census) {
  const Bytes file = read_bytes(path);
  const std::string text(reinterpret_cast<const char*>(file.data()), file.size());
  std::vector<cc::UnmappedField> fields;
  require(cc::parse_unmapped_fields(text, fields),
          "an unmapped fields file parses: " + path.string());
  for (const cc::UnmappedField& field : fields) {
    if (field.name.starts_with("TR.")) {
      census.named_textures.insert(field.value);
      ++census.slots;
    }
  }
}

// Materials' slots: every texture record.
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
}

Census take_census(const fs::path& cache) {
  Census census;
  for (const fs::directory_entry& entry : fs::recursive_directory_iterator(cache)) {
    if (!entry.is_regular_file()) {
      continue;
    }
    if (entry.path().filename() == content::kConvertedCacheRecordFile) {
      continue;
    }
    if (is_unmapped_file(entry.path())) {
      count_unmapped_file(entry.path(), census);
      continue;
    }
    const Bytes file = read_bytes(entry.path());
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

// A container of the install, read and parsed; the bytes outlive the views.
struct ReadContainer {
  Bytes file{};
  cc::CprContainer container{};
  explicit ReadContainer(const fs::path& path) : file(read_bytes(path)) {
    require(cc::parse_cpr(file, container) == cc::CprError::None, "parses: " + path.string());
  }
};

// (conversion.hpp, run_conversion: Ids; Meshes, tangent handedness; Write)
// over PW0's assets as the executable converted them: the counts the content
// holds, more vertices than PW0 stores (the handedness rule is wired into
// the run) and every material slot resolved, the unmapped TR slots read from
// the unmapped fields files. Alone catches a reader or an id rule that stops
// matching the shipped containers.
void pw0_counts(const Arguments& arguments) {
  require(!arguments.install.empty() && !arguments.pw0_cache.empty(),
          "the install and the PW0 cache are given");
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
  {
    const ReadContainer source_file(arguments.install / kPw0Directory / "model/mesh.cpr");
    cc::MeshSource source;
    require(cc::read_mesh_container(source_file.container, source).ok(), "PW0's mesh reads");
    std::uint64_t stored = 0;
    for (const cc::MeshGroupData& group : source.mesh.groups) {
      stored += group.vertex_count;
    }
    std::uint64_t converted = 0;
    for (std::uint32_t g = 0; g < mesh.group_count(); ++g) {
      converted += mesh.group(g).vertex_count;
    }
    require(converted > stored, "the handedness rule splits vertices: " + std::to_string(stored) +
                                    " stored, " + std::to_string(converted) + " converted");
  }

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

// ---- a synthetic install ----------------------------------------------------

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
    fx::append_text(strings, text);
    strings.push_back(std::byte{0});
    return offset;
  };
  Bytes files;
  for (const CatalogEntry& entry : entries) {
    fx::append_u32(files, add(entry.directory));
    fx::append_u32(files, add(entry.file));
  }
  Bytes rat(cc::kRatHeaderBytes, std::byte{0});
  for (std::size_t k = 0; k < 4; ++k) {
    rat[k] = static_cast<std::byte>("RAT "[k]);
  }
  const std::array<const Bytes*, 6> sections{nullptr, nullptr, nullptr, nullptr, &files, &strings};
  for (std::size_t s = 0; s < sections.size(); ++s) {
    fx::put_u32(rat, 0x10U + (8U * s), static_cast<std::uint32_t>(rat.size()));
    if (sections[s] != nullptr) {
      fx::put_u32(rat, 0x14U + (8U * s), static_cast<std::uint32_t>(sections[s]->size()));
      rat.insert(rat.end(), sections[s]->begin(), sections[s]->end());
    }
  }
  fx::put_u32(rat, 4, static_cast<std::uint32_t>(rat.size()));
  return rat;
}

// One container of a synthetic install: its path under data/data/stable
// ('/' separators) and its records.
struct SyntheticContainer {
  std::string path;
  std::vector<fx::Record> records;
};
using SyntheticInstall = std::vector<SyntheticContainer>;

constexpr std::string_view kModelPath = "graphics/chara/SX0/model/mesh.cpr";
constexpr std::string_view kMesh = "model.sx0.mesh";
constexpr std::string_view kTextureA = "model.sx0.texture.dds.sx0_a_psd";
constexpr std::string_view kTextureB = "model.sx0.texture.dds.sx0_b_psd";
constexpr std::string_view kSharedTexture = "shared_texture.sx_share.dds.sx_share_psd";
// The texture table's names, as the CALL stores them: material 0's base
// color is A and its normal map B; material 1's base color is the shared
// texture.
constexpr std::array<std::string_view, 3> kTableNames{"model.SX0.texture.dds.sx0_a_psd",
                                                      "model.SX0.texture.dds.sx0_b_psd",
                                                      "shared_texture.SX_Share.dds.sx_share_psd"};
// Positions of synthetic_install's containers.
constexpr std::size_t kModelContainer = 0;
constexpr std::size_t kTableContainer = 1;
constexpr std::size_t kTextureContainer = 2;

fx::Record texture_record(std::string_view qualified_name, Bytes png,
                          Bytes descriptor = fx::dxt1_descriptor()) {
  return {
      "DDS ", "texture.dds", std::string(qualified_name), std::move(png), std::move(descriptor), {},
      {}};
}

// The model of bw/testing/cpr_fixture.hpp (two groups, two materials whose
// slots name the table's three textures, a morph set, no skeleton) created
// by a CALL naming `mesh`.
std::vector<fx::Record> model_records(std::string_view mesh) {
  std::vector<fx::Record> records = fx::build_model(fx::ModelSpec{}).records;
  records.push_back({"CALL", "vfx2.mesh.create", std::string(mesh), {}, {}, {}, {}});
  return records;
}

// The model, its texture table, its texture container of A and B, and a
// shared container holding the third texture the table names.
SyntheticInstall synthetic_install() {
  return {
      {std::string(kModelPath), model_records("model.SX0.mesh")},
      {"graphics/chara/SX0/texture/default.cpr",
       fx::texture_table({cc::kTextureCreateCall, cc::kTableSlot, cc::kDdsSlot}, kTableNames)},
      {"graphics/chara/SX0/texture/texture.cpr",
       {texture_record(kTableNames[0], fx::png_bytes(fx::kPng)),
        texture_record(kTableNames[1], fx::png_bytes(fx::kPng))}},
      {"graphics/chara/shared_texture/SX_Share.cpr",
       {texture_record(kTableNames[2], fx::png_bytes(fx::kPng))}},
  };
}

// Writes every container under `root` and a stable.rat listing exactly them.
content::InstallLocation write_install(const fs::path& root, const SyntheticInstall& install) {
  std::vector<CatalogEntry> entries;
  for (const SyntheticContainer& container : install) {
    write_bytes(root / kStable / container.path, fx::build(container.records));
    const std::size_t slash = container.path.rfind('/');
    std::string directory = container.path.substr(0, slash);
    std::replace(directory.begin(), directory.end(), '/', '\\');
    entries.push_back({std::move(directory), container.path.substr(slash + 1)});
  }
  write_bytes(root / kStable / "stable.rat", rat_catalog(entries));
  content::InstallLocation location{};
  location.path = root;
  location.graphics_containers = static_cast<std::uint32_t>(install.size());
  return location;
}

fs::path cached_path(const fs::path& cache, std::string_view id) {
  return cache / content::asset_relative_path(content::make_asset_id(id).id);
}

std::map<std::string, Bytes> snapshot(const fs::path& cache) {
  std::map<std::string, Bytes> files;
  for (const fs::directory_entry& entry : fs::recursive_directory_iterator(cache)) {
    if (entry.is_regular_file()) {
      files[fs::relative(entry.path(), cache).generic_string()] = read_bytes(entry.path());
    }
  }
  return files;
}

struct Identity {
  std::string id{};
  content::AssetKind kind{};
};

// (conversion.hpp, run_conversion: Determinism, Parallel, Incremental, Write;
// Input hash) over the synthetic install: two fresh caches converted with one
// worker and with the hardware's count are byte-identical file by file,
// record included; a rerun skips every asset; then each row changes one
// input and the next run rewrites exactly the assets whose hash covers it,
// counted by the report as converted and the rest as skipped. A row that
// changes the install leaves every other file's bytes as they were and
// changes the rewritten ones; a row that damages the cache leaves the whole
// cache as it was before the damage. The rows: a base color texture's PNG
// (that texture and its material), a normal map's PNG (that texture, not the
// material it is the normal map of), a base color record's
// descriptor alone (that texture and the material it is the base color of),
// a vertex buffer byte (the mesh and its morph set, not its materials),
// the case of a texture-table name (every material, not the mesh), a cached
// file carrying another format version or not parsing (that file), a
// material whose unmapped fields file is missing or does not parse (that
// material), and each texture pipeline option (every texture). A rerun that
// skips every material reports the same unmapped fields.
void incremental_reconverts_dependents(const Arguments& arguments) {
  const ScratchTree tree(arguments.scratch, "incremental_reconverts_dependents");
  SyntheticInstall source = synthetic_install();
  const content::InstallLocation install = write_install(tree.root() / "install", source);
  const fs::path cache_a = tree.root() / "cache_a";
  const fs::path cache_b = tree.root() / "cache_b";
  const cc::ConvertResult first = convert(install, cache_a, {}, 1);
  convert(install, cache_b, {}, 0);
  const std::map<std::string, Bytes> a = snapshot(cache_a);
  require(a.size() > 1U && a == snapshot(cache_b), "byte-identical across worker counts");
  const std::uint64_t assets = first.report.converted;
  std::map<std::string, Identity> identities;
  for (const auto& [path, file] : a) {
    if (path != content::kConvertedCacheRecordFile && !is_unmapped_file(path)) {
      const content::CookedParse parse = content::parse_cooked_header(file);
      require(parse.ok(), "a cached asset parses: " + path);
      identities[path] = {std::string(content::cooked_id(file, parse.header)), parse.header.kind};
    }
  }
  require(assets == identities.size(), "every cooked file is a converted asset");

  const cc::ConvertResult rerun = convert(install, cache_a, {}, 0);
  require(rerun.report.converted == 0U && rerun.report.skipped_unchanged == assets,
          "an unchanged rerun skips every asset");
  require(std::equal(rerun.report.unmapped.begin(), rerun.report.unmapped.end(),
                     first.report.unmapped.begin(), first.report.unmapped.end(),
                     [](const cc::UnmappedCount& x, const cc::UnmappedCount& y) {
                       return x.field == y.field && x.count == y.count;
                     }),
          "the skipping rerun reports the same unmapped fields");

  const std::string material_0 = std::string(kMesh) + ".material.0";
  const std::string material_1 = std::string(kMesh) + ".material.1";
  const std::string shapes_id = std::string(kMesh) + ".shapes.0";
  const auto unmapped_of = [&cache_a](std::string_view id) {
    fs::path path = cached_path(cache_a, id);
    path += cc::kUnmappedFieldsSuffix;
    return path;
  };
  const auto rewrite_install = [&] { write_install(install.path, source); };
  cc::TexturePipelineOptions uber{};
  uber.bc7_uber_level = uber.bc7_uber_level == 0U ? 1U : uber.bc7_uber_level - 1U;
  cc::TexturePipelineOptions uber_and_green = uber;
  uber_and_green.normal_green = cc::NormalMapConvention::FlipGreen;

  struct Change {
    std::string_view name;
    std::function<void()> apply;
    std::function<bool(const Identity&)> rewrites;
    cc::TexturePipelineOptions texture;
    // The row damages the cache rather than changing the install.
    bool damages_cache{};
    // Files the change removes that the run writes again.
    std::size_t restored{};
  };
  const std::vector<Change> changes{
      {"a base color texture's PNG",
       [&] {
         source[kTextureContainer].records = {
             texture_record(kTableNames[0], fx::png_bytes(fx::kSecondPng)),
             texture_record(kTableNames[1], fx::png_bytes(fx::kPng))};
         rewrite_install();
       },
       [&](const Identity& asset) { return asset.id == kTextureA || asset.id == material_0; },
       {}},
      {"a normal map's PNG",
       [&] {
         source[kTextureContainer].records = {
             texture_record(kTableNames[0], fx::png_bytes(fx::kSecondPng)),
             texture_record(kTableNames[1], fx::png_bytes(fx::kSecondPng))};
         rewrite_install();
       },
       [&](const Identity& asset) { return asset.id == kTextureB; },
       {}},
      {"a base color record's descriptor",
       [&] {
         Bytes linear = fx::dxt1_descriptor();
         fx::put_u32(linear, 28, 0x84000002U); // sign 0 (unsigned) in place of 3 (gamma)
         source[kTextureContainer].records = {
             texture_record(kTableNames[0], fx::png_bytes(fx::kSecondPng), linear),
             texture_record(kTableNames[1], fx::png_bytes(fx::kSecondPng))};
         rewrite_install();
       },
       [&](const Identity& asset) { return asset.id == kTextureA || asset.id == material_0; },
       {}},
      {"a vertex buffer byte",
       [&] {
         fx::Record& vertices = source[kModelContainer].records[1];
         require(vertices.short_name == "vertex_000.bin", "the first vertex buffer record");
         vertices.payload[0] ^= std::byte{1};
         rewrite_install();
       },
       [&](const Identity& asset) { return asset.id == kMesh || asset.id == shapes_id; },
       {}},
      {"a texture-table name's case",
       [&] {
         std::array<std::string_view, 3> names = kTableNames;
         names[0] = "model.sx0.texture.dds.sx0_a_psd";
         source[kTableContainer].records =
             fx::texture_table({cc::kTextureCreateCall, cc::kTableSlot, cc::kDdsSlot}, names);
         rewrite_install();
       },
       [](const Identity& asset) { return asset.kind == content::AssetKind::Material; },
       {}},
      {"a cached file of another format version",
       [&] {
         Bytes file = read_bytes(cached_path(cache_a, kMesh));
         fx::put_u32(file, 4, content::kCookedFormatVersion + 1U);
         write_bytes(cached_path(cache_a, kMesh), file);
       },
       [&](const Identity& asset) { return asset.id == kMesh; },
       {},
       true},
      {"a cached file that does not parse",
       [&] {
         Bytes file = read_bytes(cached_path(cache_a, shapes_id));
         file.resize(10);
         write_bytes(cached_path(cache_a, shapes_id), file);
       },
       [&](const Identity& asset) { return asset.id == shapes_id; },
       {},
       true},
      {"a material whose unmapped fields file is missing or does not parse",
       [&] {
         fs::remove(unmapped_of(material_0));
         const std::string_view text = "no tab";
         write_bytes(unmapped_of(material_1), std::as_bytes(std::span(text)));
       },
       [&](const Identity& asset) { return asset.id == material_0 || asset.id == material_1; },
       {},
       true,
       1},
      {"the BC7 uber level", [] {},
       [](const Identity& asset) { return asset.kind == content::AssetKind::Texture; }, uber},
      {"the green convention", [] {},
       [](const Identity& asset) { return asset.kind == content::AssetKind::Texture; },
       uber_and_green},
  };
  for (std::size_t row = 0; row < changes.size(); ++row) {
    const Change& change = changes[row];
    const std::map<std::string, Bytes> undamaged = snapshot(cache_a);
    change.apply();
    const std::map<std::string, Bytes> before = snapshot(cache_a);
    const cc::ConvertResult result = convert(install, cache_a, {}, 0, change.texture);
    const std::map<std::string, Bytes> after = snapshot(cache_a);
    require(after.size() == before.size() + change.restored, row,
            "no file appears or disappears but the ones the change removed");
    std::uint64_t expected = 0;
    for (const auto& [path, identity] : identities) {
      const bool rewritten = change.rewrites(identity);
      expected += rewritten ? 1U : 0U;
      if (!change.damages_cache) {
        require((after.at(path) != before.at(path)) == rewritten, row,
                std::string(change.name) + (rewritten ? " rewrites " : " leaves untouched ") +
                    identity.id);
      }
    }
    if (change.damages_cache) {
      require(after == undamaged, row,
              std::string(change.name) + ": the cache is as it was before the damage");
    }
    require(expected > 0U, row, "the change concerns an asset");
    require(result.report.converted == expected &&
                result.report.skipped_unchanged == assets - expected,
            row, std::string(change.name) + ": the report counts the rewritten and the skipped");
  }
}

// (conversion.hpp, Ids: a texture record by its qualified name; material
// slots resolved through every record the run indexed) over the synthetic
// install, converted with no prefix: material 1's base color slot resolves to
// the texture of the shared container its table names, and that texture is
// written.
void shared_texture_resolves(const Arguments& arguments) {
  const ScratchTree tree(arguments.scratch, "shared_texture_resolves");
  const content::InstallLocation install =
      write_install(tree.root() / "install", synthetic_install());
  const fs::path cache = tree.root() / "cache";
  convert(install, cache, {}, 0);
  const Bytes file = read_bytes(cached_path(cache, std::string(kMesh) + ".material.1"));
  const content::CookedParse parse = content::parse_cooked_header(file);
  bw::materials::MaterialAssetView material;
  require(parse.ok() && bw::materials::decode_material_asset(
                            content::cooked_payload(file, parse.header), material)
                                .error == bw::materials::MaterialAssetError::None,
          "material 1 decodes");
  require(material.find_texture(bw::materials::kBaseColorTexture).id == kSharedTexture,
          "its base color is the shared texture");
  require(content::parse_cooked_header(read_bytes(cached_path(cache, kSharedTexture))).ok(),
          "the shared texture is written");
}

// (conversion.hpp, Ids: collision; Report: failed with id and reason;
// Parallel: sorted by id) over the synthetic install and two more model
// containers whose CALLs name the same mesh id: the first converts, every
// asset of each later one is a failed row naming the container that holds
// the id first, and the rows are in id order, which two jobs failing the
// same ids reach only by sorting. Alone catches a later container
// overwriting the first without a failure.
void collision_fails_second_container(const Arguments& arguments) {
  const ScratchTree tree(arguments.scratch, "collision_fails_second_container");
  SyntheticInstall containers = synthetic_install();
  containers.push_back({"graphics/chara/SX1/model/mesh.cpr", model_records("model.SX0.mesh")});
  containers.push_back({"graphics/chara/SX2/model/mesh.cpr", model_records("model.SX0.mesh")});
  const content::InstallLocation install = write_install(tree.root() / "install", containers);
  const cc::ConvertResult result =
      cc::run_conversion(options_for(tree.root() / "cache", kMesh, 0), install);
  require(result.ok(), "the run with a colliding model completes: " + result.detail);
  require(result.report.converted > 1U, "the first model's assets convert");
  require(result.report.failed.size() == 2U * result.report.converted,
          "every asset of each later model is a failed row");
  require(std::is_sorted(
              result.report.failed.begin(), result.report.failed.end(),
              [](const cc::FailedAsset& x, const cc::FailedAsset& y) { return x.id < y.id; }),
          "the failed rows are in id order");
  const std::string first = std::string(kStable) + "/" + std::string(kModelPath);
  for (const cc::FailedAsset& failed : result.report.failed) {
    require(failed.id.starts_with(kMesh) && failed.reason.find(first) != std::string::npos,
            "a failed row names the container holding the id first: " + failed.reason);
  }
}

// The model under graphics/chara/<dir>, created as model.<dir>.mesh, with its
// texture table of `names` (material 0's base color, its normal map,
// material 1's base color) and its texture container of `textures`.
void add_model(SyntheticInstall& install, const std::string& dir,
               const std::array<std::string, 3>& names, std::vector<fx::Record> textures) {
  const std::string root = "graphics/chara/" + dir;
  install.push_back({root + "/model/mesh.cpr", model_records("model." + dir + ".mesh")});
  const std::array<std::string_view, 3> table{names[0], names[1], names[2]};
  install.push_back(
      {root + "/texture/default.cpr",
       fx::texture_table({cc::kTextureCreateCall, cc::kTableSlot, cc::kDdsSlot}, table)});
  install.push_back({root + "/texture/texture.cpr", std::move(textures)});
}

// (conversion.hpp, run_conversion: Materials; Write; Report;
// material_mapping.hpp, BlendMarkerUnknown) over the synthetic install and
// three more models, one material per case. Each fails naming which, writes
// no asset and is not counted converted: a base color record that is a cube
// (SX1 material 0), one no container holds (SX1 material 1, refused when its
// slot resolves), one whose descriptor the texture plan refuses (SX2
// material 0), one whose PNG does not decode (SX2 material 1), an SRS tag
// that is no marker (SX3 material 0), and a directory where SX0 material 0's
// unmapped fields file goes. The run's other materials convert, among them
// SX3 material 1, whose base record holds half texels whose pairs of pixels
// have alpha 0 and 255: Opaque, not Masked.
void material_base_color_record_failures(const Arguments& arguments) {
  const ScratchTree tree(arguments.scratch, "material_base_color_record_failures");
  SyntheticInstall source = synthetic_install();
  const auto names = [](const std::string& dir) {
    const std::string stem = "model." + dir + ".texture.dds.";
    return std::array<std::string, 3>{stem + "a", stem + "b", stem + "c"};
  };
  const Bytes png = fx::png_bytes(fx::kPng);
  Bytes cube = fx::dxt1_descriptor();
  fx::put_u32(cube, 48, 0x0027CE00U); // dimension 3
  Bytes signed_dxt1 = fx::dxt1_descriptor();
  fx::put_u32(signed_dxt1, 28, 0x84000156U); // sign 85 on DXT1
  Bytes half = signed_dxt1;
  fx::put_u32(half, 32, 0x0025C05DU); // 16_16_16_16_EXPAND
  fx::put_u32(half, 36, 0U);          // 1 x 1, half the PNG's width
  const std::array<std::string, 3> sx1 = names("SX1");
  add_model(source, "SX1", sx1,
            {{"DDSC", "texture.dds", sx1[0], png, cube, {}, {png, png, png, png, png}},
             texture_record(sx1[1], png)});
  const std::array<std::string, 3> sx2 = names("SX2");
  add_model(source, "SX2", sx2,
            {texture_record(sx2[0], png, signed_dxt1), texture_record(sx2[1], png),
             texture_record(sx2[2], fx::text_bytes("not a png"))});
  const std::array<std::string, 3> sx3 = names("SX3");
  add_model(source, "SX3", sx3,
            {texture_record(sx3[0], png), texture_record(sx3[1], png),
             texture_record(sx3[2], fx::png_bytes(fx::kAlphaPng), half)});
  // SX3's model: material 0's SRS tag BTAD (stored last-first) becomes XXXX.
  Bytes& header = source[source.size() - 3].records[0].payload;
  const Bytes btad = fx::text_bytes("DATB");
  const auto at = std::search(header.begin(), header.end(), btad.begin(), btad.end());
  require(at != header.end(), "SX3's header holds the SRS tag");
  std::fill_n(at, 4, std::byte{'X'});
  const content::InstallLocation install = write_install(tree.root() / "install", source);
  const fs::path cache = tree.root() / "cache";
  fs::path unmapped = cached_path(cache, std::string(kMesh) + ".material.0");
  unmapped += cc::kUnmappedFieldsSuffix;
  fs::create_directories(unmapped);

  const cc::ConvertResult result = cc::run_conversion(options_for(cache, {}, 0), install);
  require(result.ok(), "the run completes: " + result.detail);
  std::map<std::string, std::string> failed;
  for (const cc::FailedAsset& f : result.report.failed) {
    failed[f.id] = f.reason;
  }
  const std::array<std::pair<std::string_view, std::string_view>, 6> cases{{
      {"model.sx1.mesh.material.0", "2D"},
      {"model.sx1.mesh.material.1", "TextureMissing"},
      {"model.sx2.mesh.material.0", "SignUnsupported"},
      {"model.sx2.mesh.material.1", "png"},
      {"model.sx3.mesh.material.0", "BlendMarkerUnknown XXXX"},
      {"model.sx0.mesh.material.0", "unmapped fields file"},
  }};
  for (std::size_t i = 0; i < cases.size(); ++i) {
    const auto found = failed.find(std::string(cases[i].first));
    require(found != failed.end() && found->second.find(cases[i].second) != std::string::npos &&
                !fs::exists(cached_path(cache, cases[i].first)),
            i,
            std::string(cases[i].first) + " fails naming " + std::string(cases[i].second) +
                " and writes no asset");
  }
  std::uint64_t written = 0;
  for (const fs::directory_entry& entry : fs::recursive_directory_iterator(cache)) {
    written += entry.is_regular_file() && !is_unmapped_file(entry.path()) &&
                       entry.path().filename() != content::kConvertedCacheRecordFile
                   ? 1U
                   : 0U;
  }
  require(result.report.converted == written, "converted counts the assets written alone");
  require(fs::exists(cached_path(cache, std::string(kMesh) + ".material.1")),
          "the run's other materials convert");
  const Bytes file = read_bytes(cached_path(cache, "model.sx3.mesh.material.1"));
  const content::CookedParse parse = content::parse_cooked_header(file);
  bw::materials::MaterialAssetView material;
  require(parse.ok() && bw::materials::decode_material_asset(
                            content::cooked_payload(file, parse.header), material)
                                .error == bw::materials::MaterialAssetError::None,
          "the half-texel material decodes");
  const auto blend = material.find_scalar(bw::materials::kBlendModeParameter);
  require(blend.found &&
              blend.value == static_cast<float>(static_cast<int>(bw::materials::BlendMode::Opaque)),
          "a half-texel base converts Opaque");
}

// (conversion.hpp, InputHasher and Input hash: the mesh's items) over the
// synthetic install: the mesh's input hash from the revision text, header.bin,
// the buffer records its VTXS, VTX and IDX chunks name in document order and
// the shapeoffset record its VTXS names equals the hash its cooked file
// carries.
void input_hash_from_items(const Arguments& arguments) {
  const ScratchTree tree(arguments.scratch, "input_hash_from_items");
  const SyntheticInstall source = synthetic_install();
  const content::InstallLocation install = write_install(tree.root() / "install", source);
  const fs::path cache = tree.root() / "cache";
  convert(install, cache, kMesh, 0);
  const std::vector<fx::Record>& records = source[kModelContainer].records;
  cc::InputHasher hasher(content::AssetKind::Mesh, kMesh);
  hasher.add_text("mesh-conversion " + std::to_string(cc::kMeshConversionRevision));
  for (const std::string_view name :
       {"header.bin", "shapevertex_000.bin", "vertex_000.bin", "index_000.bin", "vertex_001.bin",
        "index_001.bin", "shapeoffset_000.bin"}) {
    const auto record = std::find_if(records.begin(), records.end(),
                                     [name](const fx::Record& r) { return r.short_name == name; });
    require(record != records.end(), "the fixture holds " + std::string(name));
    hasher.add(record->payload);
  }
  const Bytes file = read_bytes(cached_path(cache, kMesh));
  const content::CookedParse parse = content::parse_cooked_header(file);
  require(parse.ok() && parse.header.input_hash == hasher.finish(),
          "the mesh's input hash is the hash of its items");
}

using Scenario = bw::testing::Scenario<Arguments>;
constexpr std::array kScenarios{
    Scenario{"pw0_counts", &pw0_counts},
    Scenario{"incremental_reconverts_dependents", &incremental_reconverts_dependents},
    Scenario{"shared_texture_resolves", &shared_texture_resolves},
    Scenario{"collision_fails_second_container", &collision_fails_second_container},
    Scenario{"material_base_color_record_failures", &material_base_color_record_failures},
    Scenario{"input_hash_from_items", &input_hash_from_items},
};

} // namespace

int main(int argc, char** argv) {
  return bw::testing::run_scenario(
      argc, argv, kScenarios, "<scratch directory> [<install root> <PW0 cache>]",
      [](std::span<char* const> rest, Arguments& arguments) {
        if ((rest.size() != 1U && rest.size() != 3U) || std::string_view(rest[0]).empty()) {
          return false;
        }
        arguments.scratch = rest[0];
        if (rest.size() == 3U) {
          arguments.install = rest[1];
          arguments.pw0_cache = rest[2];
        }
        return true;
      });
}
