// Explicit untextured fixture models omit UV/storage dependencies, applying
// both-phase I.1 (#ri-explicit) and GPU.1 retained geometry guidance.
// Synthetic original-format model fixtures shared by CPU and native consumers.
// No game bytes or file/process access. Callers supply float3 vertex positions,
// 16-bit indices and six D3D draw fields; defaults form one indexed triangle.
// Algorithm: encode nested state/draw records, then wrap header/vertex/index
// payloads in a bounded CPR with independent index-format metadata. Fixture
// storage uses the existing test-only process heap exception. Cost O(fixture
// bytes), no production performance claim. Both phases consulted SL.con.3
// (#rsl-bounds) for checked writes and MEM.6 (guidelines/memory/
// MEM.6-inject-allocators-behind-one-interface.md); production decode receives
// a test domain, while independent fixture preparation remains test-owned.
#pragma once

#include <bit>
#include <span>

#include "bw/content/formats/common.hpp"

namespace model_fixture {
using bw::resources::Bytes;
inline void word(Bytes& bytes, std::size_t offset, std::uint32_t value) {
  for (unsigned i = 0; i < 4; ++i) {
    bytes.at(offset + i) = static_cast<std::uint8_t>(value >> (i * 8));
  }
}
inline Bytes words(std::initializer_list<std::uint32_t> values) {
  Bytes bytes(values.size() * 4);
  std::size_t cursor = 0;
  for (auto value : values) {
    word(bytes, cursor, value);
    cursor += 4;
  }
  return bytes;
}
inline std::uint32_t tag(std::string_view name) {
  std::uint32_t result = 0;
  for (char ch : name) {
    result = (result << 8) | static_cast<unsigned char>(ch);
  }
  return result;
}
inline void append(Bytes& target, std::span<const std::uint8_t> source) {
  target.insert(target.end(), source.begin(), source.end());
}
inline void record(Bytes& target, std::string_view kind,
                   std::span<const std::uint8_t> body) {
  append(target, words({tag(kind), static_cast<std::uint32_t>(body.size())}));
  append(target, body);
}
inline constexpr std::array<float, 9> kPositions{
    -1.0F, -1.0F, 0.5F, 1.0F, -1.0F, 0.5F, 0.0F, 1.0F, 0.5F};
inline constexpr std::array<std::uint16_t, 3> kIndices{0, 1, 2};
struct Options {
  std::span<const float> positions{kPositions};
  std::span<const std::uint16_t> indices{kIndices};
  std::span<const std::uint8_t> shape_offsets{};
  std::array<std::uint32_t, 6> draw{4, 0, 0, 3, 0, 1};
  std::array<std::uint32_t, 2> shader{0x00420882, 1};
  std::uint32_t group{}, volume{};
  bool normal_mapped{}, base_only{}, secondary_base{}, sky{}, environment{},
      shape{}, untextured{}, joint_palette{}, material_continuation{},
      ambient_occlusion{}, vertex_modulation{};
  std::array<std::uint8_t, 4> joint_indices{0, 1, 0, 0};
  std::array<std::uint8_t, 4> joint_weights{64, 191, 0, 0};
};
inline Bytes archive(const Options& options = {}) {
  Bytes commands;
  // PC D3D declaration: stream0 FLOAT3 POSITION0 followed by the exact
  // terminator.
  if (options.normal_mapped) {
    auto declaration = words(
        {0, options.shape ? 1U : 0U, 2, 0x000c0000,
         options.vertex_modulation ? 0x000a0008U : 0x020a0008U, 0x00100000,
         0x0003000a, 0x00180000, 0x0006000a, 0x00200000, 0x0105000f});
    if (options.secondary_base) {
      append(declaration, words({0x00240000, 0x0205000f}));
    }
    if (options.joint_palette) {
      const auto skin_offset = options.secondary_base ? 40U : 36U;
      append(declaration, words({skin_offset << 16, 0x00010005,
                                 (skin_offset + 4) << 16, 0x01010008}));
    }
    append(declaration, words({255, 17}));
    record(commands, "VDCL", declaration);
  } else if (options.base_only) {
    record(commands, "VDCL",
           words({0, 0, 2, 0x000c0000, 0x020a0008, 0x00100000, 0x0003000a,
                  0x00180000, 0x0105000f, 255, 17}));
  } else if (options.joint_palette) {
    record(commands, "VDCL",
           words({0, 0, 2, 0x000c0000, 0x0003000a, 0x00140000, 0x0105000f,
                  0x00180000, 0x00010005, 0x001c0000, 0x01010008, 255, 17}));
  } else if (options.sky) {
    record(commands, "VDCL",
           words({0, 0, 2, 0x000c0000, 0x0003000a, 0x00140000, 0x0105000f, 255,
                  17}));
  } else if (options.untextured) {
    record(commands, "VDCL", words({0, 0, 2, 0x000c0000, 0x0003000a, 255, 17}));
  } else {
    record(commands, "VDCL", words({0, options.shape ? 1U : 0U, 2, 255, 17}));
  }
  const auto stride =
      options.normal_mapped
          ? (options.joint_palette    ? (options.secondary_base ? 48U : 44U)
             : options.secondary_base ? 40U
                                      : 36U)
      : options.base_only     ? 28U
      : options.joint_palette ? 32U
      : options.sky           ? 24U
      : options.untextured    ? 20U
                              : 12U;
  record(commands, "VTX ", words({0, 0, 0, stride}));
  if (options.shape) {
    record(commands, "VTXS", words({1, 0, 0, 12U}));
  }
  record(commands, "IDX ", words({0}));
  record(commands, "knsS", words({options.shader[0], options.shader[1]}));
  Bytes material;
  record(material, "CF1I",
         words({0, 1, tag("CCOL"), 0, 0xffffffff, 0xffffffff,
                std::bit_cast<std::uint32_t>(1.0F),
                std::bit_cast<std::uint32_t>(0.5F),
                std::bit_cast<std::uint32_t>(0.25F),
                std::bit_cast<std::uint32_t>(1.0F)}));
  if (options.normal_mapped || options.base_only || options.sky ||
      options.joint_palette) {
    auto bindings = words({0,
                           1U + options.normal_mapped + options.secondary_base +
                               options.environment + options.ambient_occlusion,
                           tag("BTX0"), 0, 0, 0});
    if (options.ambient_occlusion) {
      append(bindings, words({tag("AMOC"), 0, 0, 0}));
    }
    if (options.normal_mapped) {
      append(bindings, words({tag("NMAP"), 0, 1, 0}));
    }
    if (options.secondary_base) {
      append(bindings, words({tag("BTX1"), 0, 2, 0}));
    }
    if (options.environment) {
      append(bindings, words({tag("LENV"), 0, 3, 0}));
    }
    record(material, "TR  ", bindings);
  } else {
    record(material, "TR  ", words({0, 0}));
  }
  record(commands, "kns2", material);
  if (options.material_continuation) {
    Bytes continuation;
    record(continuation, "CM1R", words({0, 1, tag("JINT"), 0, 3, 0}));
    record(commands, "knsC", continuation);
  }
  Bytes draw;
  for (auto value : options.draw) {
    append(draw, words({value}));
  }
  auto volume = words({options.volume});
  record(volume, "DIP ", draw);
  auto group = words({options.group});
  record(group, "DVOL", volume);
  record(commands, "DGRP", group);
  Bytes header(96);
  word(header, 0, tag("VFX2"));
  word(header, 92, 12);
  append(header,
         words({0, tag("CNKH"), static_cast<std::uint32_t>(commands.size())}));
  append(header, commands);
  Bytes shape_positions;
  for (float value : options.positions) {
    append(shape_positions, words({std::bit_cast<std::uint32_t>(value)}));
  }
  Bytes vertices;
  for (std::size_t i = 0; i < options.positions.size(); ++i) {
    append(vertices,
           words({std::bit_cast<std::uint32_t>(options.positions[i])}));
    if (i % 3 == 2) {
      // White exponent-zero illumination, +Z normal, +X tangent and zero UV.
      if (options.normal_mapped) {
        append(vertices,
               words({options.vertex_modulation ? 0xffffffffU : 0x80ffffffU, 0,
                      0x00007fff, 0x00007fff, 0, 0}));
        if (options.secondary_base) {
          append(vertices, words({0}));
        }
        if (options.joint_palette) {
          vertices.insert(vertices.end(), options.joint_indices.begin(),
                          options.joint_indices.end());
          vertices.insert(vertices.end(), options.joint_weights.begin(),
                          options.joint_weights.end());
        }
      } else if (options.base_only) {
        append(vertices, words({0x80ffffff, 0, 0x00007fff, 0}));
      } else if (options.joint_palette) {
        append(vertices, words({0, 0x00007fff, 0}));
        vertices.insert(vertices.end(), options.joint_indices.begin(),
                        options.joint_indices.end());
        vertices.insert(vertices.end(), options.joint_weights.begin(),
                        options.joint_weights.end());
      } else if (options.sky) {
        append(vertices, words({0, 0x00007fff, 0}));
      } else if (options.untextured) {
        append(vertices, words({0, 0x00007fff}));
      }
    }
  }
  Bytes indices;
  for (auto index : options.indices) {
    indices.push_back(static_cast<std::uint8_t>(index));
    indices.push_back(static_cast<std::uint8_t>(index >> 8));
  }
  Bytes names;
  std::array<std::uint32_t, 6> offsets{};
  constexpr std::array<std::string_view, 6> labels{
      "header.bin",       "vertex_000.bin",      "index_000.bin",
      "vfx2.mesh.create", "shapevertex_000.bin", "shapeoffset_000.bin"};
  for (std::size_t i = 0; i < labels.size(); ++i) {
    offsets.at(i) = static_cast<std::uint32_t>(names.size());
    names.insert(names.end(), labels.at(i).begin(), labels.at(i).end());
    names.push_back(0);
  }
  const std::size_t record_count =
      options.shape ? (options.shape_offsets.empty() ? 5U : 6U) : 4U;
  const std::size_t table_bytes = record_count * 48;
  const std::size_t gap_bytes = options.shape ? 48U : 32U;
  Bytes output(96 + table_bytes);
  output[0] = 'C';
  output[1] = 'P';
  output[2] = 'R';
  output[3] = '1';
  word(output, 4, 1);
  word(output, 8, 0x11223344);
  word(output, 16, static_cast<std::uint32_t>(table_bytes));
  word(output, 24, static_cast<std::uint32_t>(names.size()));
  word(output, 28, static_cast<std::uint32_t>(gap_bytes));
  word(
      output, 32,
      static_cast<std::uint32_t>(header.size() + options.shape_offsets.size()));
  word(
      output, 36,
      static_cast<std::uint32_t>(vertices.size() + indices.size() +
                                 (options.shape ? shape_positions.size() : 0)));
  word(output, 44,
       static_cast<std::uint32_t>(table_bytes + names.size() + gap_bytes));
  constexpr std::array<std::string_view, 6> kinds{"USER", "DXVB", "DXIB",
                                                  "CALL", "DXVB", "USER"};
  for (std::size_t i = 0; i < record_count; ++i) {
    const auto at = 96 + i * 48;
    for (std::size_t j = 0; j < 4; ++j) {
      output[at + j] = static_cast<std::uint8_t>(kinds.at(i).at(j));
    }
    word(output, at + 4, offsets.at(i));
    word(output, at + 12, 0xffffffff);
  }
  word(output, 96 + 36, static_cast<std::uint32_t>(header.size()));
  word(output, 144 + 24, 0);
  word(output, 144 + 28, 16);
  word(output, 144 + 44, static_cast<std::uint32_t>(vertices.size()));
  word(output, 192 + 24, 16);
  word(output, 192 + 28, 16);
  word(output, 192 + 40, static_cast<std::uint32_t>(vertices.size()));
  word(output, 192 + 44, static_cast<std::uint32_t>(indices.size()));
  if (options.shape) {
    constexpr std::size_t kShapeRecord = 96 + 4 * 48;
    word(output, kShapeRecord + 24, 32);
    word(output, kShapeRecord + 28, 16);
    word(output, kShapeRecord + 40,
         static_cast<std::uint32_t>(vertices.size() + indices.size()));
    word(output, kShapeRecord + 44,
         static_cast<std::uint32_t>(shape_positions.size()));
  }
  if (!options.shape_offsets.empty()) {
    constexpr std::size_t kOffsetRecord = 96 + 5 * 48;
    word(output, kOffsetRecord + 32, static_cast<std::uint32_t>(header.size()));
    word(output, kOffsetRecord + 36,
         static_cast<std::uint32_t>(options.shape_offsets.size()));
  }
  append(output, names);
  output.resize(output.size() + gap_bytes);
  append(output, header);
  append(output, options.shape_offsets);
  append(output, vertices);
  append(output, indices);
  if (options.shape) {
    append(output, shape_positions);
  }
  return output;
}
}  // namespace model_fixture
