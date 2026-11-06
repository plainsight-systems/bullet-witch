#ifndef BW_CONTENT_CONVERTER_CONVERSION_HPP
#define BW_CONTENT_CONVERTER_CONVERSION_HPP
#include "bw/content/asset_id.hpp"
#include "bw/content/cooked_asset.hpp"
#include "bw/content/install.hpp"
#include "bw/content_converter/texture_reader.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// Responsibility: the conversion of an install into the converted-originals
// cache (docs/architecture/content.md, invariants 4, 6 and 7: converted on
// the player's machine into a local cache that records the install;
// deterministic; every asset carries its input hash and format version
// and is re-converted only when either changes) and the command line that
// drives it.
//
// Command line:
//   content_converter convert [--install DIR] --out DIR [--only PREFIX]
//       [--threads N] [--normal-green as-stored|flip] [--bc7-uber 0..4]
//   --install absent means Steam discovery (content install.hpp); --out
//   is the converted layer's directory, created if absent; --only limits
//   the assets written to ids with that prefix; --threads 0 (default) is
//   the hardware concurrency. Exit 0 when nothing failed, 1 when any asset
//   failed (the report still names every one), 2 on a usage error, an
//   install that does not locate or validate, an output directory that
//   cannot be written, a catalog that cannot be read, or a CrossOver
//   bottle listing the OS refuses during Steam discovery (named with the
//   OS message, never taken as no bottles). When the jobs ran before the
//   error (the cache record's write failed), the report is printed first,
//   then the error, and the exit is 2.
//
// Contract.
//   Plan: the container list is the union of the 25 .rat files' entries
//   (rat_index.hpp), sorted bytewise by relative path; one job per model
//   container (mesh, its materials, skeleton, morph sets) and one per
//   texture-holding container (every DDS and DDSC record); containers of
//   other kinds are counted as skipped by kind. plan_conversion is pure
//   over that list and decides by directory and name; a container of
//   another name that holds DDS or DDSC records (116 in the install)
//   becomes a Textures job in the run's index pass, which reads every
//   container once. The --only prefix is applied by the run, by asset id.
//   Ids: the mesh's id is the vfx2.mesh.create CALL's qualified name,
//   `model.<dir>.mesh`; materials, skeleton and shapes
//   by split_asset_id; a texture record by its qualified name, or the
//   container-record rule when it has none. Collisions: the first
//   container in catalog order keeps an id; a later container deriving
//   the same id fails, naming the first, and a losing model fails every
//   asset of it the prefix admits, split ids included; a texture id that
//   extends a mesh id (a path under the mesh's file) fails the texture.
//   Input hash (content::Hash, BLAKE3): over, in order, the format version
//   as 4 little-endian bytes, the kind, the id, then the bytes of every
//   source item in a fixed order: for a mesh, header.bin and every buffer
//   record it names, in chunk order, plus the two skeleton records and the
//   morph records; for a material, the material's block bytes and the
//   texture table's names; for a skeleton, its two records (the skeleton
//   asset's bind field is written as BindBlock::B, the measured fact the
//   plan records; read_skeleton leaves it Unknown); for a texture,
//   the pipeline options that shape its bytes (the green convention and
//   the BC7 uber level, as text), then the descriptor and the PNG(s); for
//   morph targets, the TS2V record. Every input that changes the output
//   bytes is in the hash, so a skip never keeps stale bytes.
//   Incremental: an output file that exists, parses, carries
//   kCookedFormatVersion and the same input hash is skipped unread
//   further; anything else is rewritten.
//   Write: the whole file to "<path>.tmp" in the output directory, then
//   renamed over the destination, so a reader never sees a partial asset;
//   a failure removes the temporary. Directories are created as needed.
//   Parallel: jobs run on `threads` workers; each job's output depends on
//   its inputs alone, so the cache bytes are the same for any thread
//   count and any order; the report is sorted by id before it is printed.
//   Record: after the jobs, the converted cache record
//   (content layers.hpp) is written with the install path and the
//   graphics container count, last, so a cache without the record is one
//   whose conversion did not finish.
//   Report: converted, skipped (unchanged), skipped by kind, failed with
//   id and reason, unmapped fields with counts, the elapsed seconds and
//   bytes written; render_report prints it in that order, one line each,
//   each line carrying its count or its id and reason in decimal, the
//   seconds line last but one; the label wording is the tool's own and no
//   caller parses it. Deterministic but for the time line.
//   Determinism: two runs over the same install produce byte-identical
//   caches, record included.
//
// Core guidelines read: CP.3 and CP.31 (jobs share nothing writable and
// carry their results by value), CP.23 (workers are joined before the
// report), E.27 (every failure is a named row, never a stop), I.1 (every
// path and option is an argument). Performance guidelines read: GDSA.8
// and GDSA.13 (work is partitioned by container, the unit of input, and
// grouped by kind), CACHE.1 (per-job state is per thread, nothing shared
// is written during the run), TLM.5 was read; the report is formatted once
// at the end.
namespace bw::content_converter {

enum class JobKind : std::uint8_t {
  Model,
  Textures,
  Other,
};

struct ConversionJob {
  // Relative to the data root, '/' separators.
  std::string container{};
  JobKind kind{JobKind::Other};
};

struct ConvertOptions {
  std::optional<std::filesystem::path> install{};
  std::filesystem::path output{};
  std::string only_prefix{};
  std::uint32_t threads{};
  TexturePipelineOptions texture{};
};

enum class UsageError : std::uint8_t {
  None,
  UnknownCommand,
  UnknownOption,
  MissingValue,
  DuplicateOption,
  OutputMissing,
  BadNumber,
  BadNormalGreen,
};
struct ConvertParse {
  ConvertOptions options{};
  UsageError error{UsageError::None};
  // The argument concerned, or empty when none is (no arguments,
  // OutputMissing).
  std::string_view argument{};
  [[nodiscard]] bool ok() const noexcept { return error == UsageError::None; }
};
[[nodiscard]] ConvertParse parse_convert_arguments(std::span<const std::string_view> arguments);
inline constexpr std::string_view kConvertUsage =
    "content_converter convert [--install DIR] --out DIR [--only PREFIX] [--threads N] "
    "[--normal-green as-stored|flip] [--bc7-uber 0..4]";

// Pure: the jobs for a sorted container list, kinds decided by the
// container's directory and name (model/mesh.cpr is Model; texture/
// texture.cpr, shared_texture/*.cpr and cubemap/*.cpr are Textures); the
// run's index pass adds any other container holding DDS or DDSC records.
[[nodiscard]] std::vector<ConversionJob>
plan_conversion(std::span<const std::string> containers_sorted);

struct FailedAsset {
  std::string id{};
  std::string reason{};
};
struct UnmappedCount {
  std::string field{};
  std::uint64_t count{};
};
struct ConversionReport {
  std::uint64_t converted{};
  std::uint64_t skipped_unchanged{};
  std::uint64_t skipped_by_kind{};
  std::vector<FailedAsset> failed{};
  std::vector<UnmappedCount> unmapped{};
  std::uint64_t bytes_written{};
  double seconds{};
};
[[nodiscard]] std::string render_report(const ConversionReport& report);

enum class ConvertError : std::uint8_t {
  None,
  InstallNotLocated,
  OutputNotWritable,
  CatalogUnreadable,
};
// The report is filled only after the jobs ran; an error with an empty
// report means the run stopped before them, and the executable prints the
// report first when it is not empty. The executable's stderr names the
// error enumerator (install.hpp or ConvertError) and the path concerned.
struct ConvertResult {
  ConversionReport report{};
  ConvertError error{ConvertError::None};
  std::string detail{};
  [[nodiscard]] bool ok() const noexcept { return error == ConvertError::None; }
};

// The OS-bound run over a located install.
[[nodiscard]] ConvertResult run_conversion(const ConvertOptions& options,
                                           const content::InstallLocation& install);

// The input hash of a list of items, in order, as the contract states.
// Framing: the format version as 4 little-endian bytes, the kind as 4
// little-endian bytes, then the id and each item as an 8-byte
// little-endian length followed by its bytes; finish() is BLAKE3 of that
// buffer (content cooked_asset.hpp, hash_bytes), so a test can compute
// the expected value.
class InputHasher {
public:
  InputHasher(content::AssetKind kind, std::string_view id);
  void add(std::span<const std::byte> bytes);
  void add_text(std::string_view text);
  [[nodiscard]] content::Hash finish() const noexcept;

private:
  std::vector<std::byte> buffer_{};
};

} // namespace bw::content_converter

#endif
