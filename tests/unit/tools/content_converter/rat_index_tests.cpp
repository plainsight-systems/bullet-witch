// Unit test: public contracts of bw/content_converter/rat_index.hpp; each
// scenario names the declaration it asserts. Catalogs are built here from
// the header's stated framing.
#include "bw/content_converter/rat_index.hpp"
#include "bw/testing/harness.hpp"

#include "cpr_fixture.hpp"

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

struct Catalog {
  Bytes bytes;
  std::size_t files_at{};
  std::size_t strings_at{};
};

// "RAT ", total size, two zero words, six (offset, size) pairs; sections 0,
// 2 and 3 hold opaque bytes, section 1 is empty, section 4 the 8-byte file
// entries and section 5 the string pool with the empty string at 0.
Catalog catalog() {
  Catalog c;
  Bytes strings{std::byte{0}};
  const auto add = [&](std::string_view text) {
    const auto offset = static_cast<std::uint32_t>(strings.size());
    fx::append_text(strings, text);
    strings.push_back(std::byte{0});
    return offset;
  };
  Bytes files;
  fx::append_u32(files, add("graphics\\chara\\PW0\\model"));
  fx::append_u32(files, add("mesh.cpr"));
  fx::append_u32(files, 0);
  fx::append_u32(files, add("vfx.cpr"));
  fx::append_u32(files, add("script"));
  fx::append_u32(files, add("st01_00.cpr"));
  const std::array<Bytes, 6> sections{Bytes(24, std::byte{1}), Bytes{}, Bytes(20, std::byte{2}),
                                      Bytes(8, std::byte{3}),  files,   strings};
  Bytes& b = c.bytes;
  b.resize(cc::kRatHeaderBytes);
  for (std::size_t k = 0; k < 4; ++k) {
    b[k] = static_cast<std::byte>("RAT "[k]);
  }
  for (std::size_t s = 0; s < sections.size(); ++s) {
    fx::put_u32(b, 0x10U + (8U * s), static_cast<std::uint32_t>(b.size()));
    fx::put_u32(b, 0x14U + (8U * s), static_cast<std::uint32_t>(sections[s].size()));
    if (s == 4U) {
      c.files_at = b.size();
    }
    if (s == 5U) {
      c.strings_at = b.size();
    }
    b.insert(b.end(), sections[s].begin(), sections[s].end());
  }
  fx::put_u32(b, 4, static_cast<std::uint32_t>(b.size()));
  return c;
}

// (rat_index.hpp, parse_rat, RatIndex::sections, RatIndex::files, relative_path)
void synthetic_catalog() {
  const Catalog c = catalog();
  cc::RatIndex index;
  require(cc::parse_rat(c.bytes, index) == cc::RatError::None, "the synthetic catalog parses");
  require(index.sections().size() == cc::kRatSections && index.sections()[1].size == 0U &&
              index.sections()[4].offset == c.files_at,
          "six sections as stored");
  const std::span<const cc::RatEntry> files = index.files();
  require(files.size() == 3U, "one entry per 8 bytes of section 4");
  require(files[0].directory == "graphics\\chara\\PW0\\model" && files[0].file_name == "mesh.cpr",
          "an entry's strings as stored");
  require(cc::relative_path(files[0]) == "graphics/chara/PW0/model/mesh.cpr",
          "backslashes become slashes, then the file name");
  require(files[1].directory.empty() && cc::relative_path(files[1]) == "vfx.cpr",
          "an empty directory gives the file name alone");
  require(cc::relative_path(files[2]) == "script/st01_00.cpr", "a one-component directory");
}

struct ErrorRow {
  std::string_view name;
  std::function<void(Catalog&)> change;
  cc::RatError expected;
};

// (rat_index.hpp, RatError order)
void errors_named() {
  using E = cc::RatError;
  using fx::put_u32;
  const auto resize = [](Catalog& c, std::size_t size) {
    c.bytes.resize(size);
    put_u32(c.bytes, 4, static_cast<std::uint32_t>(size));
  };
  const std::array<ErrorRow, 8> rows{{
      {"short", [](Catalog& c) { c.bytes.resize(cc::kRatHeaderBytes - 1U); }, E::TooShort},
      {"magic", [](Catalog& c) { c.bytes[3] = std::byte{'X'}; }, E::BadMagic},
      {"size word", [](Catalog& c) { put_u32(c.bytes, 4, 12); }, E::SizeMismatch},
      {"section past the file", [](Catalog& c) { put_u32(c.bytes, 0x14U + 16U, 0x10000U); },
       E::SectionOutOfRange},
      {"file table size",
       [](Catalog& c) { put_u32(c.bytes, 0x14U + 32U, fx::get_u32(c.bytes, 0x14U + 32U) - 4U); },
       E::FileTableMisaligned},
      {"string offset", [](Catalog& c) { put_u32(c.bytes, c.files_at + 12U, 0x1000U); },
       E::StringOutOfRange},
      {"string unterminated",
       [&](Catalog& c) {
         // Drop the pool's final NUL: the last file name runs off the end.
         resize(c, c.bytes.size() - 1U);
         put_u32(c.bytes, 0x14U + 40U, fx::get_u32(c.bytes, 0x14U + 40U) - 1U);
       },
       E::StringUnterminated},
      {"empty catalog is valid", [](Catalog& c) { put_u32(c.bytes, 0x14U + 32U, 0); }, E::None},
  }};
  for (std::size_t i = 0; i < rows.size(); ++i) {
    Catalog c = catalog();
    rows[i].change(c);
    cc::RatIndex index;
    require(cc::parse_rat(c.bytes, index) == rows[i].expected, i, rows[i].name);
  }
}

constexpr std::array<bw::testing::Scenario<void>, 2> kScenarios{{
    {"synthetic_catalog", synthetic_catalog},
    {"errors_named", errors_named},
}};

} // namespace

int main(int argc, char** argv) { return bw::testing::run_scenario(argc, argv, kScenarios); }
