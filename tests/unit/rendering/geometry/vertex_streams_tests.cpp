// Unit test: public contracts of bw/rendering/geometry/vertex_streams.hpp and
// pulled_mesh_fixture.hpp; each scenario names the declaration it asserts.
#include "bw/rendering/geometry/pulled_mesh_fixture.hpp"
#include "bw/rendering/geometry/vertex_streams.hpp"
#include "bw/rendering/shaders/parameter_block.hpp"
#include "bw/rendering/shaders/permutation.hpp"
#include "bw/testing/harness.hpp"

#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace {

using bw::geometry::VertexAttribute;
using bw::geometry::VertexAttributeDescription;
using bw::geometry::VertexElementType;
using bw::geometry::VertexStreamLayout;
using bw::testing::require;
using E = bw::geometry::VertexStreamError;

// A layout from per-stream strides (a zero second stride means one stream)
// and its attributes in declaration order.
VertexStreamLayout make_layout(std::uint32_t stride0, std::uint32_t stride1,
                               std::initializer_list<VertexAttributeDescription> attributes) {
  VertexStreamLayout layout{};
  for (const VertexAttributeDescription& a : attributes) {
    layout.attributes[layout.attribute_count++] = a;
  }
  layout.streams[0].stride = stride0;
  layout.streams[1].stride = stride1;
  layout.stream_count = stride1 == 0U ? 1U : 2U;
  return layout;
}

constexpr VertexElementType F3 = VertexElementType::Float3;
constexpr VertexElementType S4N = VertexElementType::Short4Norm;
constexpr VertexElementType H2 = VertexElementType::Half2;
constexpr VertexElementType U4 = VertexElementType::UByte4;
constexpr VertexElementType U4N = VertexElementType::UByte4Norm;
constexpr VertexAttribute POS = VertexAttribute::Position;
constexpr VertexAttribute NRM = VertexAttribute::Normal;
constexpr VertexAttribute COL0 = VertexAttribute::Color0;
constexpr VertexAttribute TC1 = VertexAttribute::TexCoord1;
constexpr VertexAttribute BW0 = VertexAttribute::BoneSlots;
constexpr VertexAttribute BW1 = VertexAttribute::BoneWeights;

// (vertex_streams.hpp, validate_vertex_stream_layout, VertexStreamError,
// element_bytes): each named failure from a layout that breaks that rule
// alone; where a layout breaks two, the one earlier in the enumeration wins.
// Each element type ends exactly at a 16-byte stride (12, 8, 4, 4, 4 bytes)
// and overruns it 4 bytes later.
void layout_errors_named_in_order() {
  const VertexStreamLayout valid = make_layout(12, 0, {{POS, F3, 0, 0}});
  const auto counts = [](VertexStreamLayout l, std::pair<std::uint32_t, std::uint32_t> replaced) {
    l.attribute_count = static_cast<std::uint8_t>(replaced.first);
    l.stream_count = static_cast<std::uint8_t>(replaced.second);
    return l;
  };
  struct Row {
    VertexStreamLayout layout;
    E expected;
    std::string_view note;
  };
  const std::array rows{
      Row{counts(valid, {0, 0}), E::NoAttributes, "no attributes, before NoStreams"},
      Row{counts(valid, {bw::geometry::kVertexAttributeCount + 1U, 0}), E::TooManyAttributes,
          "more attributes than slots, before NoStreams"},
      Row{counts(valid, {1, 0}), E::NoStreams, "no streams"},
      Row{counts(valid, {1, bw::geometry::kMaxVertexStreams + 1U}), E::TooManyStreams,
          "three streams"},
      Row{make_layout(24, 0, {{POS, F3, 0, 0}, {POS, F3, 0, 12}}), E::DuplicateAttribute,
          "a slot named twice"},
      Row{counts(make_layout(12, 13, {{POS, F3, 1, 0}}), {1, 1}), E::StreamOutOfRange,
          "a stream past stream_count, before StrideMisaligned"},
      Row{make_layout(0, 0, {{POS, F3, 0, 0}}), E::StrideMisaligned, "a zero stride"},
      Row{make_layout(14, 0, {{POS, F3, 0, 2}}), E::StrideMisaligned,
          "a stride not a multiple of 4, before OffsetMisaligned"},
      Row{make_layout(12, 0, {{POS, F3, 0, 2}}), E::OffsetMisaligned,
          "an offset not a multiple of 4, before AttributeOverrunsStride"},
      Row{make_layout(12, 0, {{POS, F3, 0, bw::geometry::kMaxAttributeOffset + 1U}}),
          E::OffsetOutOfRange, "an offset above 16 bits, before overrun"},
      Row{make_layout(12, 0, {{POS, F3, 0, 4}}), E::AttributeOverrunsStride,
          "a float3 at 4 in a 12-byte stride"},
      Row{make_layout(12, 12, {{POS, F3, 0, 0}}), E::StreamUnused,
          "a declared stream nothing reads"},
  };
  for (std::size_t i = 0; i < rows.size(); ++i) {
    require(bw::geometry::validate_vertex_stream_layout(rows[i].layout) == rows[i].expected, i,
            rows[i].note);
  }
  for (const auto& [type, bytes] : {std::pair{F3, 12U}, std::pair{S4N, 8U}, std::pair{H2, 4U},
                                    std::pair{U4, 4U}, std::pair{U4N, 4U}}) {
    const auto at = [type](std::uint32_t offset) {
      return bw::geometry::validate_vertex_stream_layout(
          make_layout(16, 0, {{POS, type, 0, offset}}));
    };
    require(at(16 - bytes) == E::None && at(20 - bytes) == E::AttributeOverrunsStride,
            "type " + std::to_string(static_cast<int>(type)) +
                " ends at the stride and overruns it 4 bytes later");
  }
}

// (vertex_streams.hpp, validate_vertex_range): for every declared stream,
// (base_vertex + vertex_count) * stride must fit the stream's bytes, the
// count is at least 1, and stream_bytes holds an entry per stream; anything
// else is VertexRangeOverrunsBuffer.
void vertex_range_overrun_named() {
  const VertexStreamLayout layout = make_layout(24, 12, {{POS, F3, 1, 0}, {NRM, S4N, 0, 0}});
  constexpr std::uint64_t kEnd0 = 15ULL * 24ULL;
  constexpr std::uint64_t kEnd1 = 15ULL * 12ULL;
  struct Row {
    std::array<std::uint64_t, 2> bytes;
    std::size_t entries;
    std::uint32_t base;
    std::uint32_t count;
    E expected;
    std::string_view note;
  };
  constexpr E kOverrun = E::VertexRangeOverrunsBuffer;
  constexpr std::array rows{
      Row{{kEnd0, kEnd1}, 2, 5, 10, E::None, "ending at both last bytes"},
      Row{{kEnd0, kEnd1 - 1U}, 2, 5, 10, kOverrun, "one byte short in stream 1"},
      Row{{kEnd0 - 1U, kEnd1}, 2, 5, 10, kOverrun, "one byte short in stream 0"},
      Row{{kEnd0, kEnd1}, 2, 6, 10, kOverrun, "the base vertex moves the range past the end"},
      Row{{kEnd0, kEnd1}, 2, 0, 0, kOverrun, "a zero count"},
      Row{{kEnd0, kEnd1}, 1, 0, 1, kOverrun, "fewer byte sizes than streams"},
      Row{{kEnd0, kEnd1}, 2, 0xFFFFFFFFU, 1, kOverrun, "a base vertex at 2^32 - 1 does not wrap"},
  };
  for (std::size_t i = 0; i < rows.size(); ++i) {
    require(bw::geometry::validate_vertex_range(layout,
                                                std::span(rows[i].bytes).first(rows[i].entries),
                                                rows[i].base, rows[i].count) == rows[i].expected,
            i, rows[i].note);
  }
}

std::uint32_t slot_word(const VertexStreams& block, VertexAttribute attribute) {
  const auto slot = static_cast<std::size_t>(attribute);
  const bw::shaders::Uint4& row = block.attributes[slot / 4U];
  return std::array<std::uint32_t, 4>{row.x, row.y, row.z, row.w}[slot % 4U];
}

// (vertex_streams.hpp, make_vertex_streams, attribute_word, kAbsentAttribute,
// kVertexStreamsBytes): the shader system's block is 80 bytes; each named
// slot carries its word (offset low 16 bits, stream high 16) at row slot / 4,
// component slot % 4, every other slot kAbsentAttribute; the streams, the
// stream count and the base vertex are the inputs; one stream leaves stream 1
// zero.
void block_fill_and_absent_slots() {
  require(bw::shaders::BlockLayoutOf<VertexStreams>::value.size == 80U, "size 80");
  require(bw::geometry::attribute_word(VertexAttributeDescription{TC1, H2, 1, 8}) == 0x00010008U,
          "stream 1 offset 8 is 0x00010008");
  const VertexStreamLayout two = make_layout(
      24, 12, {{POS, F3, 1, 0}, {NRM, S4N, 0, 0}, {COL0, U4N, 0, 12}, {BW1, U4N, 0, 20}});
  const std::array<bw::geometry::VertexStreamBinding, 2> bindings{
      bw::geometry::VertexStreamBinding{bw::shaders::BufferIndex{7}, 100},
      bw::geometry::VertexStreamBinding{bw::shaders::BufferIndex{9}, 90}};
  const VertexStreams block = bw::geometry::make_vertex_streams(two, bindings, 33);
  require(block.buffer0.value == 7U && block.stride0 == 24U && block.count0 == 100U &&
              block.buffer1.value == 9U && block.stride1 == 12U && block.count1 == 90U &&
              block.stream_count == 2U && block.base_vertex == 33U,
          "each stream's index, stride and count, the stream count and the base vertex");
  for (std::uint32_t slot = 0; slot < bw::geometry::kVertexAttributeCount; ++slot) {
    std::uint32_t expected = bw::geometry::kAbsentAttribute;
    for (std::size_t i = 0; i < two.attribute_count; ++i) {
      if (static_cast<std::uint32_t>(two.attributes[i].attribute) == slot) {
        expected = bw::geometry::attribute_word(two.attributes[i]);
      }
    }
    require(slot_word(block, static_cast<VertexAttribute>(slot)) == expected,
            "slot " + std::to_string(slot) + " carries its word or kAbsentAttribute");
  }
  const VertexStreams single = bw::geometry::make_vertex_streams(
      make_layout(12, 0, {{POS, F3, 0, 0}}), std::span(bindings).first(1), 0);
  require(single.buffer1.value == 0U && single.stride1 == 0U && single.count1 == 0U &&
              single.stream_count == 1U,
          "a one-stream layout leaves stream 1 zero");
  require(slot_word(single, POS) == 0U, "position at stream 0 offset 0 is word 0");
}

std::uint32_t read_u32(std::span<const std::byte> bytes, std::size_t offset) {
  std::uint32_t value = 0;
  for (std::size_t i = 0; i < 4; ++i) {
    value |= static_cast<std::uint32_t>(bytes[offset + i]) << (8U * i);
  }
  return value;
}
std::int16_t read_i16(std::span<const std::byte> bytes, std::size_t offset) {
  return static_cast<std::int16_t>(read_u32(bytes, offset) & 0xFFFFU);
}

// (pulled_mesh_fixture.hpp, make_fixture_cube and the cube): the stated
// layout, which validates; 24 vertices and 36 indices below 24, the buffers
// exactly 24 vertices at the strides; positions are -1 or 1; each triangle's
// normal (p1 - p0) x (p2 - p0) points away from the center
// (counter-clockwise from outside), and its vertices carry that normal as
// short4n with w 32767 and one color.
void cube_as_stated() {
  const bw::geometry::FixtureCube cube = bw::geometry::make_fixture_cube();
  const VertexStreamLayout stated = make_layout(24, 12,
                                                {{POS, F3, 1, 0},
                                                 {NRM, S4N, 0, 0},
                                                 {TC1, H2, 0, 8},
                                                 {COL0, U4N, 0, 12},
                                                 {BW0, U4, 0, 16},
                                                 {BW1, U4N, 0, 20}});
  require(cube.layout == stated, "the cube's layout is the stated one");
  require(bw::geometry::validate_vertex_stream_layout(cube.layout) == E::None,
          "the cube's layout validates");
  require(cube.vertex_count == 24U && cube.index_count == 36U && cube.indices.size() == 36U &&
              cube.stream0.size() == std::size_t{24} * 24U &&
              cube.stream1.size() == std::size_t{24} * 12U,
          "24 vertices, 36 indices, the buffers exactly 24 vertices at the strides");
  for (std::size_t i = 0; i < cube.stream1.size(); i += 4U) {
    const float p = std::bit_cast<float>(read_u32(cube.stream1, i));
    require(p == 1.0F || p == -1.0F, "every position component is -1 or 1");
  }
  for (std::size_t t = 0; t < 12; ++t) {
    std::array<std::array<float, 3>, 3> p{};
    std::array<std::uint16_t, 3> index{};
    for (std::size_t k = 0; k < 3; ++k) {
      index[k] = cube.indices[t * 3U + k];
      require(index[k] < 24U, "every index is below 24");
      for (std::size_t c = 0; c < 3; ++c) {
        p[k][c] =
            std::bit_cast<float>(read_u32(cube.stream1, std::size_t{index[k]} * 12U + c * 4U));
      }
    }
    const auto d = [&p](std::size_t k, std::size_t c) { return p[k][c] - p[0][c]; };
    const std::array<float, 3> n{d(1, 1) * d(2, 2) - d(1, 2) * d(2, 1),
                                 d(1, 2) * d(2, 0) - d(1, 0) * d(2, 2),
                                 d(1, 0) * d(2, 1) - d(1, 1) * d(2, 0)};
    const float outward = n[0] * (p[0][0] + p[1][0] + p[2][0]) +
                          n[1] * (p[0][1] + p[1][1] + p[2][1]) +
                          n[2] * (p[0][2] + p[1][2] + p[2][2]);
    require(outward > 0.0F,
            "triangle " + std::to_string(t) + " is counter-clockwise seen from outside");
    const float length = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
    const std::uint32_t color = read_u32(cube.stream0, std::size_t{index[0]} * 24U + 12U);
    for (std::size_t k = 0; k < 3; ++k) {
      const std::size_t base = std::size_t{index[k]} * 24U;
      for (std::size_t c = 0; c < 4; ++c) {
        const float expected = c == 3U ? 32767.0F : n[c] / length * 32767.0F;
        require(read_i16(cube.stream0, base + c * 2U) == static_cast<std::int16_t>(expected),
                "the vertex normal is the face normal as short4n with w 32767");
      }
      require(read_u32(cube.stream0, base + 12U) == color, "one color per face");
    }
  }
}

// (pulled_mesh_fixture.hpp, Contract, Shaders) through (permutation.hpp,
// find_shader): each fixture shader's stage, entry point main, dimension and
// permutation counts (the probe one per element type) and draw block.
void registry_entries_name_fixture_blocks() {
  struct Row {
    std::string_view name;
    bw::shaders::ShaderStage stage;
    std::uint32_t dimensions;
    std::uint32_t permutations;
    const bw::shaders::BlockLayout* block;
  };
  const std::array rows{
      Row{bw::geometry::kPulledMeshShader, bw::shaders::ShaderStage::Vertex, 0, 1,
          &bw::shaders::BlockLayoutOf<PulledMeshDraw>::value},
      Row{bw::geometry::kVertexDecodeProbeShader, bw::shaders::ShaderStage::Vertex, 1,
          bw::geometry::kVertexElementTypeCount,
          &bw::shaders::BlockLayoutOf<VertexDecodeProbeDraw>::value},
      Row{bw::geometry::kVertexColorShader, bw::shaders::ShaderStage::Pixel, 0, 1, nullptr},
  };
  for (const Row& row : rows) {
    const bw::shaders::ShaderDescription* shader =
        bw::shaders::find_shader(bw::shaders::shader_registry(), row.name);
    require(shader != nullptr && shader->stage == row.stage && shader->entry_point == "main" &&
                shader->dimension_count == row.dimensions &&
                shader->permutation_count == row.permutations && shader->draw_block == row.block,
            std::string(row.name) + " has its stage, entry point, permutations and block");
  }
}

using Scenario = bw::testing::Scenario<void>;
constexpr std::array kScenarios{
    Scenario{"layout_errors_named_in_order", &layout_errors_named_in_order},
    Scenario{"vertex_range_overrun_named", &vertex_range_overrun_named},
    Scenario{"block_fill_and_absent_slots", &block_fill_and_absent_slots},
    Scenario{"cube_as_stated", &cube_as_stated},
    Scenario{"registry_entries_name_fixture_blocks", &registry_entries_name_fixture_blocks},
};

} // namespace

int main(int argc, char** argv) { return bw::testing::run_scenario(argc, argv, kScenarios); }
