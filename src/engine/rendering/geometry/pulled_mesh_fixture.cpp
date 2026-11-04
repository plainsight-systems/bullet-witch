#include "bw/rendering/geometry/pulled_mesh_fixture.hpp"

#include "bw/rendering/geometry/vertex_streams.hpp"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <vector>

// Responsibility: the out-of-line part of
// bw/rendering/geometry/pulled_mesh_fixture.hpp: the generated cube.
//
// Contract (the header's): 24 vertices, four per face; 36 16-bit indices, two
// triangles per face, wound counter-clockwise seen from outside; unit
// half-extent centered at the origin. Stream 1 holds position float3 at
// stride 12; stream 0 holds normal short4n at 0 (w 32767, the content's unit
// w), texture coordinate 1 half2 at 8, color 0 ubyte4n at 12, bone slots
// ubyte4 at 16 and bone weights ubyte4n at 20, stride 24. Normals are the
// face normals, colors one per face. Bytes are written little-endian, the
// order every target's raw buffer load reads.
//
// Algorithm: for each face, a normal n and two in-plane axes u and v with
// u x v = n. The corners n + a*u + b*v for (a, b) in (-1, -1), (1, -1),
// (1, 1), (-1, 1) are then counter-clockwise seen from the +n side, that is
// from outside, and the triangles (0, 1, 2) and (0, 2, 3) of the face keep
// that order. The texture coordinate of a corner is ((a + 1) / 2, (b + 1) /
// 2). The vertex is rigid: bone slot 0 with weight 1.
//
// Design and cost: built once per call into three vectors of the exact final
// size; no other allocation.
//
// Core guidelines read: I.1 (the cube is returned, not a global), R.1 (the
// vectors own the bytes), ES.46 (every narrowing is an explicit cast of a
// value known to fit). Performance guidelines read: CDSA.32 (the bytes are
// written once in the layout the shader reads).
namespace bw::geometry {
namespace {

constexpr std::uint32_t kStream0Stride = 24;
constexpr std::uint32_t kStream1Stride = 12;
constexpr std::uint32_t kFaces = 6;
constexpr std::uint32_t kCornersPerFace = 4;
constexpr std::int32_t kSnormOne = 32767;

struct Vec3 {
  float x{};
  float y{};
  float z{};
};

struct Face {
  Vec3 normal{};
  Vec3 u{};
  Vec3 v{};
  std::array<std::uint8_t, 4> color{};
};

// u x v = normal for every face (y x z = x, z x y = -x, z x x = y,
// x x z = -y, x x y = z, y x x = -z).
constexpr std::array<Face, kFaces> kCubeFaces{{
    {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}, {230, 70, 60, 255}},
    {{-1, 0, 0}, {0, 0, 1}, {0, 1, 0}, {60, 200, 90, 255}},
    {{0, 1, 0}, {0, 0, 1}, {1, 0, 0}, {70, 110, 230, 255}},
    {{0, -1, 0}, {1, 0, 0}, {0, 0, 1}, {235, 200, 60, 255}},
    {{0, 0, 1}, {1, 0, 0}, {0, 1, 0}, {200, 80, 210, 255}},
    {{0, 0, -1}, {0, 1, 0}, {1, 0, 0}, {80, 210, 220, 255}},
}};

// The two triangles of a face, as corner numbers.
constexpr std::array<std::uint16_t, 6> kFaceTriangles{0, 1, 2, 0, 2, 3};

constexpr std::array<std::array<float, 2>, kCornersPerFace> kCorners{{
    {-1.0F, -1.0F},
    {1.0F, -1.0F},
    {1.0F, 1.0F},
    {-1.0F, 1.0F},
}};

// IEEE 754 binary32 to binary16, rounding to nearest with ties to even.
// Overflow gives infinity, a NaN stays a quiet NaN, and results below the
// smallest normal half are rounded into the subnormal range or to zero.
std::uint16_t float_to_half(float value) {
  const auto bits = std::bit_cast<std::uint32_t>(value);
  const auto sign = static_cast<std::uint16_t>((bits >> 16U) & 0x8000U);
  const std::uint32_t exponent = (bits >> 23U) & 0xFFU;
  std::uint32_t mantissa = bits & 0x7FFFFFU;
  if (exponent == 0xFFU) {
    // Infinity, or a NaN with the quiet bit set.
    return static_cast<std::uint16_t>(sign | 0x7C00U | (mantissa != 0U ? 0x0200U : 0U));
  }
  // The exponent rebased from bias 127 to bias 15.
  const std::int32_t half_exponent = static_cast<std::int32_t>(exponent) - 127 + 15;
  if (half_exponent >= 0x1F) {
    return static_cast<std::uint16_t>(sign | 0x7C00U);
  }
  if (half_exponent <= 0) {
    // Subnormal half or zero: shift the full significand (implicit bit
    // included) right so that it counts units of 2^-24.
    if (half_exponent < -10) {
      return sign;
    }
    mantissa |= 0x800000U;
    const auto shift = static_cast<std::uint32_t>(14 - half_exponent);
    const std::uint32_t truncated = mantissa >> shift;
    const std::uint32_t remainder = mantissa & ((1U << shift) - 1U);
    const std::uint32_t halfway = 1U << (shift - 1U);
    const std::uint32_t rounded =
        truncated +
        ((remainder > halfway || (remainder == halfway && (truncated & 1U) != 0U)) ? 1U : 0U);
    // A carry out of the subnormal range lands on the smallest normal, which
    // is the correct encoding.
    return static_cast<std::uint16_t>(sign | rounded);
  }
  // Normal half: keep 10 mantissa bits, round on the 13 dropped.
  const std::uint32_t truncated =
      (static_cast<std::uint32_t>(half_exponent) << 10U) | (mantissa >> 13U);
  const std::uint32_t remainder = mantissa & 0x1FFFU;
  const std::uint32_t rounded =
      truncated +
      ((remainder > 0x1000U || (remainder == 0x1000U && (truncated & 1U) != 0U)) ? 1U : 0U);
  // A carry into the exponent is correct, including the one to infinity.
  return static_cast<std::uint16_t>(sign | rounded);
}

class ByteWriter {
public:
  explicit ByteWriter(std::vector<std::byte>& bytes) : bytes_(bytes) {}
  void u8(std::uint8_t value) { bytes_.push_back(static_cast<std::byte>(value)); }
  void u16(std::uint16_t value) {
    u8(static_cast<std::uint8_t>(value & 0xFFU));
    u8(static_cast<std::uint8_t>(value >> 8U));
  }
  void u32(std::uint32_t value) {
    u16(static_cast<std::uint16_t>(value & 0xFFFFU));
    u16(static_cast<std::uint16_t>(value >> 16U));
  }
  void f32(float value) { u32(std::bit_cast<std::uint32_t>(value)); }
  void snorm16(float value) {
    // Face normal components are exactly -1, 0 or 1.
    u16(static_cast<std::uint16_t>(static_cast<std::int16_t>(value * kSnormOne)));
  }

private:
  std::vector<std::byte>& bytes_;
};

VertexStreamLayout cube_layout() {
  VertexStreamLayout layout{};
  layout.attributes[0] = {VertexAttribute::Position, VertexElementType::Float3, 1, 0};
  layout.attributes[1] = {VertexAttribute::Normal, VertexElementType::Short4Norm, 0, 0};
  layout.attributes[2] = {VertexAttribute::TexCoord1, VertexElementType::Half2, 0, 8};
  layout.attributes[3] = {VertexAttribute::Color0, VertexElementType::UByte4Norm, 0, 12};
  layout.attributes[4] = {VertexAttribute::BoneSlots, VertexElementType::UByte4, 0, 16};
  layout.attributes[5] = {VertexAttribute::BoneWeights, VertexElementType::UByte4Norm, 0, 20};
  layout.attribute_count = 6;
  layout.streams[0] = {kStream0Stride};
  layout.streams[1] = {kStream1Stride};
  layout.stream_count = 2;
  return layout;
}

} // namespace

FixtureCube make_fixture_cube() {
  FixtureCube cube{};
  cube.layout = cube_layout();
  cube.vertex_count = kFixtureCubeVertices;
  cube.index_count = kFixtureCubeIndices;
  cube.stream0.reserve(static_cast<std::size_t>(kFixtureCubeVertices) * kStream0Stride);
  cube.stream1.reserve(static_cast<std::size_t>(kFixtureCubeVertices) * kStream1Stride);
  cube.indices.reserve(kFixtureCubeIndices);
  ByteWriter stream0{cube.stream0};
  ByteWriter stream1{cube.stream1};
  for (std::uint32_t f = 0; f < kFaces; ++f) {
    const Face& face = kCubeFaces[f];
    for (const auto& corner : kCorners) {
      const float a = corner[0];
      const float b = corner[1];
      stream1.f32(face.normal.x + a * face.u.x + b * face.v.x);
      stream1.f32(face.normal.y + a * face.u.y + b * face.v.y);
      stream1.f32(face.normal.z + a * face.u.z + b * face.v.z);

      stream0.snorm16(face.normal.x);
      stream0.snorm16(face.normal.y);
      stream0.snorm16(face.normal.z);
      stream0.u16(static_cast<std::uint16_t>(kSnormOne));
      stream0.u16(float_to_half((a + 1.0F) * 0.5F));
      stream0.u16(float_to_half((b + 1.0F) * 0.5F));
      for (const std::uint8_t channel : face.color) {
        stream0.u8(channel);
      }
      stream0.u32(0U);
      stream0.u32(0x000000FFU);
    }
    const auto base = static_cast<std::uint16_t>(f * kCornersPerFace);
    for (const std::uint16_t corner : kFaceTriangles) {
      cube.indices.push_back(static_cast<std::uint16_t>(base + corner));
    }
  }
  return cube;
}

} // namespace bw::geometry
