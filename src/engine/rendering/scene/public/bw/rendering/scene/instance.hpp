#ifndef BW_RENDERING_SCENE_INSTANCE_HPP
#define BW_RENDERING_SCENE_INSTANCE_HPP

#include "bw/math/matrix/matrix4.hpp"
#include "bw/math/vector/vector3.hpp"
#include "bw/rendering/geometry/vertex_streams.hpp"
#include "bw/rendering/materials/material.hpp"
#include "bw/rendering/materials/shading.hpp"
#include "bw/rendering/shaders/parameter_block.hpp"

#include <cstdint>

// Responsibility: one instance of the scene as the world publishes it and as
// the GPU holds it (docs/architecture/scene.md, invariants 1 to 4 and "The
// instance"): its id, what a publisher states about it, and the record every
// pass's shader reaches by the instance's index. The set that holds
// instances is instance_set.hpp; the mesh tables an instance's geometry
// names are scene_geometry.hpp.
//
// An instance is one draw of a placed mesh: one index range with its vertex
// streams, one material instance, one transform and one box. A placed mesh
// of n draws is n instances.
// The game places no instanced mesh, so one record per instance holds
// everything a shader reaches by index. Positions and transforms are plain
// floats: a stage fits a float, and the record is read whole.
//
// Contract.
//   1. InstanceId is an index and a generation. A value-initialized id is
//      invalid. The index is the record's position in the record buffer
//      and the number a shader fetches the record by.
//   2. InstanceRecord is the GPU record, one per index, 336 bytes, in the
//      parameter block layout so C++ and HLSL read the same bytes:
//        object_to_world            the transform, row vectors
//        previous_object_to_world   the transform the frame before
//        bounds_center, _extent     the world box, padded by
//                                   max_displacement on every side
//        previous_bounds_center, _extent  the world box the frame before
//        streams                    the draw's vertex streams with its
//                                   base vertex (vertex_streams.hpp)
//        material                   the material block's buffer and offset
//        first_level, level_count   the draw's detail levels in the scene's
//                                   level table (scene_geometry.hpp)
//        pipeline                   the instance's pipeline index
//                                   (draw_records.hpp), or kNone when the
//                                   CPU draws it
//        blend_mode                 materials::BlendMode
//        kind_and_flags             InstanceKind in bits 0 to 7, the flag
//                                   bits above
//        sort_priority              read by the CPU order only
//        deformed_current, _previous  the geometry feed's copy indices, or
//                                   kNone
//        bone_first, bone_count     the instance's range in the bone
//                                   buffer, or kNone and 0
//        bone_slot                  which of the range's two slots is
//                                   current
//        max_displacement           the template's declared maximum
//      The four geometry-feed fields are kNone, 0 and 0 in every record:
//      nothing in the tree writes them, and while bone_first is kNone a
//      cluster's bone index is not applied by culling or by a vertex
//      shader.
//   3. world_box(local, transform, pad) is the axis-aligned box of the
//      eight transformed corners of `local`, grown by `pad` on every side.
//      An empty local box (min above max on any axis) gives an empty box.
//
// Core guidelines read: I.1, Enum.3, Con.5, C.131 (the record is plain
// data). Performance guidelines read: CDSA.3 (index plus generation;
// compared on every access by the set), CACHE.2 and CACHE.5 (the layout is
// stated and asserted), GPU.2 (one record per index, read whole by a lane).
namespace bw::scene {

inline constexpr std::uint32_t kNone = 0xFFFFFFFFU;

struct InstanceId {
  std::uint32_t index{kNone};
  std::uint32_t generation{};
  [[nodiscard]] constexpr bool valid() const noexcept { return index != kNone; }
  [[nodiscard]] friend constexpr bool operator==(InstanceId, InstanceId) noexcept = default;
};

enum class InstanceKind : std::uint8_t { Mesh, Emitter, Decal };

// Flag bits, above the kind's eight bits in kind_and_flags.
inline constexpr std::uint32_t kInstanceCarriesBakedLight = 1U << 8U;
inline constexpr std::uint32_t kInstanceCastsShadow = 1U << 9U;
// Set by the set in the record of every instance that exists; a record
// without it is an empty slot, which the dispatch and the CPU order skip.
inline constexpr std::uint32_t kInstanceLive = 1U << 10U;
inline constexpr std::uint32_t kInstanceKindMask = 0xFFU;

// An axis-aligned box. Empty when min is above max on any axis.
struct Box {
  math::Vector3f min{math::zero_init};
  math::Vector3f max{math::zero_init};
};

[[nodiscard]] Box world_box(const Box& local, const math::Matrix4f& transform, float pad) noexcept;

// What an instance draws, as scene_geometry.hpp gives it for one draw of a
// registered mesh: the draw's detail levels in the scene's level table, its
// cluster count over every level, its vertex streams with its base vertex,
// and its box in the mesh's space.
struct InstanceGeometry {
  std::uint32_t first_level{};
  std::uint32_t level_count{};
  std::uint32_t cluster_count{};
  geometry::VertexStreams streams{};
  Box bounds{};
};

// What a publisher states on entry. The material fields are the material
// system's: the block's place in the material buffer, the material's blend
// mode and the template's declared maximum displacement.
struct InstanceDescription {
  InstanceGeometry geometry{};
  materials::MaterialReference material{};
  materials::BlendMode blend_mode{materials::BlendMode::Opaque};
  // The index draw_records.hpp's table gave for the instance's template,
  // permutation and blend mode; kNone for an instance the CPU draws
  // (a blended mesh, an emitter, a decal).
  std::uint32_t pipeline{kNone};
  float max_displacement{};
  math::Matrix4f transform{};
  InstanceKind kind{InstanceKind::Mesh};
  // kInstanceCarriesBakedLight, kInstanceCastsShadow.
  std::uint32_t flags{};
  std::int32_t sort_priority{};
};

struct alignas(shaders::kBlockAlignment) InstanceRecord {
  shaders::Float4x4 object_to_world{};
  shaders::Float4x4 previous_object_to_world{};
  shaders::Float3 bounds_center{};
  shaders::Float3 bounds_extent{};
  shaders::Float3 previous_bounds_center{};
  shaders::Float3 previous_bounds_extent{};
  geometry::VertexStreams streams{};
  materials::MaterialReference material{};
  std::uint32_t first_level{};
  std::uint32_t level_count{};
  std::uint32_t pipeline{kNone};
  std::uint32_t blend_mode{};
  std::uint32_t kind_and_flags{};
  std::int32_t sort_priority{};
  std::uint32_t deformed_current{kNone};
  std::uint32_t deformed_previous{kNone};
  std::uint32_t bone_first{kNone};
  std::uint32_t bone_count{};
  std::uint32_t bone_slot{};
  float max_displacement{};
};
inline constexpr std::uint32_t kInstanceRecordBytes = 336;
static_assert(sizeof(InstanceRecord) == kInstanceRecordBytes);

} // namespace bw::scene

using InstanceRecord = bw::scene::InstanceRecord;
BW_PARAMETER_BLOCK_LAYOUT(InstanceRecord, BW_PARAMETER_MEMBER(InstanceRecord, object_to_world),
                          BW_PARAMETER_MEMBER(InstanceRecord, previous_object_to_world),
                          BW_PARAMETER_MEMBER(InstanceRecord, bounds_center),
                          BW_PARAMETER_MEMBER(InstanceRecord, bounds_extent),
                          BW_PARAMETER_MEMBER(InstanceRecord, previous_bounds_center),
                          BW_PARAMETER_MEMBER(InstanceRecord, previous_bounds_extent),
                          BW_PARAMETER_MEMBER(InstanceRecord, streams),
                          BW_PARAMETER_MEMBER(InstanceRecord, material),
                          BW_PARAMETER_MEMBER(InstanceRecord, first_level),
                          BW_PARAMETER_MEMBER(InstanceRecord, level_count),
                          BW_PARAMETER_MEMBER(InstanceRecord, pipeline),
                          BW_PARAMETER_MEMBER(InstanceRecord, blend_mode),
                          BW_PARAMETER_MEMBER(InstanceRecord, kind_and_flags),
                          BW_PARAMETER_MEMBER(InstanceRecord, sort_priority),
                          BW_PARAMETER_MEMBER(InstanceRecord, deformed_current),
                          BW_PARAMETER_MEMBER(InstanceRecord, deformed_previous),
                          BW_PARAMETER_MEMBER(InstanceRecord, bone_first),
                          BW_PARAMETER_MEMBER(InstanceRecord, bone_count),
                          BW_PARAMETER_MEMBER(InstanceRecord, bone_slot),
                          BW_PARAMETER_MEMBER(InstanceRecord, max_displacement));

#endif
