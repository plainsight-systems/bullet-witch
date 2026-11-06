#include "bw/content_converter/conversion.hpp"

#include "bw/animation/skeleton_asset.hpp"
#include "bw/content/asset_id.hpp"
#include "bw/content/cooked_asset.hpp"
#include "bw/content/install.hpp"
#include "bw/content/layers.hpp"
#include "bw/content_converter/asset_data.hpp"
#include "bw/content_converter/cpr_container.hpp"
#include "bw/content_converter/material_mapping.hpp"
#include "bw/content_converter/mesh_reader.hpp"
#include "bw/content_converter/rat_index.hpp"
#include "bw/content_converter/skeleton_reader.hpp"
#include "bw/content_converter/texture_reader.hpp"

#include "bytes.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cerrno>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

// Responsibility: the out-of-line part of bw/content_converter/conversion.hpp:
// the command line parse, the plan, the input hash, the report text and the
// run over an install.
//
// Contract (the header's), with the readings it leaves open:
//   Command line: arguments[0] is the command; options are read left to
//   right, each followed by its value (the next argument, whatever it
//   holds). The first failure is returned: an option outside the six is
//   UnknownOption, a second use of one is DuplicateOption, an option with
//   no argument after it is MissingValue, then the value's own check
//   (--threads a decimal u32, --bc7-uber a decimal 0..4: BadNumber;
//   --normal-green as-stored or flip: BadNormalGreen). OutputMissing is
//   reported after the scan when --out never appeared. `argument` views the
//   offending argument (the command, the option or the value), empty for
//   OutputMissing and for no arguments at all.
//   Plan: one job per container, in the given order, so the plan's order is
//   the sorted container order. Kinds by the last two path components:
//   ".../model/mesh.cpr" is Model; ".../texture/texture.cpr" and any ".cpr"
//   directly inside a directory named shared_texture or cubemap is Textures;
//   everything else is Other. The run's index pass reads every Other
//   container and treats one that holds DDS or DDSC records as a Textures
//   job.
//   Input hash framing as the header states it; the length before every
//   item means moving bytes across the boundary of two items changes the
//   hash. add and add_text frame alike: text is its bytes.
//   Material inputs: "the material's block bytes" are the MaterialBlock the
//   mesh reader read, written field by field in declaration order as
//   little-endian words (floats as their bit patterns, flags as one byte):
//   the block is parsed from several chunks (the material's container and
//   the nearest preceding knsS), and these are exactly the values the
//   mapping reads. Then every name of the texture table, in order.
//   Mesh inputs: header.bin, then every buffer record a VTX, VTXS or IDX
//   chunk names, in document order of the chunks, then motion.bil and
//   motion._bl when present, then each shapeoffset record a VTXS names that
//   exists, in document order. A morph set's input is its shapeoffset
//   record (the s-th of that list). A texture's inputs are the green
//   convention as its command-line spelling ("as-stored" or "flip") and the
//   BC7 uber level in decimal, for every texture whatever its format, then
//   the descriptor (the record's slot bytes), then its PNG (DDS) or its six
//   PNGs in content order (DDSC).
//   --only: an asset is written only when its id starts with the prefix.
//   An asset outside the prefix appears in no count of the report. A
//   failure before an id is known (a container that does not read or
//   parse, a record whose id does not derive) is reported whatever the
//   prefix, because the container might hold assets inside it.
//   Skipping: the existing file is read whole; it is skipped when it
//   parses, names the same id and kind (check_expectations) and carries
//   the same input hash; its payload hash is not checked.
//   The skeleton's bind block is B, as the header states: over PW0, posing
//   clip frames under each block, block B gives the smaller change in
//   length of edges between bones on 4 of 4 chosen frames and 121 of 121
//   frames of one clip. The reader leaves it Unknown and this run fills it.
//   A mesh names its skeleton id whenever the container holds either
//   skeleton record, even when the skeleton fails to read: the skeleton is
//   then a failed row and a load of the id reports it missing, rather than
//   the mesh hiding that it binds to one.
//   The model's texture table is the sibling texture/default.cpr. When that
//   file does not exist the table is empty (17 shipped meshes have none and
//   only empty TR blocks); when it exists and does not read, parse or yield
//   a table, every material with a slot fails with that reason.
//   Id collisions: the index pass derives every texture record's id and
//   every mesh id. A second asset with an id already taken, or a texture id
//   that extends a mesh id (where the mesh's split ids live), fails by name
//   with the container that holds the first; the first is kept. A model
//   whose mesh id collides still has its job run: every asset of it the
//   prefix admits (the mesh, each material, the skeleton, each morph set)
//   is a failed row with the collision, so a prefix naming a split id
//   still sees the loser's rows; nothing of it is written or counted.
//   The probe file that tests the output directory is written and removed
//   before the run; a removal the OS refuses is OutputNotWritable. The
//   cache record is removed before any job runs and written last, so an
//   interrupted run never leaves a record claiming a finished conversion.
//   The report is filled only once the jobs ran: an error returned before
//   them carries the empty report, and the one after them (the record's
//   write) carries the whole report.
//   bytes_written counts every file written, the record included.
//   Unmapped counts are gathered from every model asset the prefix admits,
//   converted or skipped alike (a material that fails adds none), so the
//   report of an unchanged rerun names the same fields: material fields as
//   "material.<name>",
//   chunks the mesh reader did not read as "mesh_chunk.<tag>", record tags
//   outside the eight known ones as "record_tag.<tag>" (bytes outside
//   printable ASCII as \xNN).
//   Report text: one line per item, "<label>: <value>"; failures as
//   "failed: <id>: <reason>" and unmapped fields as "unmapped: <field>:
//   <count>", in the report's order; seconds with three decimals.
namespace bw::content_converter {

namespace {

namespace fs = std::filesystem;
using Tag = std::array<char, 4>;

constexpr std::string_view kCommand = "convert";
constexpr std::string_view kInstallOption = "--install";
constexpr std::string_view kOutOption = "--out";
constexpr std::string_view kOnlyOption = "--only";
constexpr std::string_view kThreadsOption = "--threads";
constexpr std::string_view kNormalGreenOption = "--normal-green";
constexpr std::string_view kUberOption = "--bc7-uber";
constexpr std::uint32_t kMaxUberLevel = 4;
constexpr std::string_view kAsStored = "as-stored";
constexpr std::string_view kFlipGreen = "flip";

constexpr std::string_view kMeshCreateCall = "vfx2.mesh.create";
constexpr std::string_view kHeaderRecord = "header.bin";
constexpr std::string_view kBilRecord = "motion.bil";
constexpr std::string_view kBlRecord = "motion._bl";
constexpr std::string_view kRatExtension = ".rat";
constexpr std::string_view kTemporarySuffix = ".tmp";
constexpr std::size_t kCubeFaces = 6;
constexpr std::size_t kCubeTableBytes = 48;

constexpr Tag kVtxTag{'V', 'T', 'X', ' '};
constexpr Tag kVtxsTag{'V', 'T', 'X', 'S'};
constexpr Tag kIdxTag{'I', 'D', 'X', ' '};

// ---- names ----------------------------------------------------------------

template <typename Enum, std::size_t N>
std::string_view enum_name(Enum value, const std::array<std::string_view, N>& names) noexcept {
  const auto index = static_cast<std::size_t>(value);
  return index < N ? names[index] : std::string_view("Unknown");
}

std::string_view name_of(CprError e) noexcept {
  static constexpr std::array<std::string_view, 12> kNames{"None",
                                                           "TooShort",
                                                           "BadMagic",
                                                           "UnsupportedVersion",
                                                           "LayoutInconsistent",
                                                           "NameOutOfRange",
                                                           "NameUnterminated",
                                                           "PayloadOutOfRange",
                                                           "ArgumentRangeOutOfRange",
                                                           "ArgumentValueOutOfRange",
                                                           "SlotOutOfRange",
                                                           "TailOutOfRange"};
  return enum_name(e, kNames);
}
std::string_view name_of(RatError e) noexcept {
  static constexpr std::array<std::string_view, 8> kNames{"None",
                                                          "TooShort",
                                                          "BadMagic",
                                                          "SizeMismatch",
                                                          "SectionOutOfRange",
                                                          "FileTableMisaligned",
                                                          "StringOutOfRange",
                                                          "StringUnterminated"};
  return enum_name(e, kNames);
}
std::string_view name_of(MeshReadError e) noexcept {
  static constexpr std::array<std::string_view, 16> kNames{"None",
                                                           "HeaderRecordMissing",
                                                           "BadModelMagic",
                                                           "ChunkOverrun",
                                                           "DeclarationUnsupported",
                                                           "LayoutInvalid",
                                                           "BufferMissing",
                                                           "BufferSizeNotMultipleOfStride",
                                                           "IndexBufferSizeInvalid",
                                                           "DrawOutsideGroup",
                                                           "DrawRangeInvalid",
                                                           "PrimitiveTypeUnsupported",
                                                           "BoneMapMalformed",
                                                           "MaterialBlockMalformed",
                                                           "MorphSetMalformed",
                                                           "MorphSetGroupMissing"};
  return enum_name(e, kNames);
}
std::string_view name_of(SkeletonReadError e) noexcept {
  static constexpr std::array<std::string_view, 10> kNames{
      "None",          "NoSkeleton",    "RecordPairIncomplete", "BadMagic",         "SizeMismatch",
      "CountMismatch", "IndexMismatch", "ParentInvalid",        "RootCountInvalid", "NameInvalid"};
  return enum_name(e, kNames);
}
std::string_view name_of(TextureTableError e) noexcept {
  static constexpr std::array<std::string_view, 3> kNames{"None", "CallMissing",
                                                          "ArgumentSlotUnexpected"};
  return enum_name(e, kNames);
}
std::string_view name_of(SlotError e) noexcept {
  static constexpr std::array<std::string_view, 4> kNames{"None", "IndexOutOfRange",
                                                          "TextureMissing", "IdInvalid"};
  return enum_name(e, kNames);
}
std::string_view name_of(content::AssetIdError e) noexcept {
  static constexpr std::array<std::string_view, 6> kNames{
      "None", "Empty", "TooLong", "EmptySegment", "SegmentTooLong", "InvalidCharacter"};
  return enum_name(e, kNames);
}
std::string_view name_of(DescriptorError e) noexcept {
  static constexpr std::array<std::string_view, 2> kNames{"None", "DescriptorSizeInvalid"};
  return enum_name(e, kNames);
}
std::string_view name_of(PlanError e) noexcept {
  static constexpr std::array<std::string_view, 6> kNames{
      "None",          "FormatUnsupported", "SignUnsupported", "HalfWidthMismatch",
      "ExtentInvalid", "ImageCountInvalid"};
  return enum_name(e, kNames);
}
std::string_view name_of(EncodeError e) noexcept {
  static constexpr std::array<std::string_view, 3> kNames{"None", "FormatNotBlockCompressed",
                                                          "UberLevelOutOfRange"};
  return enum_name(e, kNames);
}

std::string tag_text(const Tag& tag) {
  static constexpr std::string_view kHex = "0123456789abcdef";
  std::string text;
  for (const char c : tag) {
    const auto byte = static_cast<unsigned char>(c);
    if (byte >= 0x20U && byte < 0x7FU) {
      text += c;
    } else {
      text += "\\x";
      text += kHex[byte >> 4U];
      text += kHex[byte & 0xFU];
    }
  }
  return text;
}

std::string decimal(std::uint64_t value) { return std::to_string(value); }

// The command line's spelling of the convention, the text a texture's hash
// carries.
std::string_view normal_green_text(NormalMapConvention convention) noexcept {
  return convention == NormalMapConvention::FlipGreen ? kFlipGreen : kAsStored;
}

// ---- small parsing --------------------------------------------------------

std::optional<std::uint32_t> parse_u32(std::string_view text) noexcept {
  if (text.empty()) {
    return std::nullopt;
  }
  std::uint32_t value = 0;
  const char* const begin = text.data();
  const char* const end = begin + text.size();
  const auto [ptr, ec] = std::from_chars(begin, end, value);
  if (ec != std::errc() || ptr != end) {
    return std::nullopt;
  }
  return value;
}

std::string lower_ascii(std::string_view text) {
  std::string out(text);
  for (char& c : out) {
    if (c >= 'A' && c <= 'Z') {
      c = static_cast<char>(c - 'A' + 'a');
    }
  }
  return out;
}

// ---- files ----------------------------------------------------------------

struct FileBytes {
  std::vector<std::byte> bytes{};
  // The OS message when the read failed; empty on success.
  std::string error{};
};

std::string errno_text() { return std::generic_category().message(errno); }

FileBytes read_file(const fs::path& path) {
  FileBytes result;
  const std::unique_ptr<std::FILE, int (*)(std::FILE*)> file(std::fopen(path.c_str(), "rb"),
                                                             &std::fclose);
  if (!file) {
    result.error = errno_text();
    return result;
  }
  std::array<std::byte, 1U << 16U> chunk{};
  while (std::feof(file.get()) == 0 && std::ferror(file.get()) == 0) {
    const std::size_t read = std::fread(chunk.data(), 1, chunk.size(), file.get());
    result.bytes.insert(result.bytes.end(), chunk.begin(),
                        chunk.begin() + static_cast<std::ptrdiff_t>(read));
  }
  if (std::ferror(file.get()) != 0) {
    result.error = errno_text();
    result.bytes.clear();
  }
  return result;
}

// Creates the parent directories, writes "<path>.tmp" whole and renames it
// over `path`; on any failure the temporary is removed and the OS message
// returned. Empty on success.
std::string write_file_atomically(const fs::path& path, std::span<const std::byte> bytes) {
  std::error_code ec;
  fs::create_directories(path.parent_path(), ec);
  if (ec) {
    // Another worker may have created it between the probe and the call.
    std::error_code probe;
    if (!fs::is_directory(path.parent_path(), probe)) {
      return "create directory " + path.parent_path().generic_string() + ": " + ec.message();
    }
  }
  fs::path temporary = path;
  temporary += kTemporarySuffix;
  std::string error;
  {
    const std::unique_ptr<std::FILE, int (*)(std::FILE*)> file(std::fopen(temporary.c_str(), "wb"),
                                                               &std::fclose);
    if (!file) {
      return "open " + temporary.generic_string() + ": " + errno_text();
    }
    if (!bytes.empty() && std::fwrite(bytes.data(), 1, bytes.size(), file.get()) != bytes.size()) {
      error = "write " + temporary.generic_string() + ": " + errno_text();
    }
    if (error.empty() && std::fflush(file.get()) != 0) {
      error = "flush " + temporary.generic_string() + ": " + errno_text();
    }
  }
  if (error.empty()) {
    fs::rename(temporary, path, ec);
    if (ec) {
      error = "rename " + temporary.generic_string() + ": " + ec.message();
    }
  }
  if (!error.empty()) {
    std::error_code ignored;
    fs::remove(temporary, ignored);
  }
  return error;
}

// ---- plan -----------------------------------------------------------------

// The last two '/'-separated components of a relative path.
struct PathTail {
  std::string_view directory{};
  std::string_view file{};
};
PathTail path_tail(std::string_view path) {
  PathTail tail;
  const std::size_t slash = path.rfind('/');
  if (slash == std::string_view::npos) {
    tail.file = path;
    return tail;
  }
  tail.file = path.substr(slash + 1);
  const std::string_view rest = path.substr(0, slash);
  const std::size_t previous = rest.rfind('/');
  tail.directory = previous == std::string_view::npos ? rest : rest.substr(previous + 1);
  return tail;
}

JobKind kind_by_name(std::string_view container) {
  const PathTail tail = path_tail(container);
  if (tail.directory == "model" && tail.file == "mesh.cpr") {
    return JobKind::Model;
  }
  if (tail.directory == "texture" && tail.file == "texture.cpr") {
    return JobKind::Textures;
  }
  if ((tail.directory == "shared_texture" || tail.directory == "cubemap") &&
      tail.file.ends_with(".cpr") && tail.file.size() > 4U) {
    return JobKind::Textures;
  }
  return JobKind::Other;
}

// ---- catalog --------------------------------------------------------------

struct Catalog {
  std::vector<std::string> containers{};
  ConvertError error{ConvertError::None};
  std::string detail{};
};

Catalog read_catalog(const fs::path& install) {
  Catalog catalog;
  const fs::path data = install / "data";
  std::vector<fs::path> rats;
  std::error_code ec;
  fs::recursive_directory_iterator it(data, ec);
  const fs::recursive_directory_iterator end;
  while (!ec && it != end) {
    std::error_code type_ec;
    if (it->is_regular_file(type_ec) && !type_ec &&
        it->path().extension().generic_string() == kRatExtension) {
      rats.push_back(it->path());
    }
    if (type_ec) {
      ec = type_ec;
      break;
    }
    it.increment(ec);
  }
  if (ec) {
    catalog.error = ConvertError::CatalogUnreadable;
    catalog.detail = "listing " + data.generic_string() + ": " + ec.message();
    return catalog;
  }
  if (rats.empty()) {
    catalog.error = ConvertError::CatalogUnreadable;
    catalog.detail = "no .rat catalog under " + data.generic_string();
    return catalog;
  }
  std::sort(rats.begin(), rats.end(), [](const fs::path& a, const fs::path& b) {
    return a.generic_string() < b.generic_string();
  });
  for (const fs::path& rat : rats) {
    const FileBytes file = read_file(rat);
    if (!file.error.empty()) {
      catalog.error = ConvertError::CatalogUnreadable;
      catalog.detail = rat.generic_string() + ": " + file.error;
      return catalog;
    }
    RatIndex index;
    const RatError rat_error = parse_rat(file.bytes, index);
    if (rat_error != RatError::None) {
      catalog.error = ConvertError::CatalogUnreadable;
      catalog.detail = rat.generic_string() + ": " + std::string(name_of(rat_error));
      return catalog;
    }
    const std::string directory = rat.parent_path().lexically_relative(install).generic_string();
    for (const RatEntry& entry : index.files()) {
      catalog.containers.push_back(directory + "/" + relative_path(entry));
    }
  }
  std::sort(catalog.containers.begin(), catalog.containers.end());
  catalog.containers.erase(std::unique(catalog.containers.begin(), catalog.containers.end()),
                           catalog.containers.end());
  return catalog;
}

// ---- index pass -----------------------------------------------------------

struct TextureEntry {
  std::uint32_t record{};
  std::string id{};
};

struct IndexedContainer {
  JobKind kind{JobKind::Other};
  // A read or parse failure; empty when the container indexed.
  std::string error{};
  std::string mesh_id{};
  // Set when an earlier container holds mesh_id: the reason every asset of
  // this model fails with.
  std::string collision{};
  std::vector<TextureEntry> textures{};
  // Lower-cased qualified names of the DDS and DDSC records.
  std::vector<std::string> texture_names{};
  std::vector<FailedAsset> failed{};
};

bool is_texture_tag(RecordTag tag) noexcept {
  return tag == RecordTag::Dds || tag == RecordTag::Ddsc;
}

const CprRecord* mesh_create_call(const CprContainer& container) noexcept {
  for (const CprRecord& record : container.records()) {
    if (record.tag == RecordTag::Call && record.short_name == kMeshCreateCall &&
        !record.qualified_name.empty()) {
      return &record;
    }
  }
  return nullptr;
}

IndexedContainer index_container(const fs::path& install, const ConversionJob& job) {
  IndexedContainer out;
  out.kind = job.kind;
  const FileBytes file = read_file(install / job.container);
  if (!file.error.empty()) {
    out.error = "container unreadable: " + file.error;
    return out;
  }
  CprContainer container;
  const CprError cpr_error = parse_cpr(file.bytes, container);
  if (cpr_error != CprError::None) {
    out.error = "container does not parse: " + std::string(name_of(cpr_error));
    return out;
  }
  const auto& records = container.records();
  for (std::size_t r = 0; r < records.size(); ++r) {
    const CprRecord& record = records[r];
    if (!is_texture_tag(record.tag)) {
      continue;
    }
    if (out.kind == JobKind::Other) {
      out.kind = JobKind::Textures;
    }
    if (out.kind != JobKind::Textures) {
      continue;
    }
    content::AssetIdResult id =
        record.qualified_name.empty()
            ? content::asset_id_from_container_record(job.container, record.short_name)
            : content::asset_id_from_qualified_name(record.qualified_name);
    // The name is resolvable whether or not its id validates, so a slot
    // naming it fails with the id error rather than as missing.
    if (!record.qualified_name.empty()) {
      out.texture_names.push_back(lower_ascii(record.qualified_name));
    }
    if (!id.ok()) {
      out.failed.push_back({job.container + ":" + std::string(record.short_name),
                            "texture id: " + std::string(name_of(id.error))});
      continue;
    }
    out.textures.push_back({static_cast<std::uint32_t>(r), std::string(id.id.view())});
  }
  if (out.kind == JobKind::Model) {
    const CprRecord* call = mesh_create_call(container);
    if (call == nullptr) {
      out.failed.push_back(
          {job.container, "mesh id: no vfx2.mesh.create CALL with a qualified name"});
    } else {
      content::AssetIdResult id = content::asset_id_from_qualified_name(call->qualified_name);
      if (!id.ok()) {
        out.failed.push_back({job.container + ":" + std::string(call->qualified_name),
                              "mesh id: " + std::string(name_of(id.error))});
      } else {
        out.mesh_id = std::string(id.id.view());
      }
    }
  }
  return out;
}

// Every lower-cased qualified name of a texture record the index saw.
class IndexedResolver final : public RecordResolver {
public:
  explicit IndexedResolver(std::vector<std::string> names) : names_(std::move(names)) {
    std::sort(names_.begin(), names_.end());
    names_.erase(std::unique(names_.begin(), names_.end()), names_.end());
  }
  [[nodiscard]] bool exists(std::string_view qualified_name) const override {
    return std::binary_search(names_.begin(), names_.end(), lower_ascii(qualified_name));
  }

private:
  std::vector<std::string> names_;
};

// ---- jobs -----------------------------------------------------------------

struct JobResult {
  std::uint64_t converted{};
  std::uint64_t skipped_unchanged{};
  std::uint64_t bytes_written{};
  std::vector<FailedAsset> failed{};
  std::map<std::string, std::uint64_t> unmapped{};
};

struct JobContext {
  const fs::path* install{};
  const ConvertOptions* options{};
  const IndexedResolver* resolver{};
};

bool wanted(std::string_view id, const ConvertOptions& options) noexcept {
  return id.starts_with(options.only_prefix);
}

bool unchanged(const fs::path& path, const content::AssetId& id, content::AssetKind kind,
               const content::Hash& input_hash) {
  std::error_code ec;
  if (!fs::is_regular_file(path, ec)) {
    return false;
  }
  const FileBytes existing = read_file(path);
  if (!existing.error.empty()) {
    return false;
  }
  const content::CookedParse parse = content::parse_cooked_header(existing.bytes);
  return parse.ok() &&
         content::check_expectations(existing.bytes, parse.header, id.view(), kind) ==
             content::CookedError::None &&
         parse.header.input_hash == input_hash;
}

using Encoder = std::function<std::vector<std::byte>(std::string& failure)>;

// One asset: skipped when unchanged, else encoded and written atomically.
void emit(const JobContext& context, JobResult& result, content::AssetKind kind,
          const content::AssetId& id, const content::Hash& input_hash, const Encoder& encode) {
  const fs::path path = context.options->output / content::asset_relative_path(id);
  if (unchanged(path, id, kind, input_hash)) {
    ++result.skipped_unchanged;
    return;
  }
  std::string failure;
  const std::vector<std::byte> payload = encode(failure);
  if (!failure.empty()) {
    result.failed.push_back({std::string(id.view()), failure});
    return;
  }
  const std::vector<std::byte> file = encode_cooked_asset(kind, id, input_hash, payload);
  const std::string error = write_file_atomically(path, file);
  if (!error.empty()) {
    result.failed.push_back({std::string(id.view()), error});
    return;
  }
  ++result.converted;
  result.bytes_written += file.size();
}

std::string encode_failure_text(std::string_view layout, const EncodeFailure& failure) {
  return std::string(layout) + " payload rejected by its decoder: error " + decimal(failure.error) +
         ", index " + decimal(failure.index);
}

void count_unknown_tags(const CprContainer& container, JobResult& result) {
  for (const CprRecord& record : container.records()) {
    if (record.tag == RecordTag::Unknown) {
      ++result.unmapped["record_tag." + tag_text(record.tag_bytes)];
    }
  }
}

// ---- textures -------------------------------------------------------------

struct CubeFaces {
  std::array<std::span<const std::byte>, kCubeFaces> faces{};
  bool ok{};
};
CubeFaces split_cube_payload(std::span<const std::byte> payload) noexcept {
  CubeFaces out;
  if (payload.size() < kCubeTableBytes) {
    return out;
  }
  for (std::size_t f = 0; f < kCubeFaces; ++f) {
    const std::uint32_t offset = bytes::u32(payload, 4U * f);
    const std::uint32_t size = bytes::u32(payload, (4U * kCubeFaces) + (4U * f));
    if (!bytes::within(offset, size, payload.size())) {
      return out;
    }
    out.faces[f] = payload.subspan(offset, size);
  }
  out.ok = true;
  return out;
}

std::string texture_failure_text(const TextureConvertFailure& failure) {
  std::string text = "texture";
  if (failure.descriptor != DescriptorError::None) {
    text += " descriptor: " + std::string(name_of(failure.descriptor));
  } else if (!failure.png.ok()) {
    text += " png: code " + decimal(failure.png.code) + " " + std::string(failure.png.text);
  } else if (failure.plan != PlanError::None) {
    text += " plan: " + std::string(name_of(failure.plan));
  } else {
    text += " encode: " + std::string(name_of(failure.encode));
  }
  return text + ", face " + decimal(failure.face);
}

void run_textures_job(const JobContext& context, const IndexedContainer& index,
                      const CprContainer& container, JobResult& result) {
  const auto& records = container.records();
  bool any_wanted = false;
  for (const TextureEntry& entry : index.textures) {
    if (!wanted(entry.id, *context.options)) {
      continue;
    }
    any_wanted = true;
    const CprRecord& record = records[entry.record];
    content::AssetIdResult id = content::make_asset_id(entry.id);
    std::vector<std::span<const std::byte>> pngs;
    if (record.tag == RecordTag::Ddsc) {
      const CubeFaces cube = split_cube_payload(record.payload);
      if (!cube.ok) {
        result.failed.push_back({entry.id, "cube payload: face table outside the record"});
        continue;
      }
      pngs.assign(cube.faces.begin(), cube.faces.end());
    } else {
      pngs.push_back(record.payload);
    }
    InputHasher hasher(content::AssetKind::Texture, entry.id);
    hasher.add_text(normal_green_text(context.options->texture.normal_green));
    hasher.add_text(decimal(context.options->texture.bc7_uber_level));
    hasher.add(record.slot);
    for (const std::span<const std::byte> png : pngs) {
      hasher.add(png);
    }
    emit(context, result, content::AssetKind::Texture, id.id, hasher.finish(),
         [&](std::string& failure) {
           TextureAssetData data;
           const TextureConvertFailure converted =
               convert_texture(record.slot, pngs, context.options->texture, data);
           if (!converted.ok()) {
             failure = texture_failure_text(converted);
             return std::vector<std::byte>{};
           }
           EncodeFailure encode{};
           std::vector<std::byte> payload = encode_texture_asset(data, encode);
           if (!encode.ok()) {
             failure = encode_failure_text("texture", encode);
           }
           return payload;
         });
  }
  if (any_wanted) {
    count_unknown_tags(container, result);
  }
}

// ---- models ---------------------------------------------------------------

std::string buffer_record_name(std::string_view stem, std::uint32_t number) {
  std::string digits = decimal(number);
  if (digits.size() < 3U) {
    digits.insert(0, 3U - digits.size(), '0');
  }
  return std::string(stem) + "_" + digits + ".bin";
}

const CprRecord* find_tagged(const CprContainer& container, std::string_view name,
                             RecordTag tag) noexcept {
  const CprRecord* record = container.find_record(name);
  return record != nullptr && record->tag == tag ? record : nullptr;
}

// The records a model's hashes name: the buffers its VTX, VTXS and IDX
// chunks name in document order, and the shapeoffset records its VTXS
// chunks name that exist, in the same order.
struct ModelRecords {
  std::vector<const CprRecord*> buffers{};
  std::vector<const CprRecord*> shape_offsets{};
  std::string error{};
};
ModelRecords model_records(const CprContainer& container, std::span<const std::byte> header) {
  ModelRecords out;
  std::vector<Chunk> chunks;
  const MeshReadFailure walk = walk_chunks(header, chunks);
  if (!walk.ok()) {
    out.error = "mesh chunk walk: " + std::string(name_of(walk.error));
    return out;
  }
  for (const Chunk& chunk : chunks) {
    const bool vtx = chunk.tag == kVtxTag;
    const bool vtxs = chunk.tag == kVtxsTag;
    const bool idx = chunk.tag == kIdxTag;
    if (!vtx && !vtxs && !idx) {
      continue;
    }
    const std::size_t number_at = idx ? 0U : 4U;
    if (chunk.data.size() < number_at + 4U) {
      out.error = "mesh hash: buffer chunk " + tag_text(chunk.tag) + " too short at " +
                  decimal(chunk.offset);
      return out;
    }
    const std::uint32_t number = bytes::u32(chunk.data, number_at);
    const CprRecord* record =
        idx ? find_tagged(container, buffer_record_name("index", number), RecordTag::Dxib)
            : find_tagged(container, buffer_record_name(vtx ? "vertex" : "shapevertex", number),
                          RecordTag::Dxvb);
    if (record == nullptr) {
      out.error = "mesh hash: buffer record of " + tag_text(chunk.tag) + " at " +
                  decimal(chunk.offset) + " missing";
      return out;
    }
    out.buffers.push_back(record);
    if (vtxs) {
      const CprRecord* shape =
          find_tagged(container, buffer_record_name("shapeoffset", number), RecordTag::User);
      if (shape != nullptr) {
        out.shape_offsets.push_back(shape);
      }
    }
  }
  return out;
}

void append_u32(std::vector<std::byte>& out, std::uint32_t value) {
  for (std::size_t i = 0; i < 4; ++i) {
    out.push_back(static_cast<std::byte>((value >> (8U * i)) & 0xFFU));
  }
}

// The material block as the reader read it, field by field.
std::vector<std::byte> material_block_bytes(const MaterialBlock& block) {
  std::vector<std::byte> out;
  const auto append_tag = [&out](const Tag& tag) {
    for (const char c : tag) {
      out.push_back(static_cast<std::byte>(c));
    }
  };
  append_u32(out, block.knss_feature);
  append_u32(out, block.knss_second);
  for (const Cf1iRecord& record : block.cf1i) {
    append_tag(record.tag);
    for (const float value : record.value) {
      append_u32(out, std::bit_cast<std::uint32_t>(value));
    }
  }
  append_u32(out, static_cast<std::uint32_t>(block.slots.size()));
  for (const TrSlot& slot : block.slots) {
    append_tag(slot.tag);
    append_u32(out, slot.index);
  }
  out.push_back(static_cast<std::byte>(block.has_sssm ? 1U : 0U));
  for (const std::uint32_t word : block.sssm_lenv) {
    append_u32(out, word);
  }
  out.push_back(static_cast<std::byte>(block.has_srs ? 1U : 0U));
  append_tag(block.srs_tag);
  for (const std::uint32_t word : block.srs_value) {
    append_u32(out, word);
  }
  return out;
}

// The sibling texture table of a model container, or the reason it could
// not be read; an absent file is an empty table.
struct ModelTable {
  std::vector<std::byte> bytes{};
  CprContainer container{};
  TextureTable table{};
  std::string error{};
};
void read_model_table(const fs::path& install, std::string_view model_container, ModelTable& out) {
  const fs::path default_path =
      (install / model_container).parent_path().parent_path() / "texture" / "default.cpr";
  std::error_code ec;
  const bool exists = fs::exists(default_path, ec);
  if (ec) {
    out.error = "texture table: probe " + default_path.generic_string() + ": " + ec.message();
    return;
  }
  if (!exists) {
    return;
  }
  FileBytes file = read_file(default_path);
  if (!file.error.empty()) {
    out.error = "texture table unreadable: " + file.error;
    return;
  }
  out.bytes = std::move(file.bytes);
  const CprError cpr_error = parse_cpr(out.bytes, out.container);
  if (cpr_error != CprError::None) {
    out.error = "texture table does not parse: " + std::string(name_of(cpr_error));
    return;
  }
  const TextureTableError table_error = texture_table_from_default(out.container, out.table);
  if (table_error != TextureTableError::None) {
    out.error = "texture table: " + std::string(name_of(table_error));
  }
}

content::AssetId split_id(const content::AssetId& mesh, content::SplitKind kind,
                          std::uint32_t index) {
  return content::split_asset_id(mesh, kind, index).id;
}

// A model whose mesh id an earlier container holds: every asset of it the
// prefix admits is a failed row with the collision, and nothing is written.
void fail_collided_model(const ConvertOptions& options, const content::AssetId& mesh_id,
                         const CprContainer& container, const MeshSource& source,
                         const std::string& reason, JobResult& result) {
  std::vector<content::AssetId> ids{mesh_id};
  for (std::size_t k = 0; k < source.materials.size(); ++k) {
    ids.push_back(split_id(mesh_id, content::SplitKind::Material, static_cast<std::uint32_t>(k)));
  }
  SkeletonAssetData skeleton;
  if (read_skeleton(container, skeleton).error != SkeletonReadError::NoSkeleton) {
    ids.push_back(split_id(mesh_id, content::SplitKind::Skeleton, 0));
  }
  for (std::size_t s = 0; s < source.morph_sets.size(); ++s) {
    ids.push_back(split_id(mesh_id, content::SplitKind::Shapes, static_cast<std::uint32_t>(s)));
  }
  for (const content::AssetId& id : ids) {
    if (wanted(id.view(), options)) {
      result.failed.push_back({std::string(id.view()), reason});
    }
  }
}

void run_model_job(const JobContext& context, const ConversionJob& job,
                   const IndexedContainer& index, const CprContainer& container,
                   JobResult& result) {
  const ConvertOptions& options = *context.options;
  const std::string& mesh_text = index.mesh_id;
  if (mesh_text.empty()) {
    return; // The index pass reported why.
  }
  if (!mesh_text.starts_with(options.only_prefix) && !options.only_prefix.starts_with(mesh_text)) {
    return;
  }
  const content::AssetId mesh_id = content::make_asset_id(mesh_text).id;

  MeshSource source;
  const MeshReadFailure read = read_mesh_container(container, source);
  if (!read.ok()) {
    // A colliding model whose mesh does not read has no split ids to name;
    // its one row carries both reasons.
    const std::string prefix = index.collision.empty() ? "" : index.collision + "; ";
    result.failed.push_back({mesh_text, prefix + "mesh read: " + std::string(name_of(read.error)) +
                                            " at " + decimal(read.offset) + " (" +
                                            tag_text(read.tag) + ")"});
    return;
  }
  if (!index.collision.empty()) {
    fail_collided_model(options, mesh_id, container, source, index.collision, result);
    return;
  }
  count_unknown_tags(container, result);
  const CprRecord* header = find_tagged(container, kHeaderRecord, RecordTag::User);
  const ModelRecords records = header != nullptr ? model_records(container, header->payload)
                                                 : ModelRecords{{}, {}, "header.bin missing"};
  if (!records.error.empty()) {
    result.failed.push_back({mesh_text, records.error});
    return;
  }
  const CprRecord* bil = container.find_record(kBilRecord);
  const CprRecord* bl = container.find_record(kBlRecord);
  for (const std::array<char, 4>& tag : source.unread_chunks) {
    ++result.unmapped["mesh_chunk." + tag_text(tag)];
  }

  SkeletonAssetData skeleton;
  const SkeletonReadFailure skeleton_read = read_skeleton(container, skeleton);
  const bool has_skeleton = skeleton_read.error != SkeletonReadError::NoSkeleton;
  const content::AssetId skeleton_id = split_id(mesh_id, content::SplitKind::Skeleton, 0);

  // Mesh.
  if (wanted(mesh_text, options)) {
    InputHasher hasher(content::AssetKind::Mesh, mesh_text);
    hasher.add(header->payload);
    for (const CprRecord* buffer : records.buffers) {
      hasher.add(buffer->payload);
    }
    for (const CprRecord* record : {bil, bl}) {
      if (record != nullptr) {
        hasher.add(record->payload);
      }
    }
    for (const CprRecord* shape : records.shape_offsets) {
      hasher.add(shape->payload);
    }
    emit(context, result, content::AssetKind::Mesh, mesh_id, hasher.finish(),
         [&](std::string& failure) {
           MeshAssetData mesh = source.mesh;
           if (has_skeleton) {
             mesh.skeleton_id = std::string(skeleton_id.view());
           }
           EncodeFailure encode{};
           std::vector<std::byte> payload = encode_mesh_asset(mesh, encode);
           if (!encode.ok()) {
             failure = encode_failure_text("mesh", encode);
           }
           return payload;
         });
  }

  // Materials.
  ModelTable table;
  if (!source.materials.empty()) {
    read_model_table(*context.install, job.container, table);
  }
  for (std::size_t k = 0; k < source.materials.size(); ++k) {
    const content::AssetId material_id =
        split_id(mesh_id, content::SplitKind::Material, static_cast<std::uint32_t>(k));
    if (!wanted(material_id.view(), options)) {
      continue;
    }
    const MaterialBlock& block = source.materials[k];
    if (!block.slots.empty() && !table.error.empty()) {
      result.failed.push_back({std::string(material_id.view()), table.error});
      continue;
    }
    std::vector<ResolvedSlot> slots;
    std::string slot_failure;
    for (std::size_t s = 0; s < block.slots.size() && slot_failure.empty(); ++s) {
      SlotResolution resolution =
          resolve_slot(table.table, block.slots[s].index, *context.resolver);
      if (resolution.error != SlotError::None) {
        slot_failure = "slot " + decimal(s) + " (" + tag_text(block.slots[s].tag) + ", index " +
                       decimal(block.slots[s].index) +
                       "): " + std::string(name_of(resolution.error));
      } else {
        slots.push_back({block.slots[s].tag, std::move(resolution.id)});
      }
    }
    if (!slot_failure.empty()) {
      result.failed.push_back({std::string(material_id.view()), slot_failure});
      continue;
    }
    const MaterialAssetData material = map_material(block, slots);
    for (const UnmappedField& field : material.unmapped) {
      ++result.unmapped["material." + field.name];
    }
    InputHasher hasher(content::AssetKind::Material, material_id.view());
    hasher.add(material_block_bytes(block));
    for (const std::string_view name : table.table.names) {
      hasher.add_text(name);
    }
    emit(context, result, content::AssetKind::Material, material_id, hasher.finish(),
         [&](std::string& failure) {
           EncodeFailure encode{};
           std::vector<std::byte> payload = encode_material_asset(material, encode);
           if (!encode.ok()) {
             failure = encode_failure_text("material", encode);
           }
           return payload;
         });
  }

  // Skeleton.
  if (has_skeleton && wanted(skeleton_id.view(), options)) {
    if (!skeleton_read.ok()) {
      result.failed.push_back({std::string(skeleton_id.view()),
                               "skeleton read: " + std::string(name_of(skeleton_read.error)) +
                                   " at bone " + decimal(skeleton_read.index)});
    } else {
      skeleton.bind_block = animation::BindBlock::B;
      InputHasher hasher(content::AssetKind::Skeleton, skeleton_id.view());
      hasher.add(bil->payload);
      hasher.add(bl->payload);
      emit(context, result, content::AssetKind::Skeleton, skeleton_id, hasher.finish(),
           [&](std::string& failure) {
             EncodeFailure encode{};
             std::vector<std::byte> payload = encode_skeleton_asset(skeleton, encode);
             if (!encode.ok()) {
               failure = encode_failure_text("skeleton", encode);
             }
             return payload;
           });
    }
  }

  // Morph sets.
  for (std::size_t s = 0; s < source.morph_sets.size(); ++s) {
    const content::AssetId shapes_id =
        split_id(mesh_id, content::SplitKind::Shapes, static_cast<std::uint32_t>(s));
    if (!wanted(shapes_id.view(), options)) {
      continue;
    }
    if (s >= records.shape_offsets.size()) {
      result.failed.push_back({std::string(shapes_id.view()), "morph set without its record"});
      continue;
    }
    InputHasher hasher(content::AssetKind::MorphTargets, shapes_id.view());
    hasher.add(records.shape_offsets[s]->payload);
    emit(context, result, content::AssetKind::MorphTargets, shapes_id, hasher.finish(),
         [&](std::string& failure) {
           EncodeFailure encode{};
           std::vector<std::byte> payload = encode_morph_targets(source.morph_sets[s], encode);
           if (!encode.ok()) {
             failure = encode_failure_text("morph targets", encode);
           }
           return payload;
         });
  }
}

JobResult run_job(const JobContext& context, const ConversionJob& job,
                  const IndexedContainer& index) {
  JobResult result;
  const FileBytes file = read_file(*context.install / job.container);
  if (!file.error.empty()) {
    result.failed.push_back({job.container, "container unreadable: " + file.error});
    return result;
  }
  CprContainer container;
  const CprError cpr_error = parse_cpr(file.bytes, container);
  if (cpr_error != CprError::None) {
    result.failed.push_back(
        {job.container, "container does not parse: " + std::string(name_of(cpr_error))});
    return result;
  }
  if (index.kind == JobKind::Model) {
    run_model_job(context, job, index, container, result);
  } else {
    run_textures_job(context, index, container, result);
  }
  return result;
}

// Runs work(i) for every i below count on `threads` workers; each call
// writes only its own slot, so nothing shared is written.
void run_parallel(std::size_t count, std::uint32_t threads,
                  const std::function<void(std::size_t)>& work) {
  std::atomic<std::size_t> next{0};
  const auto worker = [&] {
    for (std::size_t i = next.fetch_add(1); i < count; i = next.fetch_add(1)) {
      work(i);
    }
  };
  const std::size_t worker_count = std::min<std::size_t>(threads, count);
  std::vector<std::thread> pool;
  pool.reserve(worker_count > 0 ? worker_count - 1 : 0);
  for (std::size_t t = 1; t < worker_count; ++t) {
    pool.emplace_back(worker);
  }
  worker();
  for (std::thread& thread : pool) {
    thread.join();
  }
}

std::uint32_t worker_count(std::uint32_t requested) noexcept {
  if (requested != 0U) {
    return requested;
  }
  const unsigned hardware = std::thread::hardware_concurrency();
  return hardware == 0U ? 1U : hardware;
}

} // namespace

// ---- public ---------------------------------------------------------------

ConvertParse parse_convert_arguments(std::span<const std::string_view> arguments) {
  ConvertParse parse;
  const auto fail = [&parse](UsageError error, std::string_view argument) {
    parse.error = error;
    parse.argument = argument;
    return parse;
  };
  if (arguments.empty()) {
    return fail(UsageError::UnknownCommand, {});
  }
  if (arguments[0] != kCommand) {
    return fail(UsageError::UnknownCommand, arguments[0]);
  }
  constexpr std::array<std::string_view, 6> kOptions{
      kInstallOption, kOutOption, kOnlyOption, kThreadsOption, kNormalGreenOption, kUberOption};
  std::array<bool, kOptions.size()> seen{};
  ConvertOptions& options = parse.options;
  for (std::size_t i = 1; i < arguments.size(); i += 2) {
    const std::string_view option = arguments[i];
    const auto found = std::find(kOptions.begin(), kOptions.end(), option);
    if (found == kOptions.end()) {
      return fail(UsageError::UnknownOption, option);
    }
    const auto which = static_cast<std::size_t>(found - kOptions.begin());
    if (seen[which]) {
      return fail(UsageError::DuplicateOption, option);
    }
    seen[which] = true;
    if (i + 1 >= arguments.size()) {
      return fail(UsageError::MissingValue, option);
    }
    const std::string_view value = arguments[i + 1];
    if (option == kInstallOption) {
      options.install = std::filesystem::path(value);
    } else if (option == kOutOption) {
      options.output = std::filesystem::path(value);
    } else if (option == kOnlyOption) {
      options.only_prefix = std::string(value);
    } else if (option == kThreadsOption) {
      const std::optional<std::uint32_t> threads = parse_u32(value);
      if (!threads.has_value()) {
        return fail(UsageError::BadNumber, value);
      }
      options.threads = *threads;
    } else if (option == kNormalGreenOption) {
      if (value == kAsStored) {
        options.texture.normal_green = NormalMapConvention::AsStored;
      } else if (value == kFlipGreen) {
        options.texture.normal_green = NormalMapConvention::FlipGreen;
      } else {
        return fail(UsageError::BadNormalGreen, value);
      }
    } else {
      const std::optional<std::uint32_t> level = parse_u32(value);
      if (!level.has_value() || *level > kMaxUberLevel) {
        return fail(UsageError::BadNumber, value);
      }
      options.texture.bc7_uber_level = *level;
    }
  }
  if (!seen[1]) {
    return fail(UsageError::OutputMissing, {});
  }
  return parse;
}

std::vector<ConversionJob> plan_conversion(std::span<const std::string> containers_sorted) {
  std::vector<ConversionJob> jobs;
  jobs.reserve(containers_sorted.size());
  for (const std::string& container : containers_sorted) {
    jobs.push_back({container, kind_by_name(container)});
  }
  return jobs;
}

std::string render_report(const ConversionReport& report) {
  std::string text;
  text += "converted: " + decimal(report.converted) + "\n";
  text += "skipped unchanged: " + decimal(report.skipped_unchanged) + "\n";
  text += "skipped by kind: " + decimal(report.skipped_by_kind) + "\n";
  for (const FailedAsset& failed : report.failed) {
    text += "failed: " + failed.id + ": " + failed.reason + "\n";
  }
  for (const UnmappedCount& unmapped : report.unmapped) {
    text += "unmapped: " + unmapped.field + ": " + decimal(unmapped.count) + "\n";
  }
  std::array<char, 64> seconds{};
  const auto [end, ec] = std::to_chars(seconds.data(), seconds.data() + seconds.size(),
                                       report.seconds, std::chars_format::fixed, 3);
  text += "seconds: ";
  text += ec == std::errc() ? std::string(seconds.data(), end) : std::string("unrepresentable");
  text += "\n";
  text += "bytes written: " + decimal(report.bytes_written) + "\n";
  return text;
}

ConvertResult run_conversion(const ConvertOptions& options,
                             const content::InstallLocation& install) {
  const auto started = std::chrono::steady_clock::now();
  ConvertResult result;
  std::error_code ec;
  if (!fs::is_directory(install.path, ec)) {
    result.error = ConvertError::InstallNotLocated;
    result.detail = install.path.generic_string() + (ec ? ": " + ec.message() : "");
    return result;
  }
  fs::create_directories(options.output, ec);
  if (ec || !fs::is_directory(options.output, ec)) {
    result.error = ConvertError::OutputNotWritable;
    result.detail = options.output.generic_string() + (ec ? ": " + ec.message() : "");
    return result;
  }
  // A probe write, so an unwritable directory is one named failure rather
  // than one failed row per asset.
  const fs::path probe = options.output / "conversion_probe";
  const std::string probe_error = write_file_atomically(probe, {});
  if (!probe_error.empty()) {
    result.error = ConvertError::OutputNotWritable;
    result.detail = probe_error;
    return result;
  }
  fs::remove(probe, ec);
  if (ec) {
    result.error = ConvertError::OutputNotWritable;
    result.detail = "remove " + probe.generic_string() + ": " + ec.message();
    return result;
  }
  const fs::path record_path = options.output / content::kConvertedCacheRecordFile;
  fs::remove(record_path, ec);
  if (ec) {
    result.error = ConvertError::OutputNotWritable;
    result.detail = record_path.generic_string() + ": " + ec.message();
    return result;
  }

  Catalog catalog = read_catalog(install.path);
  if (catalog.error != ConvertError::None) {
    result.error = catalog.error;
    result.detail = std::move(catalog.detail);
    return result;
  }
  const std::vector<ConversionJob> jobs = plan_conversion(catalog.containers);
  const std::uint32_t threads = worker_count(options.threads);
  ConversionReport& report = result.report;

  // Index pass.
  std::vector<IndexedContainer> indexed(jobs.size());
  run_parallel(jobs.size(), threads, [&](std::size_t i) {
    try {
      indexed[i] = index_container(install.path, jobs[i]);
    } catch (const std::exception& error) {
      indexed[i] = IndexedContainer{};
      indexed[i].error = std::string("index: ") + error.what();
    }
  });
  std::vector<std::string> names;
  // Every id the index derived, with the job that holds it, for the
  // collision check; mesh ids separately for the prefix rule.
  std::map<std::string, std::size_t> owners;
  std::vector<std::string> mesh_ids;
  for (std::size_t i = 0; i < jobs.size(); ++i) {
    IndexedContainer& entry = indexed[i];
    if (!entry.error.empty()) {
      report.failed.push_back({jobs[i].container, entry.error});
      continue;
    }
    report.failed.insert(report.failed.end(), entry.failed.begin(), entry.failed.end());
    names.insert(names.end(), entry.texture_names.begin(), entry.texture_names.end());
    if (!entry.mesh_id.empty()) {
      const auto [it, inserted] = owners.emplace(entry.mesh_id, i);
      if (!inserted) {
        // The job still runs, to name every asset of this model the prefix
        // admits (its split ids need the mesh read).
        entry.collision = "id collides with " + jobs[it->second].container;
      } else {
        mesh_ids.push_back(entry.mesh_id);
      }
    }
  }
  std::sort(mesh_ids.begin(), mesh_ids.end());
  for (std::size_t i = 0; i < jobs.size(); ++i) {
    IndexedContainer& entry = indexed[i];
    std::vector<TextureEntry> kept;
    for (TextureEntry& texture : entry.textures) {
      std::string colliding;
      const auto [it, inserted] = owners.emplace(texture.id, i);
      if (!inserted) {
        colliding = jobs[it->second].container;
      } else {
        for (std::size_t dot = texture.id.find('.'); dot != std::string::npos;
             dot = texture.id.find('.', dot + 1)) {
          if (std::binary_search(mesh_ids.begin(), mesh_ids.end(), texture.id.substr(0, dot))) {
            colliding = jobs[owners.at(texture.id.substr(0, dot))].container;
            break;
          }
        }
      }
      if (colliding.empty()) {
        kept.push_back(std::move(texture));
      } else if (texture.id.starts_with(options.only_prefix)) {
        report.failed.push_back({texture.id, "id collides with " + colliding});
      }
    }
    entry.textures = std::move(kept);
  }
  const IndexedResolver resolver(std::move(names));

  // Jobs.
  std::vector<std::size_t> work;
  for (std::size_t i = 0; i < jobs.size(); ++i) {
    if (!indexed[i].error.empty()) {
      continue;
    }
    if (indexed[i].kind == JobKind::Other) {
      ++report.skipped_by_kind;
    } else {
      work.push_back(i);
    }
  }
  const JobContext context{&install.path, &options, &resolver};
  std::vector<JobResult> results(work.size());
  run_parallel(work.size(), threads, [&](std::size_t w) {
    const std::size_t i = work[w];
    try {
      results[w] = run_job(context, jobs[i], indexed[i]);
    } catch (const std::exception& error) {
      results[w] = JobResult{};
      results[w].failed.push_back({jobs[i].container, std::string("job: ") + error.what()});
    }
  });
  std::map<std::string, std::uint64_t> unmapped;
  for (const JobResult& job : results) {
    report.converted += job.converted;
    report.skipped_unchanged += job.skipped_unchanged;
    report.bytes_written += job.bytes_written;
    report.failed.insert(report.failed.end(), job.failed.begin(), job.failed.end());
    for (const auto& [field, count] : job.unmapped) {
      unmapped[field] += count;
    }
  }
  std::sort(report.failed.begin(), report.failed.end(),
            [](const FailedAsset& a, const FailedAsset& b) {
              return a.id != b.id ? a.id < b.id : a.reason < b.reason;
            });
  for (const auto& [field, count] : unmapped) {
    report.unmapped.push_back({field, count});
  }

  // Record, last.
  const content::ConvertedCacheRecord record{content::kCookedFormatVersion, install.path.string(),
                                             install.graphics_containers};
  const std::string record_text = content::encode_converted_cache_record(record);
  const std::string record_error =
      write_file_atomically(record_path, std::as_bytes(std::span(record_text)));
  if (!record_error.empty()) {
    result.error = ConvertError::OutputNotWritable;
    result.detail = record_error;
  } else {
    report.bytes_written += record_text.size();
  }
  report.seconds =
      std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
  return result;
}

InputHasher::InputHasher(content::AssetKind kind, std::string_view id) {
  append_u32(buffer_, content::kCookedFormatVersion);
  append_u32(buffer_, static_cast<std::uint32_t>(kind));
  add_text(id);
}

void InputHasher::add(std::span<const std::byte> bytes) {
  const std::uint64_t size = bytes.size();
  for (std::size_t i = 0; i < 8; ++i) {
    buffer_.push_back(static_cast<std::byte>((size >> (8U * i)) & 0xFFU));
  }
  buffer_.insert(buffer_.end(), bytes.begin(), bytes.end());
}

void InputHasher::add_text(std::string_view text) { add(std::as_bytes(std::span(text))); }

content::Hash InputHasher::finish() const noexcept { return content::hash_bytes(buffer_); }

} // namespace bw::content_converter
