// Unit test: public contracts of bw/content_converter/mesh_reader.hpp; each
// scenario names the declaration it asserts. The model container is built
// here from the header's stated framing: chunks with byte-reversed tags and
// sizes that exclude their 8 bytes, the CNKH chain after the count at +0x58,
// the VDCL elements, VTX/VTXS/IDX/DIP words, the CM1R entries, the material
// chunks and the TS2V record. DGRP and DVOL carry one leading word and MCTL
// two, as the content stores them; the header does not state these words.
#include "bw/content_converter/asset_data.hpp"
#include "bw/content_converter/cpr_container.hpp"
#include "bw/content_converter/mesh_reader.hpp"
#include "bw/rendering/geometry/mesh_asset.hpp"
#include "bw/rendering/geometry/vertex_streams.hpp"
#include "bw/testing/harness.hpp"

#include "cpr_fixture.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

namespace cc = bw::content_converter;
namespace g = bw::geometry;
namespace fx = bw::testing::cpr;
using bw::testing::require;
using fx::Bytes;
using Tag = std::array<char, 4>;

Tag tag_of(std::string_view text) { return {text[0], text[1], text[2], text[3]}; }

Bytes words(std::initializer_list<std::uint32_t> values) {
  Bytes bytes;
  for (const std::uint32_t v : values) {
    fx::append_u32(bytes, v);
  }
  return bytes;
}
Bytes counting(std::size_t count, std::uint8_t first) {
  Bytes bytes(count);
  for (std::size_t i = 0; i < count; ++i) {
    bytes[i] = static_cast<std::byte>(first + i);
  }
  return bytes;
}
Bytes join(std::initializer_list<Bytes> parts) {
  Bytes out;
  for (const Bytes& p : parts) {
    out.insert(out.end(), p.begin(), p.end());
  }
  return out;
}
// A chunk: the tag stored last-first, the size of the data, the data.
Bytes chunk(std::string_view tag, const Bytes& data) {
  Bytes out;
  for (std::size_t i = 4; i > 0; --i) {
    out.push_back(static_cast<std::byte>(tag[i - 1]));
  }
  fx::append_u32(out, static_cast<std::uint32_t>(data.size()));
  out.insert(out.end(), data.begin(), data.end());
  return out;
}
// One declaration element: stream, offset, type, method 0, usage, index.
struct Element {
  std::uint32_t stream;
  std::uint32_t offset;
  std::uint32_t type;
  std::uint32_t usage;
  std::uint32_t index;
};
Bytes element(const Element& e) {
  const auto [stream, offset, type, usage, index] = e;
  Bytes out(8);
  out[0] = static_cast<std::byte>(stream & 0xFFU);
  out[1] = static_cast<std::byte>(stream >> 8U);
  out[2] = static_cast<std::byte>(offset & 0xFFU);
  out[3] = static_cast<std::byte>(offset >> 8U);
  out[4] = static_cast<std::byte>(type);
  out[5] = std::byte{0};
  out[6] = static_cast<std::byte>(usage);
  out[7] = static_cast<std::byte>(index);
  return out;
}
Bytes vdcl(std::initializer_list<Bytes> elements) {
  Bytes data = words({0});
  for (const Bytes& e : elements) {
    data.insert(data.end(), e.begin(), e.end());
  }
  const Bytes end = element({0xFF, 0, 17, 0, 0});
  data.insert(data.end(), end.begin(), end.end());
  fx::pad_to(data, 16);
  return chunk("VDCL", data);
}
// CF1I: u32 0, count, then 32-byte records of tag (stored last-first),
// three zero words and four floats.
struct Cf1iSpec {
  std::uint32_t count;
  float base;
};
Bytes cf1i(const Cf1iSpec& spec) {
  const auto [count, base] = spec;
  Bytes data = words({0, count});
  const std::array<std::string_view, 9> tags{"PARF", "CCOL", "FRSA", "FRSB", "FRSR",
                                             "LUMI", "REFP", "SPCC", "ENVP"};
  for (std::size_t r = 0; r < tags.size(); ++r) {
    for (std::size_t i = 4; i > 0; --i) {
      data.push_back(static_cast<std::byte>(tags[r][i - 1]));
    }
    fx::append_u32(data, 0);
    fx::append_u32(data, 0);
    fx::append_u32(data, 0);
    for (std::size_t k = 0; k < 4; ++k) {
      fx::append_f32(data, base + static_cast<float>(r) + (0.25F * static_cast<float>(k)));
    }
  }
  return chunk("CF1I", data);
}
// A list chunk (TR, SSSM, SRS): u32 0, count, 16-byte entries of a tag
// (stored last-first) and three words.
Bytes list(std::string_view tag,
           std::initializer_list<std::pair<std::string_view, Bytes>> entries) {
  Bytes data = words({0, static_cast<std::uint32_t>(entries.size())});
  for (const auto& [entry_tag, entry_words] : entries) {
    for (std::size_t i = 4; i > 0; --i) {
      data.push_back(static_cast<std::byte>(entry_tag[i - 1]));
    }
    data.insert(data.end(), entry_words.begin(), entry_words.end());
  }
  return chunk(tag, data);
}
Bytes mctl(std::initializer_list<Bytes> children) {
  Bytes data = words({0, 0});
  for (const Bytes& c : children) {
    data.insert(data.end(), c.begin(), c.end());
  }
  return chunk("MCTL", data);
}
Bytes holder(std::string_view tag, std::uint32_t word, std::initializer_list<Bytes> children) {
  Bytes data = words({word});
  for (const Bytes& c : children) {
    data.insert(data.end(), c.begin(), c.end());
  }
  return chunk(tag, data);
}
Bytes dip(std::uint32_t type, std::uint32_t base, std::uint32_t count, std::uint32_t first,
          std::uint32_t primitives) {
  return chunk("DIP ", words({type, base, 0, count, first, primitives}));
}

// The knobs the error table turns; the defaults build the good model.
struct Spec {
  std::string header_name = "header.bin";
  std::string magic = "2XFV";
  bool trailing_bytes = false;
  std::uint32_t group1_normal_type = 10;
  std::uint32_t group1_texcoord_offset = 20;
  std::string index1_name = "index_001.bin";
  std::size_t vertex1_extra = 0;
  std::size_t index1_bytes = 6;
  bool dip_before_vdcl = false;
  std::uint32_t dip2_primitives = 1;
  std::uint32_t dip1_type = 4;
  std::uint32_t cm1r_slot1 = 1;
  std::uint32_t cf1i_count1 = 9;
  std::uint32_t ts2v_vertices = 4;
  bool extra_shape_record = false;
  // Group 1's VTX holds three words, too short to name its stride.
  bool short_vtx1 = false;
  // Neither group has a kns2 or knsC holding a CF1I.
  bool no_materials = false;
  // Group 1's VDCL, VTX and IDX sit inside the DGRP that holds its draw.
  bool nested_vdcl1 = false;
};

constexpr std::array<float, 16> kBounds{1.0F, 2.0F, 3.0F, 1.0F, -1.0F, -2.0F, -3.0F, 1.0F,
                                        0.0F, 0.5F, 0.0F, 1.0F, 4.0F,  0.0F,  0.0F,  1.0F};

// Group 0, the first CNKH: position in stream 1 (float3, stride 12); normal
// short4n at 0, texture coordinate 1 half2 at 8, bone slots ubyte4 at 12,
// bone weights ubyte4n at 16 in stream 0 (stride 20); 4 vertices, 6
// indices; a kns2 material with CF1I, TR (BTX0 0, NMAP 1), SSSM and SRS;
// GENV; a DGRP holding a DVOL with a bone map and a draw, and a DVOL with a
// draw and no map. Group 1, the second CNKH: position float3 at 0, normal
// short4n at 12, texture coordinate 1 half2 at 20 (stride 24); 3 vertices,
// 3 indices; a knsC material with no knsS of its own; TANM; a DGRP holding
// one draw directly.
Bytes header_bin(const Spec& s) {
  Bytes cnkh0 = join({
      s.dip_before_vdcl ? dip(4, 0, 3, 0, 1) : Bytes{},
      vdcl({element({1, 0, 2, 0, 0}), element({0, 0, 10, 3, 0}), element({0, 8, 15, 5, 1}),
            element({0, 12, 5, 1, 0}), element({0, 16, 8, 1, 1})}),
      chunk("VTXS", words({1, 0, 0, 12})),
      chunk("VTX ", words({0, 0, 0, 20})),
      chunk("IDX ", words({0})),
      chunk("knsS", words({0x00022490U, 0x167E0001U})),
      s.no_materials
          ? Bytes{}
          : chunk("kns2",
                  mctl({cf1i({9, 10.0F}),
                        list("TR  ", {{"BTX0", words({0, 0, 0})}, {"NMAP", words({0, 1, 0})}}),
                        list("SSSM", {{"LENV", words({0, 3, 13})}}),
                        list("SRS ", {{"BTAD", words({0, 0, 7})}})})),
      chunk("GENV", {}),
      holder("DGRP", 0,
             {holder("DVOL", 0,
                     {chunk("knsC", mctl({list("CM1R", {{"JINT", words({0, 5, 0})},
                                                        {"JINT", words({0, 9, s.cm1r_slot1})}})})),
                      dip(4, 0, 4, 0, 1)}),
              holder("DVOL", 1, {dip(s.dip1_type, 0, 4, 3, 1)})}),
  });
  const Bytes buffers1 = join({
      vdcl({element({0, 0, 2, 0, 0}), element({0, 12, s.group1_normal_type, 3, 0}),
            element({0, s.group1_texcoord_offset, 15, 5, 1})}),
      chunk("VTX ", s.short_vtx1 ? words({0, 1, 0}) : words({0, 1, 0, 24})),
      chunk("IDX ", words({1})),
  });
  const Bytes draw1 = dip(4, 0, 3, 0, s.dip2_primitives);
  Bytes cnkh1 = join({
      s.nested_vdcl1 ? Bytes{} : buffers1,
      s.no_materials ? Bytes{}
                     : chunk("knsC", mctl({cf1i({s.cf1i_count1, 20.0F}),
                                           list("TR  ", {{"BTX0", words({0, 2, 0})}})})),
      chunk("TANM", words({1, 2})),
      s.nested_vdcl1 ? holder("DGRP", 0, {buffers1, draw1}) : holder("DGRP", 0, {draw1}),
  });
  Bytes h = fx::text_bytes(s.magic);
  fx::append_u32(h, 0x11223344U);
  fx::append_u32(h, 1);
  fx::append_u32(h, 0);
  for (const float f : kBounds) {
    fx::append_f32(h, f);
  }
  fx::append_u32(h, 0);
  fx::append_u32(h, 0);
  fx::append_u32(h, 2); // CNKH count at +0x58
  fx::append_u32(h, 0);
  fx::append_u32(h, 0);
  for (std::uint32_t k = 0; k < 2; ++k) {
    fx::append_u32(h, k);
    const Bytes c = chunk("CNKH", k == 0 ? cnkh0 : cnkh1);
    h.insert(h.end(), c.begin(), c.end());
  }
  if (s.trailing_bytes) {
    fx::append_u32(h, 0);
  }
  return h;
}

// TS2V: magic, byte-order word, version, vertex count, shape count, the
// entry table at 0x50, the data offset; 16-byte entries naming 32-byte
// names by offset from the record start; deltas shape-major.
Bytes ts2v(std::uint32_t vertices) {
  Bytes b = fx::text_bytes("TS2V");
  fx::append_u32(b, 0x11223344U);
  fx::append_u32(b, 2);
  fx::append_u32(b, vertices);
  fx::append_u32(b, 2);
  b.resize(0x48);
  fx::append_u32(b, 0x50);
  fx::append_u32(b, 0x50 + (2 * 16) + (2 * 32));
  fx::append_u32(b, 0x50 + (2 * 16));
  b.resize(b.size() + 12U);
  fx::append_u32(b, 0x50 + (2 * 16) + 32);
  b.resize(b.size() + 12U);
  for (const std::string_view name : {std::string_view("smile"), std::string_view("blink")}) {
    Bytes field(32);
    std::copy(name.begin(), name.end(), reinterpret_cast<char*>(field.data()));
    b.insert(b.end(), field.begin(), field.end());
  }
  const Bytes deltas = counting(std::size_t{64}, 60);
  b.insert(b.end(), deltas.begin(), deltas.end());
  return b;
}

struct Model {
  std::vector<fx::Record> records;
  Bytes file;
};

Model build_model(const Spec& s) {
  Model m;
  Bytes index1 = counting(s.index1_bytes, 0);
  for (std::size_t i = 0; i < index1.size(); i += 2) {
    index1[i] = static_cast<std::byte>(i / 2);
    index1[i + 1] = std::byte{0};
  }
  const Bytes index0 = words({0x00010000U, 0x00000002U, 0x00030002U});
  m.records = {
      {"USER", s.header_name, "", header_bin(s), Bytes(16), {}, {}},
      {"DXVB", "vertex_000.bin", "", counting(std::size_t{80}, 1), Bytes(32), {}, {}},
      {"DXVB", "shapevertex_000.bin", "", counting(std::size_t{48}, 100), Bytes(32), {}, {}},
      {"DXIB", "index_000.bin", "", index0, Bytes(32), {}, {}},
      {"DXVB",
       "vertex_001.bin",
       "",
       counting(std::size_t{72} + s.vertex1_extra, 150),
       Bytes(32),
       {},
       {}},
      {"DXIB", s.index1_name, "", index1, Bytes(32), {}, {}},
      {"USER", "shapeoffset_000.bin", "", ts2v(s.ts2v_vertices), Bytes(16), {}, {}},
  };
  if (s.extra_shape_record) {
    m.records.push_back({"USER", "shapeoffset_001.bin", "", ts2v(4), Bytes(16), {}, {}});
  }
  m.file = fx::build(m.records);
  return m;
}

bool same(std::span<const std::byte> a, std::span<const std::byte> b) {
  return std::ranges::equal(a, b);
}

// (mesh_reader.hpp, read_mesh_container: layout, buffers, draws, materials,
// bone maps, bounds, morph sets, unread_chunks)
void synthetic_model() {
  const Model model = build_model(Spec{});
  cc::CprContainer container;
  require(cc::parse_cpr(model.file, container) == cc::CprError::None, "the container parses");
  cc::MeshSource out;
  const cc::MeshReadFailure failure = cc::read_mesh_container(container, out);
  require(failure.ok(), "the synthetic model reads");
  const cc::MeshAssetData& mesh = out.mesh;
  require(mesh.groups.size() == 2U, "one group per VDCL");

  using A = g::VertexAttribute;
  using T = g::VertexElementType;
  const cc::MeshGroupData& g0 = mesh.groups[0];
  g::VertexStreamLayout layout0{};
  layout0.attributes[0] = {A::Position, T::Float3, 1, 0};
  layout0.attributes[1] = {A::Normal, T::Short4Norm, 0, 0};
  layout0.attributes[2] = {A::TexCoord1, T::Half2, 0, 8};
  layout0.attributes[3] = {A::BoneSlots, T::UByte4, 0, 12};
  layout0.attributes[4] = {A::BoneWeights, T::UByte4Norm, 0, 16};
  layout0.attribute_count = 5;
  layout0.stream_count = 2;
  layout0.streams = {g::VertexStreamDescription{20}, g::VertexStreamDescription{12}};
  require(g0.layout == layout0, "the two-stream layout from VDCL, VTX and VTXS");
  require(g0.vertex_count == 4U, "vertex count is stream 0 bytes over its stride");
  require(same(g0.stream0, model.records[1].payload) && same(g0.stream1, model.records[2].payload),
          "both streams copied as stored");
  require(g0.indices == std::vector<std::uint16_t>{0, 1, 2, 0, 2, 3}, "16-bit indices");
  require(g0.first_draw == 0U && g0.draw_count == 2U && g0.morph_set == 0U,
          "group 0's draws and morph set");

  const cc::MeshGroupData& g1 = mesh.groups[1];
  g::VertexStreamLayout layout1{};
  layout1.attributes[0] = {A::Position, T::Float3, 0, 0};
  layout1.attributes[1] = {A::Normal, T::Short4Norm, 0, 12};
  layout1.attributes[2] = {A::TexCoord1, T::Half2, 0, 20};
  layout1.attribute_count = 3;
  layout1.stream_count = 1;
  layout1.streams[0].stride = 24;
  require(g1.layout == layout1 && g1.stream1.empty(), "a one-stream group");
  require(g1.vertex_count == 3U && g1.indices.size() == 3U &&
              same(g1.stream0, model.records[4].payload),
          "group 1's buffers");
  require(g1.first_draw == 2U && g1.draw_count == 1U && g1.morph_set == g::kNoMorphSet,
          "group 1's draws, no morph set");

  require(mesh.draws.size() == 3U, "one draw per DIP");
  const std::array<g::MeshDrawRecord, 3> draws{{
      {0, 0, 4, 0, 3, 0, 0, 0},
      {0, 0, 4, 3, 3, 0, g::kNoBoneMap, 0},
      {0, 0, 3, 0, 3, 1, g::kNoBoneMap, 0},
  }};
  for (std::size_t i = 0; i < draws.size(); ++i) {
    const g::MeshDrawRecord& d = mesh.draws[i];
    require(d.base_vertex == draws[i].base_vertex && d.min_index == draws[i].min_index &&
                d.vertex_count == draws[i].vertex_count && d.first_index == draws[i].first_index &&
                d.index_count == draws[i].index_count && d.material == draws[i].material &&
                d.bone_map == draws[i].bone_map,
            i, "draw: index count is 3 * primitives; material and bone map by document order");
  }
  require(mesh.material_count == 2U && out.materials.size() == 2U,
          "a material per kns2 or knsC holding a CF1I");
  require(mesh.bone_maps == std::vector<std::vector<std::uint32_t>>{{5, 9}},
          "the bone map lists skeleton bone positions");
  require(mesh.bounds_max == std::array<float, 3>{1.0F, 2.0F, 3.0F} &&
              mesh.bounds_min == std::array<float, 3>{-1.0F, -2.0F, -3.0F} &&
              mesh.center == std::array<float, 3>{0.0F, 0.5F, 0.0F} && mesh.radius == 4.0F,
          "bounds as stored");
  require(mesh.skeleton_id.empty(), "the skeleton id is the caller's to set");

  const cc::MaterialBlock& m0 = out.materials[0];
  require(m0.knss_feature == 0x00022490U && m0.knss_second == 0x167E0001U, "knsS words");
  require(m0.cf1i[0].tag == tag_of("PARF") && m0.cf1i[8].tag == tag_of("ENVP") &&
              m0.cf1i[4].value == std::array<float, 4>{14.0F, 14.25F, 14.5F, 14.75F},
          "CF1I records as stored");
  require(m0.slots.size() == 2U && m0.slots[0].tag == tag_of("BTX0") && m0.slots[0].index == 0U &&
              m0.slots[1].tag == tag_of("NMAP") && m0.slots[1].index == 1U,
          "TR slots");
  require(m0.has_sssm && m0.sssm_lenv == std::array<std::uint32_t, 3>{0, 3, 13}, "SSSM");
  require(m0.has_srs && m0.srs_tag == tag_of("BTAD") &&
              m0.srs_value == std::array<std::uint32_t, 3>{0, 0, 7},
          "SRS");
  const cc::MaterialBlock& m1 = out.materials[1];
  require(m1.knss_feature == 0x00022490U && m1.knss_second == 0x167E0001U,
          "a material without its own knsS takes the nearest preceding one");
  require(m1.slots.size() == 1U && m1.slots[0].index == 2U && !m1.has_sssm && !m1.has_srs,
          "the second material's blocks");

  require(out.morph_sets.size() == 1U, "one morph set");
  const cc::MorphTargetsData& set = out.morph_sets[0];
  require(set.group == 0U && set.vertex_count == 4U && set.targets.size() == 2U, "set header");
  const Bytes deltas = counting(64, 60);
  require(set.targets[0].name == "smile" && set.targets[1].name == "blink" &&
              same(set.targets[0].deltas, std::span(deltas).first(32)) &&
              same(set.targets[1].deltas, std::span(deltas).subspan(32)),
          "named targets with their deltas, shape-major");
  require(out.unread_chunks == std::vector<Tag>{tag_of("GENV"), tag_of("TANM")},
          "tags not read, once each, first-seen order");

  // (mesh_reader.hpp, read_mesh_container: "A mesh with no CF1I material has
  // material_count 0 and every draw kNoMaterial")
  {
    Spec spec;
    spec.no_materials = true;
    const Model bare = build_model(spec);
    cc::CprContainer c;
    require(cc::parse_cpr(bare.file, c) == cc::CprError::None, "the bare model parses");
    cc::MeshSource read;
    require(cc::read_mesh_container(c, read).ok(), "a model with no CF1I reads");
    require(read.mesh.material_count == 0U && read.materials.empty() &&
                read.mesh.draws.size() == 3U &&
                std::ranges::all_of(
                    read.mesh.draws,
                    [](const g::MeshDrawRecord& d) { return d.material == g::kNoMaterial; }),
            "no material, every draw kNoMaterial");
  }

  // (mesh_reader.hpp, Framing: a VDCL nested inside DGRP or DVOL reads the
  // same, the walk being by document order)
  {
    Spec spec;
    spec.nested_vdcl1 = true;
    const Model nested = build_model(spec);
    cc::CprContainer c;
    require(cc::parse_cpr(nested.file, c) == cc::CprError::None, "the nested model parses");
    cc::MeshSource read;
    require(cc::read_mesh_container(c, read).ok(), "a VDCL inside a DGRP reads");
    const cc::MeshAssetData& n = read.mesh;
    require(n.groups.size() == mesh.groups.size() && n.draws.size() == mesh.draws.size() &&
                n.material_count == mesh.material_count && n.bone_maps == mesh.bone_maps &&
                read.materials.size() == out.materials.size() &&
                read.morph_sets.size() == out.morph_sets.size(),
            "the same counts as the flat model");
    for (std::size_t i = 0; i < n.groups.size(); ++i) {
      const cc::MeshGroupData& x = n.groups[i];
      const cc::MeshGroupData& y = mesh.groups[i];
      require(x.layout == y.layout && x.vertex_count == y.vertex_count && x.stream0 == y.stream0 &&
                  x.stream1 == y.stream1 && x.indices == y.indices &&
                  x.first_draw == y.first_draw && x.draw_count == y.draw_count &&
                  x.morph_set == y.morph_set,
              i, "the group reads as the flat one");
    }
    for (std::size_t i = 0; i < n.draws.size(); ++i) {
      const g::MeshDrawRecord& x = n.draws[i];
      const g::MeshDrawRecord& y = mesh.draws[i];
      require(x.base_vertex == y.base_vertex && x.min_index == y.min_index &&
                  x.vertex_count == y.vertex_count && x.first_index == y.first_index &&
                  x.index_count == y.index_count && x.material == y.material &&
                  x.bone_map == y.bone_map,
              i, "the draw reads as the flat one");
    }
  }
}

struct ErrorRow {
  std::string_view name;
  std::function<void(Spec&)> change;
  cc::MeshReadError expected;
};

// (mesh_reader.hpp, MeshReadError order, MeshReadFailure offset and tag)
void errors_named() {
  using E = cc::MeshReadError;
  const std::array<ErrorRow, 15> rows{{
      {"header record", [](Spec& s) { s.header_name = "header.dat"; }, E::HeaderRecordMissing},
      {"model magic", [](Spec& s) { s.magic = "2XFW"; }, E::BadModelMagic},
      {"chain end", [](Spec& s) { s.trailing_bytes = true; }, E::ChunkOverrun},
      {"element type", [](Spec& s) { s.group1_normal_type = 3; }, E::DeclarationUnsupported},
      {"attribute offset", [](Spec& s) { s.group1_texcoord_offset = 22; }, E::LayoutInvalid},
      {"index record", [](Spec& s) { s.index1_name = "index_009.bin"; }, E::BufferMissing},
      {"vertex bytes", [](Spec& s) { s.vertex1_extra = 1; }, E::BufferSizeNotMultipleOfStride},
      {"index bytes", [](Spec& s) { s.index1_bytes = 4; }, E::IndexBufferSizeInvalid},
      {"draw before any group", [](Spec& s) { s.dip_before_vdcl = true; }, E::DrawOutsideGroup},
      {"draw range", [](Spec& s) { s.dip2_primitives = 2; }, E::DrawRangeInvalid},
      {"primitive type", [](Spec& s) { s.dip1_type = 5; }, E::PrimitiveTypeUnsupported},
      {"bone map slot", [](Spec& s) { s.cm1r_slot1 = 0; }, E::BoneMapMalformed},
      {"CF1I count", [](Spec& s) { s.cf1i_count1 = 8; }, E::MaterialBlockMalformed},
      {"morph vertex count", [](Spec& s) { s.ts2v_vertices = 5; }, E::MorphSetMalformed},
      {"morph set without group", [](Spec& s) { s.extra_shape_record = true; },
       E::MorphSetGroupMissing},
  }};
  for (std::size_t i = 0; i < rows.size(); ++i) {
    Spec spec;
    rows[i].change(spec);
    const Model model = build_model(spec);
    cc::CprContainer container;
    require(cc::parse_cpr(model.file, container) == cc::CprError::None, i, "container parses");
    cc::MeshSource out;
    require(cc::read_mesh_container(container, out).error == rows[i].expected, i, rows[i].name);
  }
  // The failure names the chunk: its offset in the header payload and tag.
  // Chunk 20 is group 0's second DIP; chunk 23 is group 1's VTX.
  struct Located {
    std::string_view name;
    std::function<void(Spec&)> change;
    std::string_view tag;
    std::size_t chunk;
  };
  const std::array<Located, 2> located{{
      {"primitive type", [](Spec& s) { s.dip1_type = 5; }, "DIP ", 20},
      {"vertex chunk too short", [](Spec& s) { s.short_vtx1 = true; }, "VTX ", 23},
  }};
  for (std::size_t i = 0; i < located.size(); ++i) {
    Spec spec;
    located[i].change(spec);
    const Model model = build_model(spec);
    cc::CprContainer container;
    require(cc::parse_cpr(model.file, container) == cc::CprError::None, i, "container parses");
    std::vector<cc::Chunk> chunks;
    require(cc::walk_chunks(container.records()[0].payload, chunks).ok(), i, "walks");
    cc::MeshSource out;
    const cc::MeshReadFailure failure = cc::read_mesh_container(container, out);
    require(failure.tag == tag_of(located[i].tag) &&
                failure.offset == chunks[located[i].chunk].offset,
            i, located[i].name);
  }
}

constexpr std::array<bw::testing::Scenario<void>, 2> kScenarios{{
    {"synthetic_model", synthetic_model},
    {"errors_named", errors_named},
}};

} // namespace

int main(int argc, char** argv) { return bw::testing::run_scenario(argc, argv, kScenarios); }
