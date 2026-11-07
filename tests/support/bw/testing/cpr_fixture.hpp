#ifndef BW_TESTS_SUPPORT_BW_TESTING_CPR_FIXTURE_HPP
#define BW_TESTS_SUPPORT_BW_TESTING_CPR_FIXTURE_HPP
// Test fixture: synthetic original-game containers built from the framing
// bw/content_converter/cpr_container.hpp states (header words, 48-byte
// record entries, 12-byte argument triples, the names block with its
// leading NUL, the slot block, region 0 for USER payloads and region 1 for
// buffers and images, and the 5-image tail block). The converter's tests
// share it through the harness's include directory; it is the header's
// framing written out, not the reader's code. The model container at the end
// is bw/content_converter/mesh_reader.hpp's framing, and the texture records
// and table texture_reader.hpp's and material_mapping.hpp's, written out the
// same way.

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace bw::testing::cpr {

using Bytes = std::vector<std::byte>;

inline void put_u32(Bytes& bytes, std::size_t at, std::uint32_t value) {
  for (std::size_t i = 0; i < 4; ++i) {
    bytes[at + i] = static_cast<std::byte>((value >> (8U * i)) & 0xFFU);
  }
}
inline std::uint32_t get_u32(std::span<const std::byte> bytes, std::size_t at) {
  std::uint32_t value = 0;
  for (std::size_t i = 0; i < 4; ++i) {
    value |= std::to_integer<std::uint32_t>(bytes[at + i]) << (8U * i);
  }
  return value;
}
inline void append_u32(Bytes& bytes, std::uint32_t value) {
  bytes.resize(bytes.size() + 4U);
  put_u32(bytes, bytes.size() - 4U, value);
}
inline void append_f32(Bytes& bytes, float value) {
  append_u32(bytes, std::bit_cast<std::uint32_t>(value));
}
inline void append_text(Bytes& bytes, std::string_view text) {
  for (const char c : text) {
    bytes.push_back(static_cast<std::byte>(c));
  }
}
inline void pad_to(Bytes& bytes, std::size_t alignment) {
  bytes.resize((bytes.size() + alignment - 1U) / alignment * alignment);
}
inline Bytes text_bytes(std::string_view text) {
  Bytes bytes;
  append_text(bytes, text);
  return bytes;
}

struct Argument {
  std::string slot;
  // 2: `text` is a names-block string; 0: `integer` is a literal.
  std::uint32_t kind{2};
  std::string text;
  std::uint32_t integer{};
};

struct Record {
  std::string tag; // four characters as stored
  std::string short_name;
  std::string qualified_name; // empty: none
  Bytes payload;              // region 0 for USER, region 1 for the buffer and image tags
  Bytes slot;
  std::vector<Argument> arguments;
  std::vector<Bytes> tail; // five images, or empty for no tail block
};

// Where the builder put things, so a test can patch exact words.
struct Layout {
  std::size_t arguments_at{};
  std::size_t names_at{};
  std::size_t slots_at{};
  std::size_t region0_at{};
  std::size_t region1_at{};
  std::vector<std::size_t> tail_at;
};

inline std::size_t record_at(std::size_t index) { return 0x60U + (48U * index); }

inline bool uses_region1(std::string_view tag) {
  return tag == "DXVB" || tag == "DXIB" || tag == "DDS " || tag == "DDSC";
}

// The container bytes for `records`, in record order.
inline Bytes build(const std::vector<Record>& records, Layout* layout = nullptr) {
  Bytes names{std::byte{0}};
  const auto name_offset = [&](std::string_view text) -> std::uint32_t {
    if (text.empty()) {
      return 0;
    }
    const auto offset = static_cast<std::uint32_t>(names.size());
    append_text(names, text);
    names.push_back(std::byte{0});
    return offset;
  };
  Bytes arguments;
  Bytes slots;
  Bytes region0;
  Bytes region1;
  Bytes table(48U * records.size());
  for (std::size_t i = 0; i < records.size(); ++i) {
    const Record& r = records[i];
    const std::size_t at = 48U * i;
    for (std::size_t k = 0; k < 4; ++k) {
      table[at + k] = static_cast<std::byte>(r.tag[k]);
    }
    put_u32(table, at + 0x04, name_offset(r.short_name));
    put_u32(table, at + 0x08, name_offset(r.qualified_name));
    put_u32(table, at + 0x0C, 0xFFFFFFFFU);
    put_u32(table, at + 0x10, static_cast<std::uint32_t>(r.arguments.size()));
    put_u32(table, at + 0x14, static_cast<std::uint32_t>(arguments.size() / 12U));
    for (const Argument& a : r.arguments) {
      append_u32(arguments, name_offset(a.slot));
      append_u32(arguments, a.kind);
      append_u32(arguments, a.kind == 2U ? name_offset(a.text) : a.integer);
    }
    put_u32(table, at + 0x18, static_cast<std::uint32_t>(slots.size()));
    put_u32(table, at + 0x1C, static_cast<std::uint32_t>(r.slot.size()));
    slots.insert(slots.end(), r.slot.begin(), r.slot.end());
    pad_to(slots, 16);
    if (r.tag == "USER") {
      pad_to(region0, 16);
      put_u32(table, at + 0x20, static_cast<std::uint32_t>(region0.size()));
      put_u32(table, at + 0x24, static_cast<std::uint32_t>(r.payload.size()));
      region0.insert(region0.end(), r.payload.begin(), r.payload.end());
    } else if (uses_region1(r.tag)) {
      pad_to(region1, 4);
      put_u32(table, at + 0x28, static_cast<std::uint32_t>(region1.size()));
      put_u32(table, at + 0x2C, static_cast<std::uint32_t>(r.payload.size()));
      region1.insert(region1.end(), r.payload.begin(), r.payload.end());
    }
  }
  pad_to(arguments, 16);
  pad_to(names, 16);
  pad_to(region0, 16);
  pad_to(region1, 4);
  Bytes file(0x60);
  for (std::size_t k = 0; k < 4; ++k) {
    file[k] = static_cast<std::byte>("CPR1"[k]);
  }
  put_u32(file, 0x04, 1);
  put_u32(file, 0x08, 0x11223344U);
  put_u32(file, 0x10, static_cast<std::uint32_t>(table.size()));
  put_u32(file, 0x14, static_cast<std::uint32_t>(arguments.size()));
  put_u32(file, 0x18, static_cast<std::uint32_t>(names.size()));
  put_u32(file, 0x1C, static_cast<std::uint32_t>(slots.size()));
  put_u32(file, 0x20, static_cast<std::uint32_t>(region0.size()));
  put_u32(file, 0x24, static_cast<std::uint32_t>(region1.size()));
  put_u32(
      file, 0x2C,
      static_cast<std::uint32_t>(table.size() + arguments.size() + names.size() + slots.size()));
  Layout l;
  file.insert(file.end(), table.begin(), table.end());
  l.arguments_at = file.size();
  file.insert(file.end(), arguments.begin(), arguments.end());
  l.names_at = file.size();
  file.insert(file.end(), names.begin(), names.end());
  l.slots_at = file.size();
  file.insert(file.end(), slots.begin(), slots.end());
  l.region0_at = file.size();
  file.insert(file.end(), region0.begin(), region0.end());
  l.region1_at = file.size();
  file.insert(file.end(), region1.begin(), region1.end());
  // Tail blocks after region 1: five offsets, five sizes, then the images,
  // offsets relative to the end of the two tables.
  for (std::size_t i = 0; i < records.size(); ++i) {
    if (records[i].tail.empty()) {
      continue;
    }
    pad_to(file, 4);
    const std::size_t tail = file.size();
    l.tail_at.push_back(tail);
    put_u32(file, 0x60U + (48U * i) + 0x0C, static_cast<std::uint32_t>(tail));
    file.resize(tail + 40U);
    Bytes images;
    for (std::size_t k = 0; k < records[i].tail.size(); ++k) {
      pad_to(images, 4);
      put_u32(file, tail + (4U * k), static_cast<std::uint32_t>(images.size()));
      put_u32(file, tail + 20U + (4U * k), static_cast<std::uint32_t>(records[i].tail[k].size()));
      images.insert(images.end(), records[i].tail[k].begin(), records[i].tail[k].end());
    }
    file.insert(file.end(), images.begin(), images.end());
  }
  if (layout != nullptr) {
    *layout = l;
  }
  return file;
}

// ---- model header chunks (mesh_reader.hpp) ----------------------------------

inline Bytes words(std::initializer_list<std::uint32_t> values) {
  Bytes bytes;
  for (const std::uint32_t v : values) {
    append_u32(bytes, v);
  }
  return bytes;
}
inline Bytes join(std::initializer_list<Bytes> parts) {
  Bytes out;
  for (const Bytes& p : parts) {
    out.insert(out.end(), p.begin(), p.end());
  }
  return out;
}
// A chunk: the tag stored last-first, the size of the data, the data.
inline Bytes chunk(std::string_view tag, const Bytes& data) {
  Bytes out;
  for (std::size_t i = 4; i > 0; --i) {
    out.push_back(static_cast<std::byte>(tag[i - 1]));
  }
  append_u32(out, static_cast<std::uint32_t>(data.size()));
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
inline Bytes element(const Element& e) {
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
inline Bytes vdcl(std::initializer_list<Bytes> elements) {
  Bytes data = words({0});
  for (const Bytes& e : elements) {
    data.insert(data.end(), e.begin(), e.end());
  }
  const Bytes end = element({0xFF, 0, 17, 0, 0});
  data.insert(data.end(), end.begin(), end.end());
  pad_to(data, 16);
  return chunk("VDCL", data);
}
inline Bytes holder(std::string_view tag, std::uint32_t word,
                    std::initializer_list<Bytes> children) {
  Bytes data = words({word});
  for (const Bytes& c : children) {
    data.insert(data.end(), c.begin(), c.end());
  }
  return chunk(tag, data);
}
inline Bytes dip(std::uint32_t type, std::uint32_t base, std::uint32_t count, std::uint32_t first,
                 std::uint32_t primitives) {
  return chunk("DIP ", words({type, base, 0, count, first, primitives}));
}

// ---- a model container (mesh_reader.hpp) ------------------------------------

// Chunks with byte-reversed tags and sizes that exclude their 8 bytes, the
// CNKH chain after the count at +0x58, the VDCL elements, VTX/VTXS/IDX/DIP
// words, the CM1R entries, the material chunks and the TS2V record. DGRP and
// DVOL carry one leading word and MCTL two, as the content stores them; the
// header does not state these words.

inline Bytes counting(std::size_t count, std::uint8_t first) {
  Bytes bytes(count);
  for (std::size_t i = 0; i < count; ++i) {
    bytes[i] = static_cast<std::byte>(first + i);
  }
  return bytes;
}
// CF1I: u32 0, count, then 32-byte records of tag (stored last-first),
// three zero words and four floats.
struct Cf1iSpec {
  std::uint32_t count;
  float base;
};
inline Bytes cf1i(const Cf1iSpec& spec) {
  const auto [count, base] = spec;
  Bytes data = words({0, count});
  const std::array<std::string_view, 9> tags{"PARF", "CCOL", "FRSA", "FRSB", "FRSR",
                                             "LUMI", "REFP", "SPCC", "ENVP"};
  for (std::size_t r = 0; r < tags.size(); ++r) {
    for (std::size_t i = 4; i > 0; --i) {
      data.push_back(static_cast<std::byte>(tags[r][i - 1]));
    }
    append_u32(data, 0);
    append_u32(data, 0);
    append_u32(data, 0);
    for (std::size_t k = 0; k < 4; ++k) {
      append_f32(data, base + static_cast<float>(r) + (0.25F * static_cast<float>(k)));
    }
  }
  return chunk("CF1I", data);
}
// A list chunk (TR, SSSM, SRS): u32 0, count, 16-byte entries of a tag
// (stored last-first) and three words.
inline Bytes list(std::string_view tag,
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
inline Bytes mctl(std::initializer_list<Bytes> children) {
  Bytes data = words({0, 0});
  for (const Bytes& c : children) {
    data.insert(data.end(), c.begin(), c.end());
  }
  return chunk("MCTL", data);
}

// The knobs mesh_reader_tests.cpp's error table turns; the defaults build
// the good model.
struct ModelSpec {
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

inline constexpr std::array<float, 16> kBounds{1.0F, 2.0F, 3.0F, 1.0F, -1.0F, -2.0F, -3.0F, 1.0F,
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
inline Bytes model_header(const ModelSpec& s) {
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
  Bytes h = text_bytes(s.magic);
  append_u32(h, 0x11223344U);
  append_u32(h, 1);
  append_u32(h, 0);
  for (const float f : kBounds) {
    append_f32(h, f);
  }
  append_u32(h, 0);
  append_u32(h, 0);
  append_u32(h, 2); // CNKH count at +0x58
  append_u32(h, 0);
  append_u32(h, 0);
  for (std::uint32_t k = 0; k < 2; ++k) {
    append_u32(h, k);
    const Bytes c = chunk("CNKH", k == 0 ? cnkh0 : cnkh1);
    h.insert(h.end(), c.begin(), c.end());
  }
  if (s.trailing_bytes) {
    append_u32(h, 0);
  }
  return h;
}

// TS2V: magic, byte-order word, version, vertex count, shape count, the
// entry table at 0x50, the data offset; 16-byte entries naming 32-byte
// names by offset from the record start; deltas shape-major.
inline Bytes ts2v(std::uint32_t vertices) {
  Bytes b = text_bytes("TS2V");
  append_u32(b, 0x11223344U);
  append_u32(b, 2);
  append_u32(b, vertices);
  append_u32(b, 2);
  b.resize(0x48);
  append_u32(b, 0x50);
  append_u32(b, 0x50 + (2 * 16) + (2 * 32));
  append_u32(b, 0x50 + (2 * 16));
  b.resize(b.size() + 12U);
  append_u32(b, 0x50 + (2 * 16) + 32);
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
  std::vector<Record> records;
  Bytes file;
};

inline Model build_model(const ModelSpec& s) {
  Model m;
  Bytes index1 = counting(s.index1_bytes, 0);
  for (std::size_t i = 0; i < index1.size(); i += 2) {
    index1[i] = static_cast<std::byte>(i / 2);
    index1[i + 1] = std::byte{0};
  }
  const Bytes index0 = words({0x00010000U, 0x00000002U, 0x00030002U});
  m.records = {
      {"USER", s.header_name, "", model_header(s), Bytes(16), {}, {}},
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
  m.file = build(m.records);
  return m;
}

// ---- texture records (texture_reader.hpp, material_mapping.hpp) -------------

// A DDS record's 52-byte descriptor: a 512 x 512 DXT1 2D texture, sign x, y,
// z gamma and w unsigned.
inline Bytes dxt1_descriptor() {
  return words({3, 1, 0, 0, 0, 0xFFFF0000U, 0xFFFF0000U, 0x840000FEU, 0x0025C052U, 0x003FE1FFU,
                0x00000D10U, 0x00000240U, 0x0027CA00U});
}

// Two 2x2 8-bit RGB PNGs (colour type 2, no alpha). The first: red, green /
// blue, (10, 20, 30); the second: (40, 50, 60), (70, 80, 90) / (100, 110,
// 120), (130, 140, 150). Generated once with a python3 -I script using
// zlib.compress (level 9) and zlib.crc32 for the IDAT stream and the chunk
// CRCs; the bytes are the fixture, nothing regenerates them.
inline constexpr std::array<std::uint8_t, 76> kPng{
    0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52,
    0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x02, 0x08, 0x02, 0x00, 0x00, 0x00, 0xfd, 0xd4, 0x9a,
    0x73, 0x00, 0x00, 0x00, 0x13, 0x49, 0x44, 0x41, 0x54, 0x78, 0xda, 0x63, 0xf8, 0xcf, 0xc0, 0xc0,
    0x00, 0xc2, 0x0c, 0xff, 0xb9, 0x44, 0xe4, 0x00, 0x1a, 0x58, 0x03, 0x3a, 0xe2, 0x92, 0x6e, 0xd9,
    0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82,
};
inline constexpr std::array<std::uint8_t, 79> kSecondPng{
    0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52,
    0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x02, 0x08, 0x02, 0x00, 0x00, 0x00, 0xfd, 0xd4, 0x9a,
    0x73, 0x00, 0x00, 0x00, 0x16, 0x49, 0x44, 0x41, 0x54, 0x78, 0xda, 0x63, 0xd0, 0x30, 0xb2, 0x71,
    0x0b, 0x88, 0x62, 0x48, 0xc9, 0xab, 0x68, 0xea, 0x99, 0x06, 0x00, 0x18, 0xf0, 0x04, 0x75, 0xb5,
    0xbf, 0x2d, 0x0a, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82,
};
template <std::size_t N> Bytes png_bytes(const std::array<std::uint8_t, N>& png) {
  Bytes bytes;
  for (const std::uint8_t v : png) {
    bytes.push_back(static_cast<std::byte>(v));
  }
  return bytes;
}

// A texture table container: a USER record and the CALL `function` whose
// first argument (slot `first_slot`) names the table and whose later
// arguments (slot `later_slot`) are `names`, in order.
struct TableCall {
  std::string_view function;
  std::string_view first_slot;
  std::string_view later_slot;
};
inline std::vector<Record> texture_table(const TableCall& call,
                                         std::span<const std::string_view> names) {
  std::vector<Argument> arguments{
      {std::string(call.first_slot), 2, "model.PW0.texture.table.table", 0}};
  for (const std::string_view name : names) {
    arguments.push_back({std::string(call.later_slot), 2, std::string(name), 0});
  }
  return {
      {"USER", "table._tm", "model.pw0.texture.table.table", Bytes(16), Bytes(16), {}, {}},
      {"CALL", std::string(call.function), "model.pw0.texture.default", {}, {}, arguments, {}},
  };
}

} // namespace bw::testing::cpr

#endif
