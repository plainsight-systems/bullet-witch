// Integration test: bw/content/loader.hpp and layers.hpp's open_layers over
// layer directories on the file system. Each scenario builds its own tree
// under <scratch>/<scenario>.tmp and removes it before and after.
#include "bw/content/asset_id.hpp"
#include "bw/content/cooked_asset.hpp"
#include "bw/content/layers.hpp"
#include "bw/content/loader.hpp"
#include "bw/testing/harness.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace {

namespace fs = std::filesystem;
using bw::content::AssetId;
using bw::content::AssetKind;
using bw::content::Layer;
using bw::content::LayerError;
using bw::content::Loader;
using bw::content::LoadError;
using bw::content::LoadResult;
using bw::testing::require;

struct Arguments {
  fs::path scratch{};
};

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
  [[nodiscard]] fs::path directory(std::string_view name) const {
    const fs::path path = root_ / name;
    fs::create_directories(path);
    return path;
  }
  [[nodiscard]] const fs::path& root() const noexcept { return root_; }

private:
  fs::path root_;
};

AssetId id_of(std::string_view text) {
  const bw::content::AssetIdResult result = bw::content::make_asset_id(text);
  require(result.ok(), "fixture id validates: " + std::string(text));
  return result.id;
}

std::vector<std::byte> bytes_of(std::string_view text) {
  std::vector<std::byte> bytes(text.size());
  for (std::size_t i = 0; i < text.size(); ++i) {
    bytes[i] = static_cast<std::byte>(text[i]);
  }
  return bytes;
}

std::string text_of(std::span<const std::byte> bytes) {
  std::string text(bytes.size(), '\0');
  for (std::size_t i = 0; i < bytes.size(); ++i) {
    text[i] = std::to_integer<char>(bytes[i]);
  }
  return text;
}

// A cooked file for `id` holding `payload`, through the public encoder.
std::vector<std::byte> cooked(std::string_view id, AssetKind kind, std::string_view payload) {
  const std::vector<std::byte> body = bytes_of(payload);
  const bw::content::CookedHeaderFields fields{kind, body.size(), bw::content::Hash{},
                                               bw::content::hash_bytes(body), id};
  std::vector<std::byte> file(bw::content::cooked_payload_offset(id.size()));
  require(bw::content::encode_cooked_header(fields, file) == file.size(), "fixture encodes");
  file.insert(file.end(), body.begin(), body.end());
  return file;
}

void write_bytes(const fs::path& path, std::span<const std::byte> bytes) {
  fs::create_directories(path.parent_path());
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  out.write(reinterpret_cast<const char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
  require(out.good(), "fixture written: " + path.string());
}

void write_text(const fs::path& path, std::string_view text) { write_bytes(path, bytes_of(text)); }

// Writes `file` where the layer holds asset `id`.
void place(const fs::path& layer, std::string_view id, std::span<const std::byte> file) {
  write_bytes(layer / bw::content::asset_relative_path(id_of(id)), file);
}

void write_record(const fs::path& converted,
                  std::uint32_t version = bw::content::kCookedFormatVersion) {
  write_text(converted / bw::content::kConvertedCacheRecordFile,
             bw::content::encode_converted_cache_record({version, "/install/as/given", 6035}));
}

constexpr bw::content::LayerMask bit(Layer layer) { return bw::content::layer_bit(layer); }

// Every path under `root`, to compare a tree before and after a call.
std::set<fs::path> tree_paths(const fs::path& root) {
  std::set<fs::path> paths;
  for (const fs::directory_entry& entry : fs::recursive_directory_iterator(root)) {
    paths.insert(entry.path());
  }
  return paths;
}

// (loader.hpp, Loader::load; layers.hpp, open_layers): over three layer
// directories, an id held by an earlier layer is answered by it and a later
// layer's file for the same id is never read; each layer answers for its own
// ids; the handle's payload survives a move; a missing id names itself and
// every present layer searched, and an absent layer is not searched.
// open_layers names its first failure in LayerError order with the path, a
// record failure's line and error name, and a refused directory probe's OS
// message; it creates nothing.
void override_wins_and_missing_named(const Arguments& arguments) {
  const ScratchTree tree(arguments.scratch, "override_wins_and_missing_named");
  const fs::path overrides = tree.directory("overrides");
  const fs::path published = tree.directory("published");
  const fs::path converted = tree.directory("converted");
  write_record(converted);
  place(converted, "model.a.mesh", cooked("model.a.mesh", AssetKind::Mesh, "converted"));
  place(overrides, "model.a.mesh", cooked("model.a.mesh", AssetKind::Mesh, "override"));
  place(converted, "model.a.mesh.material.0",
        cooked("model.a.mesh.material.0", AssetKind::Material, "split"));
  place(published, "model.b.mesh", cooked("model.b.mesh", AssetKind::Mesh, "published"));

  const std::set<fs::path> before_open = tree_paths(tree.root());
  const bw::content::OpenLayersResult open =
      bw::content::open_layers(overrides, published, converted);
  require(open.ok(), "three layers open: " + open.detail);
  require(tree_paths(tree.root()) == before_open, "opening the layers creates nothing");
  require(open.record.format_version == bw::content::kCookedFormatVersion &&
              open.record.install_path == "/install/as/given" &&
              open.record.graphics_containers == 6035U,
          "the converted cache record is returned");
  require(open.layers.present == std::array{true, true, true}, "every given layer is present");
  require(open.layers.paths[static_cast<std::size_t>(Layer::ConvertedOriginals)] == converted,
          "the converted layer's path");

  const Loader loader(open.layers);
  LoadResult wins = loader.load(id_of("model.a.mesh"), AssetKind::Mesh);
  require(wins.ok(), "the overridden id loads");
  require(text_of(wins.handle.payload()) == "override", "the override answers");
  require(wins.handle.id() == id_of("model.a.mesh"), "the handle names the id asked for");
  const bw::content::AssetHandle moved = std::move(wins.handle);
  require(text_of(moved.payload()) == "override", "the payload survives a move");

  const LoadResult split = loader.load(id_of("model.a.mesh.material.0"), AssetKind::Material);
  require(split.ok() && text_of(split.handle.payload()) == "split",
          "a split asset beside its parent's file loads from the converted layer");
  const LoadResult middle = loader.load(id_of("model.b.mesh"), AssetKind::Mesh);
  require(middle.ok() && text_of(middle.handle.payload()) == "published",
          "the published layer answers for its own id");

  const LoadResult missing = loader.load(id_of("model.c.mesh"), AssetKind::Mesh);
  require(missing.failure.error == LoadError::Missing, "an id no layer holds is Missing");
  require(missing.failure.id == id_of("model.c.mesh"), "Missing names the id");
  require(missing.failure.searched == (bit(Layer::LocalOverrides) | bit(Layer::PublishedCache) |
                                       bit(Layer::ConvertedOriginals)),
          "Missing names every layer searched");

  const bw::content::OpenLayersResult two =
      bw::content::open_layers(overrides, std::nullopt, converted);
  require(two.ok() && !two.layers.present[static_cast<std::size_t>(Layer::PublishedCache)],
          "a layer not given is not present");
  const LoadResult missing_two = Loader(two.layers).load(id_of("model.b.mesh"), AssetKind::Mesh);
  require(missing_two.failure.error == LoadError::Missing &&
              missing_two.failure.searched ==
                  (bit(Layer::LocalOverrides) | bit(Layer::ConvertedOriginals)),
          "an absent layer is neither searched nor reported");

  const fs::path nowhere = tree.root() / "nowhere";
  const fs::path no_record = tree.directory("no_record");
  const fs::path record_dir = tree.directory("record_dir");
  fs::create_directories(record_dir / bw::content::kConvertedCacheRecordFile);
  const fs::path bad_record = tree.directory("bad_record");
  write_text(bad_record / bw::content::kConvertedCacheRecordFile, "format_version=1\n");
  const fs::path old_record = tree.directory("old_record");
  write_record(old_record, bw::content::kCookedFormatVersion + 1U);
  // A layer directory that links to itself: the OS will not say whether it
  // is a directory.
  const fs::path looping_layer = tree.root() / "looping_layer";
  fs::create_symlink(looping_layer.filename(), looping_layer);
  struct Row {
    std::optional<fs::path> overrides;
    std::optional<fs::path> published;
    fs::path converted;
    LayerError expected;
    fs::path named;
    std::string_view note;
    // Also in detail: a record failure's ":<line>: <error name>", or empty.
    std::string_view record_failure{};
  };
  const std::array rows{
      Row{nowhere, nowhere, nowhere, LayerError::OverridesNotADirectory, nowhere,
          "overrides first"},
      Row{std::nullopt, nowhere, nowhere, LayerError::PublishedNotADirectory, nowhere,
          "published before converted"},
      Row{overrides, published, nowhere, LayerError::ConvertedNotADirectory, nowhere, "converted"},
      Row{looping_layer, published, converted, LayerError::LayerDirectoryUnreadable, looping_layer,
          "an overrides directory the OS will not probe"},
      Row{std::nullopt, std::nullopt, no_record, LayerError::ConvertedRecordMissing, no_record,
          "a converted layer without its record"},
      Row{std::nullopt, std::nullopt, record_dir, LayerError::ConvertedRecordUnreadable, record_dir,
          "a record the OS will not read as a file"},
      Row{std::nullopt, std::nullopt, bad_record, LayerError::ConvertedRecordInvalid, bad_record,
          "a record that does not parse", ":0: MissingKey"},
      Row{std::nullopt, std::nullopt, old_record, LayerError::ConvertedRecordVersionMismatch,
          old_record, "a record of another format version", ":0: VersionMismatch"},
  };
  const std::set<fs::path> before_rows = tree_paths(tree.root());
  for (std::size_t i = 0; i < rows.size(); ++i) {
    const auto result =
        bw::content::open_layers(rows[i].overrides, rows[i].published, rows[i].converted);
    require(result.error == rows[i].expected, i, rows[i].note);
    require(result.detail.find(rows[i].named.string()) != std::string::npos, i,
            std::string(rows[i].note) + ": the path is named in " + result.detail);
    require(result.detail.find(rows[i].record_failure) != std::string::npos, i,
            std::string(rows[i].note) + ": the line and error name in " + result.detail);
    const std::string refused_prefix = rows[i].named.string() + ": ";
    require(rows[i].expected != LayerError::LayerDirectoryUnreadable ||
                (result.detail.starts_with(refused_prefix) &&
                 result.detail.size() > refused_prefix.size()),
            i, std::string(rows[i].note) + ": the path and the OS message in " + result.detail);
  }
  require(tree_paths(tree.root()) == before_rows, "a failing open_layers creates nothing");
}

// (loader.hpp, LoadError, load_error_from, LoaderOptions): a file that exists
// and fails a check is that id's failure, named as the frame check names it,
// and no later layer is tried; hash verification is on unless the caller
// turns it off; an id that does not validate searches nothing; a file the OS
// will not read, or a layer the OS will not probe, is ReadFailed with the OS
// message and no later layer answers.
void failures_named(const Arguments& arguments) {
  const ScratchTree tree(arguments.scratch, "failures_named");
  const fs::path overrides = tree.directory("overrides");
  const fs::path converted = tree.directory("converted");
  write_record(converted);

  place(converted, "good.a", cooked("good.a", AssetKind::Mesh, "payload"));
  std::vector<std::byte> flipped = cooked("hash.a", AssetKind::Mesh, "payload");
  flipped.back() ^= std::byte{1};
  place(converted, "hash.a", flipped);
  place(converted, "kind.a", cooked("kind.a", AssetKind::Texture, "payload"));
  std::vector<std::byte> version = cooked("version.a", AssetKind::Mesh, "payload");
  version[4] = std::byte{2};
  place(converted, "version.a", version);
  place(converted, "id.a", cooked("id.b", AssetKind::Mesh, "payload"));
  place(converted, "short.a", bytes_of("BWCK"));
  fs::create_directories(converted / bw::content::asset_relative_path(id_of("dir.a")));
  std::vector<std::byte> broken = cooked("shadow.a", AssetKind::Mesh, "override");
  broken[0] = std::byte{'X'};
  place(overrides, "shadow.a", broken);
  place(converted, "shadow.a", cooked("shadow.a", AssetKind::Mesh, "converted"));

  const bw::content::OpenLayersResult open =
      bw::content::open_layers(overrides, std::nullopt, converted);
  require(open.ok(), "layers open: " + open.detail);
  const Loader loader(open.layers);
  require(loader.options().verify_payload_hash, "hash verification is on by default");
  require(loader.load(id_of("good.a"), AssetKind::Mesh).ok(), "the control loads");

  struct Row {
    std::string_view id;
    LoadError expected;
    bw::content::LayerMask searched;
    std::string_view note;
  };
  const bw::content::LayerMask both = bit(Layer::LocalOverrides) | bit(Layer::ConvertedOriginals);
  const std::array rows{
      Row{"hash.a", LoadError::PayloadHashMismatch, both, "a payload byte changed"},
      Row{"kind.a", LoadError::KindMismatch, both, "a texture asked for as a mesh"},
      Row{"version.a", LoadError::VersionMismatch, both, "another format version"},
      Row{"id.a", LoadError::IdMismatch, both, "a file holding another id"},
      Row{"short.a", LoadError::TooShort, both, "a file shorter than the header"},
      Row{"dir.a", LoadError::ReadFailed, both, "a directory where the file belongs"},
      Row{"shadow.a", LoadError::BadMagic, bit(Layer::LocalOverrides),
          "a broken override is the answer; the converted file is not tried"},
  };
  for (std::size_t i = 0; i < rows.size(); ++i) {
    const LoadResult result = loader.load(id_of(rows[i].id), AssetKind::Mesh);
    require(result.failure.error == rows[i].expected, i, rows[i].note);
    require(result.failure.id == id_of(rows[i].id), i, std::string(rows[i].note) + ": the id");
    require(result.failure.searched == rows[i].searched, i,
            std::string(rows[i].note) + ": the layers searched");
  }
  const LoadResult unreadable = loader.load(id_of("dir.a"), AssetKind::Mesh);
  require(!unreadable.failure.detail.empty(), "ReadFailed carries the OS message");

  // An overrides layer under a name longer than the OS allows: the probe of
  // that layer fails, and the converted layer, which holds the file, is not
  // asked.
  bw::content::LayerDirectories unprobed = open.layers;
  unprobed.paths[static_cast<std::size_t>(Layer::LocalOverrides)] =
      tree.root() / std::string(300, 'x');
  const LoadResult refused = Loader(unprobed).load(id_of("good.a"), AssetKind::Mesh);
  require(refused.failure.error == LoadError::ReadFailed &&
              refused.failure.searched == bit(Layer::LocalOverrides) &&
              !refused.failure.detail.empty(),
          "a layer the OS will not probe is ReadFailed with the message, not skipped");

  const Loader unverified(open.layers, bw::content::LoaderOptions{.verify_payload_hash = false});
  const LoadResult named_choice = unverified.load(id_of("hash.a"), AssetKind::Mesh);
  require(named_choice.ok(), "a caller that turns verification off gets the bytes");
  require(unverified.load(id_of("kind.a"), AssetKind::Mesh).failure.error ==
              LoadError::KindMismatch,
          "turning verification off leaves the other checks");

  const LoadResult invalid = loader.load(AssetId{}, AssetKind::Mesh);
  require(invalid.failure.error == LoadError::InvalidId && invalid.failure.searched == 0U,
          "an id that does not validate searches no layer");
}

using Scenario = bw::testing::Scenario<Arguments>;
constexpr std::array kScenarios{
    Scenario{"override_wins_and_missing_named", &override_wins_and_missing_named},
    Scenario{"failures_named", &failures_named},
};

} // namespace

int main(int argc, char** argv) {
  return bw::testing::run_scenario(argc, argv, kScenarios, "<scratch directory>",
                                   [](std::span<char* const> rest, Arguments& arguments) {
                                     if (rest.size() != 1U) {
                                       return false;
                                     }
                                     arguments.scratch = rest[0];
                                     return true;
                                   });
}
