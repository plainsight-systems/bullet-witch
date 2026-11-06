#ifndef BW_TESTS_UNIT_TOOLS_CONTENT_CONVERTER_CPR_FIXTURE_HPP
#define BW_TESTS_UNIT_TOOLS_CONTENT_CONVERTER_CPR_FIXTURE_HPP
// Test fixture: synthetic original-game containers built from the framing
// bw/content_converter/cpr_container.hpp states (header words, 48-byte
// record entries, 12-byte argument triples, the names block with its
// leading NUL, the slot block, region 0 for USER payloads and region 1 for
// buffers and images, and the 5-image tail block). The converter's tests
// share it; it is the header's framing written out, not the reader's code.

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
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

} // namespace bw::testing::cpr

#endif
