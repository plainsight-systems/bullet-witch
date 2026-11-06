// Unit test: public contracts of bw/content_converter/conversion.hpp that
// need no file; each scenario names the declaration it asserts. The run over
// an install is the integration test's.
#include "bw/content/cooked_asset.hpp"
#include "bw/content_converter/conversion.hpp"
#include "bw/content_converter/texture_reader.hpp"
#include "bw/testing/harness.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

namespace cc = bw::content_converter;
namespace content = bw::content;
using bw::testing::require;

cc::ConvertParse parse(std::initializer_list<std::string_view> arguments) {
  const std::vector<std::string_view> list(arguments);
  return cc::parse_convert_arguments(list);
}

// (conversion.hpp, parse_convert_arguments, UsageError, kConvertUsage): every
// option sets its field; an absent option keeps its default; each failure
// is named with the argument it concerns; every option the usage line names
// is one the parser accepts.
void parse_arguments() {
  {
    const cc::ConvertParse p =
        parse({"convert", "--install", "/games/bw", "--out", "/cache", "--only", "model.pw0.",
               "--threads", "8", "--normal-green", "flip", "--bc7-uber", "4"});
    require(p.ok(), "full command line parses");
    require(p.options.install == std::optional<std::filesystem::path>("/games/bw"), "install");
    require(p.options.output == "/cache", "out");
    require(p.options.only_prefix == "model.pw0.", "only");
    require(p.options.threads == 8U, "threads");
    require(p.options.texture.normal_green == cc::NormalMapConvention::FlipGreen, "normal green");
    require(p.options.texture.bc7_uber_level == 4U, "bc7 uber");
  }
  {
    const cc::ConvertParse p = parse({"convert", "--normal-green", "as-stored", "--out", "o"});
    require(p.ok(), "minimal command line parses");
    require(!p.options.install.has_value(), "install absent means discovery");
    require(p.options.only_prefix.empty() && p.options.threads == 0U, "only and threads default");
    require(p.options.texture.normal_green == cc::NormalMapConvention::AsStored, "as-stored");
    require(p.options.texture.bc7_uber_level == cc::kBc7UberLevel, "uber default");
  }

  struct Row {
    std::vector<std::string_view> arguments;
    cc::UsageError error;
    std::string_view argument;
  };
  const std::array<Row, 7> rows{{
      {{}, cc::UsageError::UnknownCommand, ""},
      {{"convert", "--out", "o", "--fast", "1"}, cc::UsageError::UnknownOption, "--fast"},
      {{"convert", "--out"}, cc::UsageError::MissingValue, "--out"},
      {{"convert", "--out", "a", "--out", "b"}, cc::UsageError::DuplicateOption, "--out"},
      {{"convert", "--only", "model."}, cc::UsageError::OutputMissing, ""},
      {{"convert", "--out", "o", "--threads", "four"}, cc::UsageError::BadNumber, "four"},
      {{"convert", "--out", "o", "--normal-green", "up"}, cc::UsageError::BadNormalGreen, "up"},
  }};
  for (std::size_t i = 0; i < rows.size(); ++i) {
    const cc::ConvertParse p = cc::parse_convert_arguments(rows[i].arguments);
    require(p.error == rows[i].error, i, "error named");
    require(p.argument == rows[i].argument, i, "argument named");
  }

  // Each "--" token of the usage line, given a value it allows, parses.
  const std::string_view usage = cc::kConvertUsage;
  std::size_t options = 0;
  for (std::size_t at = usage.find("--"); at != std::string_view::npos;
       at = usage.find("--", at + 2)) {
    const std::size_t end = usage.find_first_of(" ]", at);
    const std::string_view option = usage.substr(at, end - at);
    const std::string_view value = option == "--normal-green"                          ? "flip"
                                   : (option == "--threads" || option == "--bc7-uber") ? "1"
                                                                                       : "x";
    const cc::ConvertParse p = option == "--out" ? parse({"convert", option, value})
                                                 : parse({"convert", "--out", "o", option, value});
    require(p.ok(), options, "an option of the usage line parses");
    ++options;
  }
  require(options == 6U, "the usage line names six options");
}

// (conversion.hpp, plan_conversion): one job per container in the given
// (sorted) order, kinds by directory and name.
void plan_kinds_and_order() {
  const std::vector<std::string> containers{
      "data/concept/PW1/extend/cosspr.cpr",
      "data/data/stable/graphics/bg/01/cubemap/global_cubemap.cpr",
      "data/data/stable/graphics/bg/01/shared_texture/st01_gras12_psd.cpr",
      "data/data/stable/graphics/chara/PW0/model/mesh.cpr",
      "data/data/stable/graphics/chara/PW0/model/mesh2.cpr",
      "data/data/stable/graphics/chara/PW0/texture/default.cpr",
      "data/data/stable/graphics/chara/PW0/texture/texture.cpr",
      "data/data/stable/graphics/chara/PW0/vfx.cpr",
      "data/data/stable/graphics/chara/shared_texture/sub/inner.cpr",
      "data/data/stable/graphics/chara/texture/mesh.cpr",
      "data/shader/shaders.cpr",
  };
  const std::array<cc::JobKind, 11> kinds{
      cc::JobKind::Other, cc::JobKind::Textures, cc::JobKind::Textures, cc::JobKind::Model,
      cc::JobKind::Other, cc::JobKind::Other,    cc::JobKind::Textures, cc::JobKind::Other,
      cc::JobKind::Other, cc::JobKind::Other,    cc::JobKind::Other,
  };
  const std::vector<cc::ConversionJob> jobs = cc::plan_conversion(containers);
  require(jobs.size() == containers.size(), "one job per container");
  for (std::size_t i = 0; i < jobs.size(); ++i) {
    require(jobs[i].container == containers[i], i, "container in the given order");
    require(jobs[i].kind == kinds[i], i, "kind by directory and name");
  }
  require(cc::plan_conversion({}).empty(), "no containers, no jobs");
}

std::vector<std::string_view> lines_of(std::string_view text) {
  std::vector<std::string_view> lines;
  for (std::size_t at = 0; at < text.size();) {
    const std::size_t end = text.find('\n', at);
    if (end == std::string_view::npos) {
      lines.push_back(text.substr(at));
      break;
    }
    lines.push_back(text.substr(at, end - at));
    at = end + 1;
  }
  return lines;
}

bool carries(std::string_view line, std::string_view text) {
  return line.find(text) != std::string_view::npos;
}

// (conversion.hpp, render_report and Report): converted, skipped
// (unchanged), skipped by kind, each failure, each unmapped field, the
// seconds, the bytes written, in that order, one line each, each line
// carrying its count or its id and reason in decimal, the seconds line last
// but one. The label wording and the seconds' decimal places are the
// tool's own and are not asserted. The counts are chosen so that none is a
// substring of another line's values.
void report_text() {
  cc::ConversionReport report;
  report.converted = 29;
  report.skipped_unchanged = 31;
  report.skipped_by_kind = 6596;
  report.failed = {{"model.a.mesh", "mesh read: BufferMissing"},
                   {"model.b.texture.dds.c", "texture png: bad crc"}};
  report.unmapped = {{"material.PARF", 47}, {"mesh_chunk.TANM", 88}};
  report.bytes_written = 1234567;
  report.seconds = 12.75;

  const std::string text = cc::render_report(report);
  require(text.ends_with('\n'), "every line ends with a newline");
  const std::vector<std::string_view> lines = lines_of(text);
  struct Expected {
    std::string_view first;
    std::string_view second;
  };
  const std::array<Expected, 9> expected{{
      {"29", ""},
      {"31", ""},
      {"6596", ""},
      {"model.a.mesh", "mesh read: BufferMissing"},
      {"model.b.texture.dds.c", "texture png: bad crc"},
      {"material.PARF", "47"},
      {"mesh_chunk.TANM", "88"},
      {"12", ""},
      {"1234567", ""},
  }};
  require(lines.size() == expected.size(), "one line per item");
  for (std::size_t i = 0; i < lines.size(); ++i) {
    require(carries(lines[i], expected[i].first), i, "the line carries its value");
    require(carries(lines[i], expected[i].second), i, "the line carries its reason or count");
  }
  require(!carries(lines[lines.size() - 2], "1234567"), "the seconds line is last but one");

  const std::string empty_text = cc::render_report(cc::ConversionReport{});
  const std::vector<std::string_view> empty = lines_of(empty_text);
  require(empty.size() == 5U, "an empty report has its five count lines");
  for (std::size_t i = 0; i < empty.size(); ++i) {
    require(carries(empty[i], "0"), i, "each count line carries its zero");
  }
}

std::vector<std::byte> bytes_of(std::string_view text) {
  std::vector<std::byte> out;
  for (const char c : text) {
    out.push_back(static_cast<std::byte>(c));
  }
  return out;
}

content::Hash hash_of(content::AssetKind kind, std::string_view id,
                      std::initializer_list<std::string_view> items) {
  cc::InputHasher hasher(kind, id);
  for (const std::string_view item : items) {
    hasher.add(bytes_of(item));
  }
  return hasher.finish();
}

// (conversion.hpp, InputHasher): the kind, the id and every item's bytes, in
// order, enter the hash; equal inputs give equal hashes; text is its bytes;
// finish() is hash_bytes over the stated framing, the format version
// included.
void input_hash_order() {
  constexpr auto kTexture = content::AssetKind::Texture;
  const content::Hash base = hash_of(kTexture, "model.pw0.texture.dds.a", {"descriptor", "png"});
  require(base == hash_of(kTexture, "model.pw0.texture.dds.a", {"descriptor", "png"}),
          "equal inputs, equal hash");
  require(base != hash_of(kTexture, "model.pw0.texture.dds.a", {"descriptor", "pnh"}),
          "a changed byte changes the hash");
  require(base != hash_of(kTexture, "model.pw0.texture.dds.a", {"png", "descriptor"}),
          "a changed order changes the hash");
  require(base != hash_of(kTexture, "model.pw0.texture.dds.b", {"descriptor", "png"}),
          "a changed id changes the hash");
  require(base != hash_of(content::AssetKind::Material, "model.pw0.texture.dds.a",
                          {"descriptor", "png"}),
          "a changed kind changes the hash");
  require(base != hash_of(kTexture, "model.pw0.texture.dds.a", {"descriptor"}),
          "a dropped item changes the hash");

  cc::InputHasher text(kTexture, "model.pw0.texture.dds.a");
  text.add_text("descriptor");
  text.add_text("png");
  require(text.finish() == base, "add_text adds the text's bytes");

  // Known answer: the buffer the framing states, hashed with hash_bytes.
  const auto framed = [](std::uint32_t version, content::AssetKind kind, std::string_view id,
                         std::initializer_list<std::string_view> items) {
    std::vector<std::byte> buffer;
    const auto little = [&buffer](std::uint64_t value, std::size_t width) {
      for (std::size_t i = 0; i < width; ++i) {
        buffer.push_back(static_cast<std::byte>((value >> (8U * i)) & 0xFFU));
      }
    };
    const auto sized = [&](std::string_view item) {
      little(item.size(), 8);
      const std::vector<std::byte> bytes = bytes_of(item);
      buffer.insert(buffer.end(), bytes.begin(), bytes.end());
    };
    little(version, 4);
    little(static_cast<std::uint32_t>(kind), 4);
    sized(id);
    for (const std::string_view item : items) {
      sized(item);
    }
    return content::hash_bytes(buffer);
  };
  require(base == framed(content::kCookedFormatVersion, kTexture, "model.pw0.texture.dds.a",
                         {"descriptor", "png"}),
          "finish is hash_bytes of the stated framing");
  require(hash_of(content::AssetKind::Mesh, "model.pw0.mesh", {}) ==
              framed(content::kCookedFormatVersion, content::AssetKind::Mesh, "model.pw0.mesh", {}),
          "an asset with no items is the version, the kind and the id");
  require(base != framed(content::kCookedFormatVersion + 1U, kTexture, "model.pw0.texture.dds.a",
                         {"descriptor", "png"}),
          "the format version enters the hash");
}

constexpr std::array<bw::testing::Scenario<void>, 4> kScenarios{{
    {"parse_arguments", parse_arguments},
    {"plan_kinds_and_order", plan_kinds_and_order},
    {"report_text", report_text},
    {"input_hash_order", input_hash_order},
}};

} // namespace

int main(int argc, char** argv) { return bw::testing::run_scenario(argc, argv, kScenarios); }
