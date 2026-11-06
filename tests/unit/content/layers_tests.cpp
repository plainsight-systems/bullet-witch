// Unit test: the pure contracts of bw/content/layers.hpp; each scenario names
// the declarations it asserts. open_layers is the OS probe and is asserted by
// tests/integration/content.
#include "bw/content/cooked_asset.hpp"
#include "bw/content/layers.hpp"
#include "bw/testing/harness.hpp"

#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace {

using bw::content::CacheRecordError;
using bw::content::CacheRecordParse;
using bw::content::ConvertedCacheRecord;
using bw::content::Layer;
using bw::testing::require;
using E = CacheRecordError;

// (layers.hpp, parse_converted_cache_record, encode_converted_cache_record,
// CacheRecordError): encode and parse round-trip, install_path verbatim
// including '=' and spaces; each malformed record names its first failure at
// its 1-based line, MissingKey and VersionMismatch at line 0, and
// VersionMismatch only after the whole file parsed.
void cache_record_round_trip_and_errors() {
  const ConvertedCacheRecord record{bw::content::kCookedFormatVersion,
                                    "C:\\Games\\Bullet Witch=copy/\xc3\xa9", 6035};
  const std::string text = bw::content::encode_converted_cache_record(record);
  const CacheRecordParse round = bw::content::parse_converted_cache_record(text);
  require(round.ok() && round.record == record, "encode then parse gives the record back");
  require(text == "format_version=1\ninstall_path=C:\\Games\\Bullet Witch=copy/\xc3\xa9\n"
                  "graphics_containers=6035\n",
          "the stated line form");

  const std::string version = "format_version=1\n";
  const std::string path = "install_path=/games/bw\n";
  const std::string count = "graphics_containers=6035\n";
  struct Row {
    std::string text;
    E expected;
    std::size_t line;
    std::string_view note;
  };
  const std::array rows{
      Row{"format_version\n" + path + count, E::UnknownLine, 1, "a line without '='"},
      Row{version + "install=/x\n" + count, E::UnknownLine, 2, "an unknown key"},
      Row{path + version + count, E::UnknownLine, 1, "a key out of order"},
      Row{version + count + path, E::UnknownLine, 2, "a later key before an earlier one"},
      Row{version + version + path + count, E::DuplicateKey, 2, "a key twice"},
      Row{version + path + count + count, E::DuplicateKey, 4, "the last key twice"},
      Row{version + path, E::MissingKey, 0, "graphics_containers missing"},
      Row{"", E::MissingKey, 0, "an empty record"},
      Row{"format_version=one\n" + path + count, E::BadValue, 1, "not decimal"},
      Row{"format_version=\n" + path + count, E::BadValue, 1, "no digits"},
      Row{version + path + "graphics_containers=-1\n", E::BadValue, 3, "a sign"},
      Row{version + path + "graphics_containers=4294967296\n", E::BadValue, 3, "past u32"},
      Row{"format_version=2\n" + path + count, E::VersionMismatch, 0, "another format"},
  };
  for (std::size_t i = 0; i < rows.size(); ++i) {
    const CacheRecordParse parse = bw::content::parse_converted_cache_record(rows[i].text);
    require(parse.error == rows[i].expected, i, rows[i].note);
    require(parse.line == rows[i].line, i, std::string(rows[i].note) + ": line");
  }
  const CacheRecordParse max = bw::content::parse_converted_cache_record(
      version + path + "graphics_containers=4294967295\n");
  require(max.ok() && max.record.graphics_containers == 4294967295U, "u32 max parses");
}

using Scenario = bw::testing::Scenario<void>;
constexpr std::array kScenarios{
    Scenario{"cache_record_round_trip_and_errors", &cache_record_round_trip_and_errors},
};

} // namespace

int main(int argc, char** argv) { return bw::testing::run_scenario(argc, argv, kScenarios); }
