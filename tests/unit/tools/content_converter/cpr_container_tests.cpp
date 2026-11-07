// Unit test: public contracts of bw/content_converter/cpr_container.hpp;
// each scenario names the declaration it asserts. Containers are built here
// from the header's stated framing (cpr_fixture.hpp).
#include "bw/content_converter/cpr_container.hpp"
#include "bw/testing/cpr_fixture.hpp"
#include "bw/testing/harness.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <string_view>
#include <vector>

namespace {

namespace cc = bw::content_converter;
namespace fx = bw::testing::cpr;
using bw::testing::require;
using fx::Bytes;

Bytes counting(std::size_t count, std::uint8_t first) {
  Bytes bytes(count);
  for (std::size_t i = 0; i < count; ++i) {
    bytes[i] = static_cast<std::byte>(first + i);
  }
  return bytes;
}

bool same(std::span<const std::byte> a, std::span<const std::byte> b) {
  return std::ranges::equal(a, b);
}

// Six records: a USER payload, a CALL with name and integer arguments, a
// vertex buffer, an image with a tail block, a record of an unknown tag and
// a USER record that carries an argument. Five triples are 60 bytes, so the
// argument table is padded to 64, which is not a multiple of 12.
std::vector<fx::Record> records() {
  std::vector<fx::Record> r(6);
  r[0] = {"USER", "header.bin", "", counting(20, 1), counting(16, 100), {}, {}};
  r[1] = {"CALL",
          "vfx.texture.create",
          "model.pw0.texture.default",
          {},
          {},
          {{"table", 2, "model.PW0.texture.table.table", 0},
           {"dds", 2, "model.PW0.texture.dds.PW0_body_m1_game_psd", 0},
           {"bank", 0, "", 3},
           {"lod", 0, "", 1}},
          {}};
  r[2] = {"DXVB", "vertex_000.bin", "", counting(36, 40), counting(32, 120), {}, {}};
  r[3] = {"DDS ",
          "body.dds",
          "model.pw0.texture.dds.pw0_body_m1_game_psd",
          counting(10, 80),
          counting(52, 160),
          {},
          {counting(3, 1), counting(5, 2), counting(7, 3), counting(1, 4), counting(2, 5)}};
  r[4] = {"ABCD", "mystery.bin", "", {}, {}, {}, {}};
  r[5] = {"USER", "motion.spi", "", counting(4, 9), counting(16, 7), {{"path", 2, "event.x", 0}},
          {}};
  return r;
}

// (cpr_container.hpp, parse_cpr: header, records, names, arguments,
// arguments_of, regions, slots, tail_images, find_record, RecordTag::Unknown)
void synthetic_container() {
  fx::Layout layout;
  const Bytes file = fx::build(records(), &layout);
  cc::CprContainer c;
  require(cc::parse_cpr(file, c) == cc::CprError::None, "the synthetic container parses");

  const cc::CprHeader& h = c.header();
  require(h.version == 1U && h.record_table_bytes == 6U * cc::kCprRecordBytes,
          "version and record table size");
  require(h.region0_base == layout.region0_at && h.region1_base == layout.region1_at,
          "region bases follow the four tables");
  require(h.names_bytes == layout.slots_at - layout.names_at &&
              h.argument_table_bytes == layout.names_at - layout.arguments_at,
          "table sizes as stored");
  require(h.argument_table_bytes == 64U && h.argument_table_bytes % cc::kCprArgumentBytes != 0U &&
              c.arguments().size() == h.argument_table_bytes / cc::kCprArgumentBytes,
          "a table padded to 16 bytes holds floor(bytes / 12) triples");

  const std::span<const cc::CprRecord> rs = c.records();
  require(rs.size() == 6U, "one record per table entry");
  const std::array<cc::RecordTag, 6> tags{cc::RecordTag::User,    cc::RecordTag::Call,
                                          cc::RecordTag::Dxvb,    cc::RecordTag::Dds,
                                          cc::RecordTag::Unknown, cc::RecordTag::User};
  for (std::size_t i = 0; i < tags.size(); ++i) {
    require(rs[i].tag == tags[i], i, "tag");
  }
  require(rs[4].tag_bytes == std::array<char, 4>{'A', 'B', 'C', 'D'} && rs[4].payload.empty(),
          "an unknown tag keeps its bytes and has no payload");
  require(rs[0].short_name == "header.bin" && rs[0].qualified_name.empty(),
          "a record without a qualified name");
  require(rs[3].qualified_name == "model.pw0.texture.dds.pw0_body_m1_game_psd", "a qualified name");

  const std::vector<fx::Record> source = records();
  require(same(rs[0].payload, source[0].payload) &&
              rs[0].payload.data() == file.data() + layout.region0_at,
          "a USER payload views region 0");
  require(same(rs[2].payload, source[2].payload) &&
              rs[2].payload.data() == file.data() + layout.region1_at,
          "a buffer payload views region 1");
  require(same(rs[3].payload, source[3].payload), "an image payload");
  require(rs[1].payload.empty(), "a CALL has no payload");
  require(same(rs[3].slot, source[3].slot) && same(rs[2].slot, source[2].slot),
          "slots view the slot block");

  const std::span<const cc::CprArgument> call = c.arguments_of(rs[1]);
  require(call.size() == 4U, "the CALL's triples");
  require(call[0].slot_name == "table" && call[0].kind == cc::kCprArgumentKindName &&
              call[0].value == "model.PW0.texture.table.table",
          "a name argument keeps its case");
  require(call[1].slot_name == "dds" &&
              call[1].value == "model.PW0.texture.dds.PW0_body_m1_game_psd",
          "second argument");
  require(call[2].kind == cc::kCprArgumentKindInteger && call[2].raw_value == 3U &&
              call[2].value.empty(),
          "an integer argument");
  const std::span<const cc::CprArgument> user = c.arguments_of(rs[5]);
  require(user.size() == 1U && user[0].value == "event.x", "a USER record's argument");
  require(c.arguments_of(rs[0]).empty(), "a record with no arguments");

  const auto tails = c.tail_images(rs[3]);
  for (std::size_t i = 0; i < cc::kCprTailImages; ++i) {
    require(same(tails[i], source[3].tail[i]), i, "tail image");
  }
  const auto none = c.tail_images(rs[0]);
  require(std::ranges::all_of(none, [](std::span<const std::byte> s) { return s.empty(); }),
          "no tail block, no images");

  require(c.find_record("vertex_000.bin") == &rs[2], "find_record by short name");
  require(c.find_record("VERTEX_000.BIN") == nullptr, "find_record is bytewise");
  require(c.find_record("missing.bin") == nullptr, "find_record misses");
}

struct ErrorRow {
  std::string_view name;
  std::function<void(Bytes&, const fx::Layout&)> change;
  cc::CprError expected;
};

// (cpr_container.hpp, CprError order)
void errors_named() {
  using E = cc::CprError;
  using fx::put_u32;
  fx::Layout layout;
  const Bytes good = fx::build(records(), &layout);
  const auto names_bytes = [](const Bytes& f) { return fx::get_u32(f, 0x18); };
  const std::array<ErrorRow, 11> rows{{
      {"short", [](Bytes& f, const fx::Layout&) { f.resize(cc::kCprHeaderBytes - 1U); },
       E::TooShort},
      {"magic", [](Bytes& f, const fx::Layout&) { f[0] = std::byte{'X'}; }, E::BadMagic},
      {"version", [](Bytes& f, const fx::Layout&) { put_u32(f, 0x04, 2); }, E::UnsupportedVersion},
      {"table sum",
       [](Bytes& f, const fx::Layout&) { put_u32(f, 0x2C, fx::get_u32(f, 0x2C) + 16U); },
       E::LayoutInconsistent},
      {"name offset",
       [&](Bytes& f, const fx::Layout&) { put_u32(f, fx::record_at(2) + 4, names_bytes(f)); },
       E::NameOutOfRange},
      {"name unterminated",
       [&](Bytes& f, const fx::Layout& l) {
         // The last string in the block is "event.x", the fifth triple's
         // value; overwrite its NUL and the padding after it, and point
         // record 0's name at it.
         const std::size_t end = l.names_at + names_bytes(f);
         const std::size_t start = l.names_at + fx::get_u32(f, l.arguments_at + 48U + 8U);
         std::fill(f.begin() + static_cast<std::ptrdiff_t>(start + 7U),
                   f.begin() + static_cast<std::ptrdiff_t>(end), std::byte{'x'});
         put_u32(f, fx::record_at(0) + 4, static_cast<std::uint32_t>(start - l.names_at));
       },
       E::NameUnterminated},
      {"payload past its region",
       [](Bytes& f, const fx::Layout&) { put_u32(f, fx::record_at(0) + 0x24, 0x10000U); },
       E::PayloadOutOfRange},
      {"argument range",
       [](Bytes& f, const fx::Layout&) { put_u32(f, fx::record_at(1) + 0x10, 100); },
       E::ArgumentRangeOutOfRange},
      {"argument value",
       [&](Bytes& f, const fx::Layout& l) {
         put_u32(f, l.arguments_at + 12U + 8U, names_bytes(f));
       },
       E::ArgumentValueOutOfRange},
      {"slot", [](Bytes& f, const fx::Layout&) { put_u32(f, fx::record_at(3) + 0x1C, 0x10000U); },
       E::SlotOutOfRange},
      {"tail image",
       [](Bytes& f, const fx::Layout& l) { put_u32(f, l.tail_at[0] + 20U + 16U, 0x10000U); },
       E::TailOutOfRange},
  }};
  for (std::size_t i = 0; i < rows.size(); ++i) {
    Bytes file = good;
    rows[i].change(file, layout);
    cc::CprContainer c;
    require(cc::parse_cpr(file, c) == rows[i].expected, i, rows[i].name);
  }
}

// (cpr_container.hpp, find_by_qualified_name, equals_ignoring_ascii_case)
void qualified_name_lookup() {
  const Bytes file = fx::build(records());
  cc::CprContainer c;
  require(cc::parse_cpr(file, c) == cc::CprError::None, "parses");
  require(c.find_by_qualified_name("MODEL.PW0.Texture.DDS.PW0_body_M1_game_psd") == &c.records()[3],
          "a CALL argument's case finds the lower-case stored name");
  require(c.find_by_qualified_name("model.pw0.texture.dds.other") == nullptr, "a miss");

  struct Row {
    std::string_view a;
    std::string_view b;
    bool equal;
  };
  const std::array<Row, 6> rows{{
      {"abc_1", "ABC_1", true},
      {"abc", "abd", false},
      {"abc", "abcd", false},
      // '[' and '{' differ by the case bit but are not letters.
      {"[", "{", false},
      // Bytes outside ASCII are compared as they are.
      {"\xC4", "\xE4", false},
      {"", "", true},
  }};
  for (std::size_t i = 0; i < rows.size(); ++i) {
    require(cc::equals_ignoring_ascii_case(rows[i].a, rows[i].b) == rows[i].equal, i,
            "ASCII case folding only");
  }
}

constexpr std::array<bw::testing::Scenario<void>, 3> kScenarios{{
    {"synthetic_container", synthetic_container},
    {"errors_named", errors_named},
    {"qualified_name_lookup", qualified_name_lookup},
}};

} // namespace

int main(int argc, char** argv) { return bw::testing::run_scenario(argc, argv, kScenarios); }
