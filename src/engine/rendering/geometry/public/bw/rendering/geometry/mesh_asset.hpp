#ifndef BW_RENDERING_GEOMETRY_MESH_ASSET_HPP
#define BW_RENDERING_GEOMETRY_MESH_ASSET_HPP
#include "bw/rendering/geometry/vertex_streams.hpp"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <type_traits>

// Responsibility: the mesh payload of a cooked asset and its reader
// (docs/architecture/content.md, "Asset kinds": vertex streams in the
// vertex-pulling layout, a 16-bit index buffer, a draw list with base vertex
// and material ids, clusters as index ranges each with a box, detail levels
// as cluster ranges each with a screen size, bounds, the skeleton id it
// binds to; readers of a payload live with the consuming system). The converter writes this
// layout; the renderer uploads the stream and index bytes as they are and
// pulls vertices with vertex_streams.hpp.
//
// What a mesh is: one or more groups, each with its own vertex layout, one
// or two vertex streams and one index buffer; draws under a group, each a
// triangle-list range with a base vertex and a material; a bone map per
// skinned draw listing skeleton bone positions; an axis-aligned box, a
// center and a radius; morph targets for some groups; clusters, each a run
// of whole triangles of one draw with the box of its vertices and, when
// its draw is bound whole to one bone, that bone; detail levels, each a run
// of clusters with the screen size it is chosen at. A Tangent
// attribute's w is the bitangent's sign, -1 or +1, equal over the vertices
// of each triangle; the writer establishes it and decode does not read it.
//
// Layout, little-endian, every record trivially copyable with no padding,
// read by copy (memcpy) from the payload bytes, never by pointer cast:
//   MeshPayloadHeader at 0; then at the offsets it names, each a multiple
//   of 16 from the payload start: group records, draw records, bone map
//   records, bone entries (u32 skeleton bone positions), the string table
//   (bytes, no terminators), cluster records, level records, and each
//   group's stream and index bytes at
//   the offsets its record names. Offsets are from the payload start.
//
// Contract (decode_mesh_asset refuses a payload that breaks a rule below and
// names a broken rule; with several broken, which one is named is not
// promised; a payload that decodes satisfies all of these):
//   1. The header fits, every section offset is a multiple of 16 and every
//      section ends within the payload.
//   2. Each group's attribute and stream fields form a VertexStreamLayout
//      that validate_vertex_stream_layout accepts; attribute and element
//      type bytes are enumerators of VertexAttribute and VertexElementType.
//   3. Each group's stream s has size vertex_count * stride(s), lies within
//      the payload at a 16-aligned offset, and stream1 fields are zero when
//      stream_count is 1; vertex_count is at least 1.
//   4. Each group's index range holds index_count 16-bit indices, a
//      multiple of 3 and at least 3, within the payload at a 16-aligned
//      offset.
//   5. The groups' draw ranges [first_draw, first_draw + draw_count) are
//      consecutive, in group order, and cover every draw once.
//   6. Each draw: index_count is a multiple of 3 and at least 3;
//      first_index + index_count is within its group's index_count;
//      base_vertex + min_index + vertex_count is within its group's
//      vertex_count; material is below material_count or kNoMaterial;
//      bone_map is below bone_map_count or kNoBoneMap. The indices
//      themselves are not read: the shader adds base_vertex, and the draw's
//      vertex range is what validate_vertex_range checks at draw time.
//   7. Bone map entry ranges are within bone_entry_count. Entries are
//      skeleton bone positions and are not checked against the skeleton
//      here; the consumer that binds the skeleton checks them.
//   8. skeleton id: the string table slice, empty when the mesh has no
//      skeleton; when present it validates as an asset id. Material ids are
//      not stored: draw.material i names "<mesh id>.material.<i>"
//      (content asset_id.hpp, split_asset_id).
//   9. morph_set: kNoMorphSet, or n for the asset "<mesh id>.shapes.<n>";
//      the shapes asset names this group and its vertex count.
//  10. Each cluster: draw is below draw_count
//      (ClusterDrawOutOfRange); index_count is a multiple of 3, at least 3
//      and at most kMaxClusterIndices, and [first_index, first_index +
//      index_count) lies within its draw's index range, first_index in the
//      draw's group's index buffer as the draw's is
//      (ClusterIndexRangeInvalid); bone is kNoClusterBone unless its draw
//      has a bone map (ClusterBoneWithoutMap). The box is not checked
//      against the vertices.
//  11. Levels: level_count is 1 to kMaxMeshLevels; the levels' cluster
//      ranges [first_cluster, first_cluster + cluster_count) are
//      consecutive, in level order, each at least one cluster, and cover
//      every cluster once (LevelRangeInvalid); within a level the
//      clusters' draw indices do not decrease, so a draw's clusters at a
//      level are one run (ClusterOrderInvalid). Level 0 is the most
//      detailed. screen_size is not checked.
//   Bounds: max, min, center, radius. They are not checked against the
//   vertices; a mesh may carry zeros.
//
// A cluster is the unit culling tests and a draw record draws
// (docs/architecture/scene.md, invariant 6). The 128-triangle ceiling is
// the converter's rule and this reader's bound.
// A cluster record carries a plain box and no backface or visibility data:
// the dispatch tests a box against a frustum and a hierarchy and nothing
// else.
//
// Core guidelines read: SL.con.3 (every offset and count is checked before
// use), C.183 (records are copied out of the bytes, no punning), I.1 and
// Con.5 (the view is a pure function of the bytes), ES.46 (every field has
// an explicit width). Performance guidelines read: CDSA.32 (buffers are in
// the GPU's layout and are handed over without a copy), CACHE.2 and
// CACHE.5 (records are declared with their byte layout stated and
// asserted), GDSA.12 (draws, groups and maps are indexed directly).
namespace bw::geometry {

static_assert(std::endian::native == std::endian::little,
              "cooked payloads are little-endian and are read on little-endian targets");

inline constexpr std::uint32_t kMeshSectionAlignment = 16;
inline constexpr std::uint32_t kNoMaterial = 0xFFFFFFFFU;
inline constexpr std::uint32_t kNoBoneMap = 0xFFFFFFFFU;
inline constexpr std::uint32_t kNoMorphSet = 0xFFFFFFFFU;
inline constexpr std::uint32_t kNoClusterBone = 0xFFFFFFFFU;
// 128 triangles.
inline constexpr std::uint32_t kMaxClusterIndices = 384;
inline constexpr std::uint32_t kMaxMeshLevels = 8;

struct MeshPayloadHeader {
  std::uint32_t group_count{};
  std::uint32_t draw_count{};
  std::uint32_t material_count{};
  std::uint32_t bone_map_count{};
  std::uint32_t bone_entry_count{};
  std::uint32_t string_bytes{};
  std::uint32_t skeleton_id_offset{};
  std::uint32_t skeleton_id_length{};
  std::array<float, 3> bounds_max{};
  std::array<float, 3> bounds_min{};
  std::array<float, 3> center{};
  float radius{};
  std::uint32_t groups_offset{};
  std::uint32_t draws_offset{};
  std::uint32_t bone_maps_offset{};
  std::uint32_t bone_entries_offset{};
  std::uint32_t strings_offset{};
  std::uint32_t cluster_count{};
  std::uint32_t level_count{};
  std::uint32_t clusters_offset{};
  std::uint32_t levels_offset{};
};
static_assert(sizeof(MeshPayloadHeader) == 108);
static_assert(std::is_trivially_copyable_v<MeshPayloadHeader>);

struct MeshAttributeRecord {
  std::uint8_t attribute{};
  std::uint8_t type{};
  std::uint8_t stream{};
  std::uint8_t reserved{};
  std::uint32_t offset{};
};
static_assert(sizeof(MeshAttributeRecord) == 8);

struct MeshGroupRecord {
  std::array<MeshAttributeRecord, kVertexAttributeCount> attributes{};
  std::uint32_t stride0{};
  std::uint32_t stride1{};
  std::uint8_t attribute_count{};
  std::uint8_t stream_count{};
  std::uint8_t reserved0{};
  std::uint8_t reserved1{};
  std::uint32_t vertex_count{};
  std::uint32_t stream0_offset{};
  std::uint32_t stream0_size{};
  std::uint32_t stream1_offset{};
  std::uint32_t stream1_size{};
  std::uint32_t index_offset{};
  std::uint32_t index_count{};
  std::uint32_t first_draw{};
  std::uint32_t draw_count{};
  std::uint32_t morph_set{};
  std::uint32_t reserved2{};
};
static_assert(sizeof(MeshGroupRecord) == 152);
static_assert(std::is_trivially_copyable_v<MeshGroupRecord>);

struct MeshDrawRecord {
  std::uint32_t base_vertex{};
  std::uint32_t min_index{};
  std::uint32_t vertex_count{};
  std::uint32_t first_index{};
  std::uint32_t index_count{};
  std::uint32_t material{};
  std::uint32_t bone_map{};
  std::uint32_t reserved{};
};
static_assert(sizeof(MeshDrawRecord) == 32);

struct MeshBoneMapRecord {
  std::uint32_t first_entry{};
  std::uint32_t entry_count{};
};
static_assert(sizeof(MeshBoneMapRecord) == 8);

// One cluster: a run of whole triangles of one draw. first_index is in the
// draw's group's index buffer; the box is in the mesh's space; bone is the
// skeleton bone position every vertex of the draw is bound to at full
// weight, or kNoClusterBone.
struct MeshClusterRecord {
  std::uint32_t first_index{};
  std::uint32_t index_count{};
  std::uint32_t draw{};
  std::uint32_t bone{kNoClusterBone};
  std::array<float, 3> box_min{};
  std::array<float, 3> box_max{};
};
static_assert(sizeof(MeshClusterRecord) == 40);

// One detail level: a run of clusters and the screen size it is chosen at
// (bw/rendering/scene/detail_level.hpp).
struct MeshLevelRecord {
  std::uint32_t first_cluster{};
  std::uint32_t cluster_count{};
  float screen_size{};
  std::uint32_t reserved{};
};
static_assert(sizeof(MeshLevelRecord) == 16);

// Named failures; the index of the group, draw, map, cluster or level
// concerned is reported beside it.
enum class MeshAssetError : std::uint8_t {
  None,
  HeaderTruncated,
  SectionMisaligned,
  SectionOutOfBounds,
  NoGroups,
  AttributeUnknown,
  // validate_vertex_stream_layout refused the group's layout; the
  // VertexStreamError is reported beside it.
  LayoutInvalid,
  VertexCountZero,
  StreamSizeMismatch,
  StreamOutOfBounds,
  IndexCountInvalid,
  IndexOutOfBounds,
  DrawRangeInvalid,
  DrawIndexRangeInvalid,
  DrawVertexRangeInvalid,
  MaterialOutOfRange,
  BoneMapOutOfRange,
  BoneEntriesOutOfRange,
  SkeletonIdInvalid,
  ClusterDrawOutOfRange,
  ClusterIndexRangeInvalid,
  ClusterBoneWithoutMap,
  LevelRangeInvalid,
  ClusterOrderInvalid,
};

struct MeshAssetFailure {
  MeshAssetError error{MeshAssetError::None};
  // The group, draw, bone map, cluster or level index concerned, or 0.
  std::uint32_t index{};
  VertexStreamError layout_error{VertexStreamError::None};
};

// A validated view over payload bytes the caller keeps alive.
class MeshAssetView {
public:
  MeshAssetView() = default;
  [[nodiscard]] const MeshPayloadHeader& header() const noexcept { return header_; }
  [[nodiscard]] std::uint32_t group_count() const noexcept { return header_.group_count; }
  [[nodiscard]] std::uint32_t draw_count() const noexcept { return header_.draw_count; }
  [[nodiscard]] std::uint32_t bone_map_count() const noexcept { return header_.bone_map_count; }
  [[nodiscard]] std::uint32_t cluster_count() const noexcept { return header_.cluster_count; }
  [[nodiscard]] std::uint32_t level_count() const noexcept { return header_.level_count; }
  // Preconditions: the index is below the matching count.
  [[nodiscard]] MeshGroupRecord group(std::uint32_t index) const noexcept;
  [[nodiscard]] MeshDrawRecord draw(std::uint32_t index) const noexcept;
  [[nodiscard]] MeshBoneMapRecord bone_map(std::uint32_t index) const noexcept;
  [[nodiscard]] std::uint32_t bone_entry(std::uint32_t index) const noexcept;
  [[nodiscard]] MeshClusterRecord cluster(std::uint32_t index) const noexcept;
  [[nodiscard]] MeshLevelRecord level(std::uint32_t index) const noexcept;
  // The VertexStreamLayout the group's record states; validated by decode.
  [[nodiscard]] VertexStreamLayout layout(const MeshGroupRecord& group) const noexcept;
  // stream is 0 or 1 and below the group's stream_count.
  [[nodiscard]] std::span<const std::byte> stream_bytes(const MeshGroupRecord& group,
                                                        std::uint32_t stream) const noexcept;
  [[nodiscard]] std::span<const std::byte> index_bytes(const MeshGroupRecord& group) const noexcept;
  [[nodiscard]] std::string_view skeleton_id() const noexcept;
  [[nodiscard]] std::span<const std::byte> payload() const noexcept { return payload_; }

private:
  friend struct MeshAssetDecode;
  friend MeshAssetFailure decode_mesh_asset(std::span<const std::byte>, MeshAssetView&) noexcept;
  std::span<const std::byte> payload_{};
  MeshPayloadHeader header_{};
};

// Fills `view` and returns None, or returns a failure and leaves
// `view` unspecified.
[[nodiscard]] MeshAssetFailure decode_mesh_asset(std::span<const std::byte> payload,
                                                 MeshAssetView& view) noexcept;

} // namespace bw::geometry

#endif
