#ifndef BW_CONTENT_CONVERTER_CONVERSION_HPP
#define BW_CONTENT_CONVERTER_CONVERSION_HPP
#include "bw/content/asset_id.hpp"
#include "bw/content/cooked_asset.hpp"
#include "bw/content/install.hpp"
#include "bw/content_converter/asset_data.hpp"
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
//   source item in a fixed order: for a mesh, the mesh conversion revision
//   as the text "mesh-conversion <kMeshConversionRevision>", then
//   header.bin and every buffer record it names, in chunk order, plus the
//   two skeleton records and the morph records; for a material, the mapping revision as the text
//   "material-mapping <kMaterialMappingRevision>" (material_mapping.hpp),
//   the material's block bytes, the texture table's names, and the
//   descriptor and the PNG bytes of its base color texture record when it
//   has a BTX0 slot (the base alpha class the blend mode is mapped from is
//   a function of both); for a skeleton, its two records (the skeleton
//   asset's bind field is written as BindBlock::B, the measured fact the
//   plan records; read_skeleton leaves it Unknown); for a texture,
//   the pipeline options that shape its bytes (the green convention and
//   the BC7 uber level, as text), then the descriptor and the PNG(s); for
//   morph targets, the mesh conversion revision text, the TS2V record and
//   its mesh's header.bin and buffer records (a split vertex of the mesh
//   repeats its delta). Every input that changes the output
//   bytes is in the hash, so a skip never keeps stale bytes.
//   Meshes, tangent handedness (write_tangent_handedness, a pure function
//   of the mesh and its morph sets, run before either is encoded): every
//   tangent the content stores has w = +1, mirrored texture coordinates or
//   not, so the conversion writes the sign. A triangle is three consecutive
//   indices of a draw's range, each plus the draw's base vertex. For each
//   triangle of a group whose layout has Tangent, with positions P, texture
//   coordinate set 0 as stored and the sum N of its three vertex normals:
//   h = sign((u1 - u0) * (v2 - v0) - (u2 - u0) * (v1 - v0)) *
//   sign(dot(cross(P1 - P0, P2 - P0), N)), which is the sign of
//   dot(cross(dP/du, dP/dv), N). A triangle has no h when its texture area
//   is under 1e-12 in magnitude, when dot(cross(P1 - P0, P2 - P0), N) is
//   zero, or when either is not finite; a group without Position, Normal
//   or TexCoord0 has no triangle with h. Each corner of a triangle with h
//   gets w = -h: -1 where the texture is not mirrored, +1 where it is. A
//   vertex whose corners all agree takes that w; a vertex used only by
//   triangles without h, or by none, takes -1; a vertex whose corners
//   disagree keeps -1 and is copied once with +1, the copy appended after
//   the group's last original vertex in the order the originals appear,
//   with every stream of the vertex repeated (stream 1 included) and, in
//   every target of the group's morph set, its delta repeated in the same
//   order; each index of a triangle with h = -1 that named the original
//   names the copy. A draw whose triangles name a copy has its
//   vertex_count widened to reach the last index it names; its min_index
//   is kept. The stored tangent's xyz is kept; w is written as -32767 or
//   +32767. The normal map's two channels are kept as stored
//   (--normal-green as-stored is the rule for this content): with this w
//   the frame w * cross(normal, tangent) of the lit template has its
//   bitangent along -dP/dv, the frame the content's normal maps are read
//   in.
//   Each failure names its group (TangentHandednessError, one rule each):
//     TangentTypeUnsupported    a Tangent that is not short4n, which
//                               cannot hold the sign;
//     DrawOutOfRange            a draw's range past the group's indices,
//                               or an index naming a vertex past the
//                               group's count;
//     DrawRangesOverlap         two draws of the group share an index
//                               position, which could need two remaps;
//     MorphVertexCountMismatch  the group's morph set has another vertex
//                               count than the group;
//     VertexCountOverflow       the copies take the group past 65,535
//                               vertices.
//   On a failure the mesh and the morph sets are left partly written and
//   the run writes none of them: the mesh and every morph set of it fail
//   with the enumerator's name and the group.
//   Materials: a material's base alpha class is classify_base_alpha of
//   its BTX0 record's decoded PNG (material_mapping.hpp), Opaque for a
//   record whose texels are halfs (texture_reader.hpp, half_texels), and
//   NoTexture without a BTX0 slot; the PNG is decoded only when the
//   material is converted, not when it is skipped. A material whose BTX0
//   slot names a record that is not a 2D texture, is not indexed, or
//   whose container, descriptor or PNG cannot be read fails, naming which.
//   A material that map_material refuses fails with the error's name and
//   the name it gives, like any other failed asset.
//   Incremental: an output file that exists, parses, carries
//   kCookedFormatVersion and the same input hash is skipped unread
//   further; a material is skipped only when its unmapped fields file
//   also exists and parses, and that file is read to count its fields for
//   the report; anything else is rewritten.
//   Write: the whole file to "<path>.tmp" in the output directory, then
//   renamed over the destination, so a reader never sees a partial asset;
//   a failure removes the temporary. Directories are created as needed.
//   A material's unmapped fields file (material_mapping.hpp) is written
//   the same way, before the material's asset, at the asset's path with
//   kUnmappedFieldsSuffix appended; when that write fails the material
//   fails by name and its asset is not written. No asset id maps to that
//   path (a layer file ends with the cooked extension), nothing walks a
//   layer directory, and the game never opens it.
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

// Numbers the mesh rules above: any change to them changes the number,
// and the number is in every mesh's and morph set's input hash, so a rule
// change reconverts meshes and nothing else.
inline constexpr std::uint32_t kMeshConversionRevision = 2;

enum class TangentHandednessError : std::uint8_t {
  None,
  TangentTypeUnsupported,
  DrawOutOfRange,
  DrawRangesOverlap,
  MorphVertexCountMismatch,
  VertexCountOverflow,
};
struct TangentHandednessResult {
  TangentHandednessError error{TangentHandednessError::None};
  // The group the error names; 0 when there is none.
  std::uint32_t group{};
  [[nodiscard]] bool ok() const noexcept { return error == TangentHandednessError::None; }
};
// The rule "Meshes, tangent handedness" above. `morph_sets` are the mesh's,
// each naming its group.
[[nodiscard]] TangentHandednessResult
write_tangent_handedness(MeshAssetData& mesh, std::vector<MorphTargetsData>& morph_sets);

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
