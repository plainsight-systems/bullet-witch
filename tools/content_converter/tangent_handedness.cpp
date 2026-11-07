#include "bw/content_converter/asset_data.hpp"
#include "bw/content_converter/conversion.hpp"
#include "bw/rendering/geometry/mesh_asset.hpp"
#include "bw/rendering/geometry/morph_targets_asset.hpp"
#include "bw/rendering/geometry/vertex_streams.hpp"
#include "bytes.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

// Responsibility: write_tangent_handedness, the rule conversion.hpp states
// under "Meshes, tangent handedness".
//
// Readings of that rule:
//   - A triangle is three consecutive indices of a draw's range, each plus
//     the draw's base vertex. Indices no draw names are not triangles.
//   - h needs the geometric sign as well as the texture sign: a triangle
//     whose dot(cross(P1 - P0, P2 - P0), N) is zero or not finite has no h,
//     like one whose texture area is under 1e-12.
//   - A group whose layout has Tangent but no Position, Normal or texture
//     coordinate set 0 has no triangle with h: every vertex takes -1.
//   - The tangent's w is written as -32767 or +32767; a Tangent stored as
//     another element type than short4n fails the mesh, naming the group,
//     since w cannot hold -1 there.
//   - Two draws of a group whose index ranges overlap fail the mesh, naming
//     the group: an index two draws share could need two remaps.
//   - A draw whose triangles name a copy has its vertex_count widened so
//     that [min_index, min_index + vertex_count) covers every index it
//     names; min_index is kept. The mesh header's draw rule then holds as
//     before.
//   - A draw range past the group's indices, or an index naming a vertex
//     past the group's count, fails the mesh, naming the group.
//   - A morph set of the group gains each copy's delta, in copy order, in
//     every target; a set whose vertex count is not the group's fails the
//     mesh, naming the group.
// Arithmetic is in double: the texture area's products of two floats are
// exact there, so its sign is the same on every host.
namespace bw::content_converter {
namespace {

using geometry::VertexAttribute;
using geometry::VertexElementType;

constexpr double kMinimumTextureArea = 1e-12;
constexpr std::uint32_t kMaxSplitVertices = 65535;
constexpr std::int16_t kSnormOne = 32767;

struct Vec3 {
  double x{};
  double y{};
  double z{};
};

Vec3 operator-(const Vec3& a, const Vec3& b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
Vec3 operator+(const Vec3& a, const Vec3& b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
Vec3 cross(const Vec3& a, const Vec3& b) {
  return {(a.y * b.z) - (a.z * b.y), (a.z * b.x) - (a.x * b.z), (a.x * b.y) - (a.y * b.x)};
}
double dot(const Vec3& a, const Vec3& b) { return (a.x * b.x) + (a.y * b.y) + (a.z * b.z); }

double half_to_double(std::uint32_t h) {
  const std::uint32_t exponent = (h >> 10U) & 0x1FU;
  const double mantissa = static_cast<double>(h & 0x3FFU);
  double magnitude = 0.0;
  if (exponent == 0U) {
    magnitude = std::ldexp(mantissa, -24);
  } else if (exponent == 31U) {
    magnitude = mantissa == 0.0 ? INFINITY : NAN;
  } else {
    magnitude = std::ldexp(1024.0 + mantissa, static_cast<int>(exponent) - 25);
  }
  return (h & 0x8000U) != 0U ? -magnitude : magnitude;
}

double snorm16(std::uint32_t bits) {
  const auto value = static_cast<std::int16_t>(static_cast<std::uint16_t>(bits));
  return std::fmax(-1.0, static_cast<double>(value) / kSnormOne);
}

// One attribute of one group, read as up to three components.
class AttributeReader {
public:
  AttributeReader(const MeshGroupData& group, const geometry::VertexAttributeDescription& attribute)
      : bytes_(attribute.stream == 0 ? group.stream0 : group.stream1),
        stride_(group.layout.streams[attribute.stream].stride), offset_(attribute.offset),
        type_(attribute.type) {}

  [[nodiscard]] Vec3 read(std::uint32_t vertex) const {
    const std::size_t at = (std::size_t{vertex} * stride_) + offset_;
    switch (type_) {
    case VertexElementType::Float3:
      return {bytes::f32(bytes_, at), bytes::f32(bytes_, at + 4U), bytes::f32(bytes_, at + 8U)};
    case VertexElementType::Short4Norm:
      return {snorm16(bytes::u16(bytes_, at)), snorm16(bytes::u16(bytes_, at + 2U)),
              snorm16(bytes::u16(bytes_, at + 4U))};
    case VertexElementType::Half2:
      return {half_to_double(bytes::u16(bytes_, at)), half_to_double(bytes::u16(bytes_, at + 2U)),
              0.0};
    case VertexElementType::UByte4:
      return {static_cast<double>(bytes::u8(bytes_, at)),
              static_cast<double>(bytes::u8(bytes_, at + 1U)),
              static_cast<double>(bytes::u8(bytes_, at + 2U))};
    case VertexElementType::UByte4Norm:
      return {bytes::u8(bytes_, at) / 255.0, bytes::u8(bytes_, at + 1U) / 255.0,
              bytes::u8(bytes_, at + 2U) / 255.0};
    }
    return {};
  }

private:
  std::span<const std::byte> bytes_;
  std::uint32_t stride_;
  std::uint32_t offset_;
  VertexElementType type_;
};

std::optional<geometry::VertexAttributeDescription> find_attribute(const MeshGroupData& group,
                                                                   VertexAttribute attribute) {
  for (std::uint8_t a = 0; a < group.layout.attribute_count; ++a) {
    if (group.layout.attributes[a].attribute == attribute) {
      return group.layout.attributes[a];
    }
  }
  return std::nullopt;
}

int sign_of(double value) { return value > 0.0 ? 1 : (value < 0.0 ? -1 : 0); }

// The triangle's h, or 0 when it has none.
struct TriangleInputs {
  std::optional<AttributeReader> position;
  std::optional<AttributeReader> normal;
  std::optional<AttributeReader> texcoord;
};
int handedness_of(const TriangleInputs& in, const std::array<std::uint32_t, 3>& v) {
  if (!in.position || !in.normal || !in.texcoord) {
    return 0;
  }
  const Vec3 t0 = in.texcoord->read(v[0]);
  const Vec3 t1 = in.texcoord->read(v[1]);
  const Vec3 t2 = in.texcoord->read(v[2]);
  for (const Vec3& t : {t0, t1, t2}) {
    if (!std::isfinite(t.x) || !std::isfinite(t.y)) {
      return 0;
    }
  }
  const double area = ((t1.x - t0.x) * (t2.y - t0.y)) - ((t2.x - t0.x) * (t1.y - t0.y));
  if (!(std::fabs(area) >= kMinimumTextureArea)) {
    return 0;
  }
  const Vec3 p0 = in.position->read(v[0]);
  const Vec3 n = in.normal->read(v[0]) + in.normal->read(v[1]) + in.normal->read(v[2]);
  const double facing = dot(cross(in.position->read(v[1]) - p0, in.position->read(v[2]) - p0), n);
  if (!std::isfinite(facing)) {
    return 0;
  }
  return sign_of(area) * sign_of(facing);
}

void write_w(std::vector<std::byte>& stream, std::size_t at, std::int16_t w) {
  const auto bits = static_cast<std::uint16_t>(w);
  stream[at] = static_cast<std::byte>(bits & 0xFFU);
  stream[at + 1U] = static_cast<std::byte>(bits >> 8U);
}

// Vertex states, as bits: a corner of w -1 was seen, a corner of +1.
constexpr std::uint8_t kSeenMinus = 1U;
constexpr std::uint8_t kSeenPlus = 2U;

TangentHandednessResult write_group(MeshAssetData& mesh, std::uint32_t g,
                                    std::vector<MorphTargetsData>& morph_sets) {
  using E = TangentHandednessError;
  MeshGroupData& group = mesh.groups[g];
  const std::optional<geometry::VertexAttributeDescription> tangent =
      find_attribute(group, VertexAttribute::Tangent);
  if (!tangent) {
    return {};
  }
  if (tangent->type != VertexElementType::Short4Norm) {
    return {E::TangentTypeUnsupported, g};
  }
  TriangleInputs inputs;
  if (const auto p = find_attribute(group, VertexAttribute::Position)) {
    inputs.position.emplace(group, *p);
  }
  if (const auto n = find_attribute(group, VertexAttribute::Normal)) {
    inputs.normal.emplace(group, *n);
  }
  if (const auto t = find_attribute(group, VertexAttribute::TexCoord0)) {
    inputs.texcoord.emplace(group, *t);
  }

  const std::uint32_t original = group.vertex_count;
  std::vector<std::uint8_t> seen(original, 0U);
  // Per index slot: the h of the triangle that holds it (0 when none), and
  // which draw claimed it.
  std::vector<std::int8_t> slot_h(group.indices.size(), 0);
  std::vector<std::uint8_t> claimed(group.indices.size(), 0U);
  for (std::uint32_t d = group.first_draw; d < group.first_draw + group.draw_count; ++d) {
    const geometry::MeshDrawRecord& draw = mesh.draws[d];
    if (std::size_t{draw.first_index} + draw.index_count > group.indices.size()) {
      return {E::DrawOutOfRange, g};
    }
    for (std::uint32_t i = draw.first_index; i + 2U < draw.first_index + draw.index_count;
         i += 3U) {
      if (claimed[i] != 0U || claimed[i + 1U] != 0U || claimed[i + 2U] != 0U) {
        return {E::DrawRangesOverlap, g};
      }
      claimed[i] = claimed[i + 1U] = claimed[i + 2U] = 1U;
      const std::array<std::uint32_t, 3> v{draw.base_vertex + group.indices[i],
                                           draw.base_vertex + group.indices[i + 1U],
                                           draw.base_vertex + group.indices[i + 2U]};
      if (v[0] >= original || v[1] >= original || v[2] >= original) {
        return {E::DrawOutOfRange, g};
      }
      const int h = handedness_of(inputs, v);
      if (h == 0) {
        continue;
      }
      for (std::uint32_t c = 0; c < 3U; ++c) {
        slot_h[i + c] = static_cast<std::int8_t>(h);
        seen[v[c]] |= h > 0 ? kSeenMinus : kSeenPlus;
      }
    }
  }

  // Copies, in the order the originals appear.
  std::vector<std::uint32_t> copy_of(original, 0U);
  std::vector<std::uint32_t> copied;
  for (std::uint32_t v = 0; v < original; ++v) {
    if (seen[v] == (kSeenMinus | kSeenPlus)) {
      copy_of[v] = original + static_cast<std::uint32_t>(copied.size());
      copied.push_back(v);
    }
  }
  if (!copied.empty() && original + copied.size() > kMaxSplitVertices) {
    return {E::VertexCountOverflow, g};
  }
  for (const MorphTargetsData& set : morph_sets) {
    if (set.group == g && set.vertex_count != original) {
      return {E::MorphVertexCountMismatch, g};
    }
  }

  // Every stream of a copied vertex repeated, then every w written.
  for (std::uint8_t s = 0; s < group.layout.stream_count; ++s) {
    std::vector<std::byte>& stream = s == 0 ? group.stream0 : group.stream1;
    const std::size_t stride = group.layout.streams[s].stride;
    stream.reserve(stream.size() + (copied.size() * stride));
    for (const std::uint32_t v : copied) {
      const std::size_t from = v * stride;
      for (std::size_t b = 0; b < stride; ++b) {
        stream.push_back(stream[from + b]);
      }
    }
  }
  group.vertex_count = original + static_cast<std::uint32_t>(copied.size());
  std::vector<std::byte>& tangent_stream = tangent->stream == 0 ? group.stream0 : group.stream1;
  const std::size_t tangent_stride = group.layout.streams[tangent->stream].stride;
  for (std::uint32_t v = 0; v < group.vertex_count; ++v) {
    const bool plus = v >= original || seen[v] == kSeenPlus;
    write_w(tangent_stream, (v * tangent_stride) + tangent->offset + 6U,
            static_cast<std::int16_t>(plus ? kSnormOne : -kSnormOne));
  }

  // Indices of the mirrored triangles name the copies; draw ranges cover
  // what they name.
  for (std::uint32_t d = group.first_draw; d < group.first_draw + group.draw_count; ++d) {
    geometry::MeshDrawRecord& draw = mesh.draws[d];
    std::uint32_t highest = 0;
    bool remapped = false;
    for (std::uint32_t i = draw.first_index; i < draw.first_index + draw.index_count; ++i) {
      const std::uint32_t v = draw.base_vertex + group.indices[i];
      if (slot_h[i] < 0 && copy_of[v] != 0U) {
        group.indices[i] = static_cast<std::uint16_t>(copy_of[v] - draw.base_vertex);
        remapped = true;
      }
      highest = std::max<std::uint32_t>(highest, group.indices[i]);
    }
    if (remapped && highest + 1U > draw.min_index + draw.vertex_count) {
      draw.vertex_count = highest + 1U - draw.min_index;
    }
  }

  for (MorphTargetsData& set : morph_sets) {
    if (set.group != g) {
      continue;
    }
    for (MorphTargetData& target : set.targets) {
      target.deltas.reserve(target.deltas.size() + (copied.size() * geometry::kMorphDeltaBytes));
      for (const std::uint32_t v : copied) {
        const std::size_t from = std::size_t{v} * geometry::kMorphDeltaBytes;
        for (std::size_t b = 0; b < geometry::kMorphDeltaBytes; ++b) {
          target.deltas.push_back(target.deltas[from + b]);
        }
      }
    }
    set.vertex_count = group.vertex_count;
  }
  return {};
}

} // namespace

TangentHandednessResult write_tangent_handedness(MeshAssetData& mesh,
                                                 std::vector<MorphTargetsData>& morph_sets) {
  for (std::uint32_t g = 0; g < mesh.groups.size(); ++g) {
    const TangentHandednessResult failure = write_group(mesh, g, morph_sets);
    if (!failure.ok()) {
      return failure;
    }
  }
  return {};
}

} // namespace bw::content_converter
