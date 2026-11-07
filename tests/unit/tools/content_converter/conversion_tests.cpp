// Unit test: public contracts of bw/content_converter/conversion.hpp that
// need no file; each scenario names the declaration it asserts. The run over
// an install is the integration test's; the handedness rule's mesh is built
// here as MeshAssetData, no container.
#include "bw/content/cooked_asset.hpp"
#include "bw/content_converter/asset_data.hpp"
#include "bw/content_converter/conversion.hpp"
#include "bw/content_converter/texture_reader.hpp"
#include "bw/rendering/geometry/mesh_asset.hpp"
#include "bw/rendering/geometry/morph_targets_asset.hpp"
#include "bw/rendering/geometry/vertex_streams.hpp"
#include "bw/testing/harness.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
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

// ---- tangent handedness ------------------------------------------------------

namespace g = bw::geometry;
using Bytes = std::vector<std::byte>;

void append_u16(Bytes& out, std::uint16_t value) {
  out.push_back(static_cast<std::byte>(value & 0xFFU));
  out.push_back(static_cast<std::byte>(value >> 8U));
}
void append_f32(Bytes& out, float value) {
  const auto bits = std::bit_cast<std::uint32_t>(value);
  append_u16(out, static_cast<std::uint16_t>(bits & 0xFFFFU));
  append_u16(out, static_cast<std::uint16_t>(bits >> 16U));
}
std::int16_t i16_at(std::span<const std::byte> bytes, std::size_t at) {
  return static_cast<std::int16_t>(std::to_integer<std::uint16_t>(bytes[at]) |
                                   (std::to_integer<std::uint16_t>(bytes[at + 1]) << 8U));
}

constexpr std::uint16_t kSnormOne = 0x7FFF;
constexpr std::uint16_t kHalfZero = 0x0000;
constexpr std::uint16_t kHalfHalf = 0x3800;
constexpr std::uint16_t kHalfOne = 0x3C00;
// Group 0, stream 0: normal short4n at 0, texture coordinate set 0 half2 at
// 8, tangent short4n at 12; group 1 has no texture coordinates: normal at 0,
// tangent at 8. Stream 1 of both: position float3.
constexpr std::size_t kStride0 = 20;
constexpr std::size_t kStride1 = 16;
constexpr std::size_t kPositionStride = 12;
constexpr std::size_t kTangentW0 = 18;
constexpr std::size_t kTangentW1 = 14;

struct HandedVertex {
  std::array<float, 3> position{};
  std::array<std::uint16_t, 2> uv{};
};

// A normal (0, 0, 1) and a tangent (1, 0, 0) with w +1, as the content
// stores every tangent.
void append_frame(Bytes& out, std::span<const std::uint16_t> uv) {
  for (const std::uint16_t c : {std::uint16_t{0}, std::uint16_t{0}, kSnormOne, kSnormOne}) {
    append_u16(out, c);
  }
  for (const std::uint16_t c : uv) {
    append_u16(out, c);
  }
  for (const std::uint16_t c : {kSnormOne, std::uint16_t{0}, std::uint16_t{0}, kSnormOne}) {
    append_u16(out, c);
  }
}

cc::MeshGroupData handed_group(std::span<const HandedVertex> vertices, bool texcoord,
                               std::vector<std::uint16_t> indices, std::uint32_t first_draw) {
  using A = g::VertexAttribute;
  using T = g::VertexElementType;
  cc::MeshGroupData group;
  g::VertexStreamLayout& layout = group.layout;
  layout.attributes[0] = {A::Position, T::Float3, 1, 0};
  layout.attributes[1] = {A::Normal, T::Short4Norm, 0, 0};
  if (texcoord) {
    layout.attributes[2] = {A::TexCoord0, T::Half2, 0, 8};
    layout.attributes[3] = {A::Tangent, T::Short4Norm, 0, 12};
  } else {
    layout.attributes[2] = {A::Tangent, T::Short4Norm, 0, 8};
  }
  layout.attribute_count = texcoord ? 4 : 3;
  layout.stream_count = 2;
  layout.streams = {
      g::VertexStreamDescription{static_cast<std::uint32_t>(texcoord ? kStride0 : kStride1)},
      g::VertexStreamDescription{static_cast<std::uint32_t>(kPositionStride)}};
  for (const HandedVertex& v : vertices) {
    append_frame(group.stream0, texcoord ? std::span<const std::uint16_t>(v.uv)
                                         : std::span<const std::uint16_t>());
    for (const float c : v.position) {
      append_f32(group.stream1, c);
    }
  }
  group.vertex_count = static_cast<std::uint32_t>(vertices.size());
  group.indices = std::move(indices);
  group.first_draw = first_draw;
  group.draw_count = 1;
  return group;
}

// Group 0: triangle A (v0, v1, v2) is not mirrored, B (v0, v2, v3) is, so v0
// and v2 split; C (v4, v5, v6) has no texture area. Group 1 has Tangent and
// no texture coordinate set 0. The morph set is group 0's, two targets, each
// vertex's delta distinct.
struct HandedMesh {
  cc::MeshAssetData mesh;
  std::vector<cc::MorphTargetsData> morph_sets;
};
HandedMesh handed_mesh() {
  const std::array<HandedVertex, 7> vertices{{
      {{0.0F, 0.0F, 0.0F}, {kHalfZero, kHalfZero}},
      {{1.0F, 0.0F, 0.0F}, {kHalfOne, kHalfZero}},
      {{0.0F, 1.0F, 0.0F}, {kHalfZero, kHalfOne}},
      {{-1.0F, 0.0F, 0.0F}, {kHalfOne, kHalfZero}},
      {{2.0F, 0.0F, 0.0F}, {kHalfHalf, kHalfHalf}},
      {{3.0F, 0.0F, 0.0F}, {kHalfHalf, kHalfHalf}},
      {{2.0F, 1.0F, 0.0F}, {kHalfHalf, kHalfHalf}},
  }};
  HandedMesh m;
  m.mesh.groups.push_back(handed_group(vertices, true, {0, 1, 2, 0, 2, 3, 4, 5, 6}, 0));
  m.mesh.groups.push_back(handed_group(std::span(vertices).first(3), false, {0, 1, 2}, 1));
  m.mesh.draws = {{0, 0, 7, 0, 9, 0, g::kNoBoneMap, 0}, {0, 0, 3, 0, 3, 0, g::kNoBoneMap, 0}};
  m.mesh.material_count = 1;
  cc::MorphTargetsData set{0, 7, {}};
  for (std::uint8_t t = 0; t < 2; ++t) {
    cc::MorphTargetData target{t == 0 ? "smile" : "blink", {}};
    for (std::size_t b = 0; b < std::size_t{7} * g::kMorphDeltaBytes; ++b) {
      target.deltas.push_back(static_cast<std::byte>((std::size_t{t} * 100U) + b));
    }
    set.targets.push_back(std::move(target));
  }
  m.morph_sets.push_back(std::move(set));
  return m;
}

// (conversion.hpp, write_tangent_handedness, Meshes, tangent handedness,
// TangentHandednessError): w per corner, -1 unmirrored and +1 mirrored; a
// vertex whose corners disagree keeps -1 and its copy, appended after the
// originals in their order, takes +1 with both streams and every target's
// delta repeated and the mirrored triangle's indices naming it; the draw's
// vertex_count widened to the last index it names, min_index kept. A vertex
// used only by a triangle with no texture area, and every vertex of a group
// without texture coordinate set 0, takes -1. Then one row per error name,
// each breaking one rule, naming its group. Alone catches a sign written per
// vertex rather than per corner, a split that drops a stream or a delta, or
// an overflow written as wrapped indices.
void tangent_handedness() {
  HandedMesh m = handed_mesh();
  const cc::TangentHandednessResult result = cc::write_tangent_handedness(m.mesh, m.morph_sets);
  require(result.ok(), "the mesh is written");

  const cc::MeshGroupData& group = m.mesh.groups[0];
  require(group.vertex_count == 9U && group.stream0.size() == 9U * kStride0 &&
              group.stream1.size() == 9U * kPositionStride,
          "v0 and v2 are copied once each");
  constexpr std::int16_t kMinus = -32767;
  constexpr std::int16_t kPlus = 32767;
  const std::array<std::int16_t, 9> w{kMinus, kMinus, kMinus, kPlus, kMinus,
                                      kMinus, kMinus, kPlus,  kPlus};
  for (std::size_t v = 0; v < w.size(); ++v) {
    require(i16_at(group.stream0, (v * kStride0) + kTangentW0) == w[v], v, "w per corner");
  }
  // Each copy and its original.
  constexpr std::array<std::pair<std::size_t, std::size_t>, 2> kCopies{{{7, 0}, {8, 2}}};
  const auto same = [](std::span<const std::byte> bytes, std::size_t a, std::size_t b,
                       std::size_t length) {
    return std::equal(bytes.begin() + static_cast<std::ptrdiff_t>(a),
                      bytes.begin() + static_cast<std::ptrdiff_t>(a + length),
                      bytes.begin() + static_cast<std::ptrdiff_t>(b));
  };
  for (const auto& [copy, original] : kCopies) {
    require(same(group.stream0, copy * kStride0, original * kStride0, kTangentW0) &&
                same(group.stream1, copy * kPositionStride, original * kPositionStride,
                     kPositionStride),
            copy, "the copy repeats both streams of its original but w");
  }
  require(group.indices == std::vector<std::uint16_t>{0, 1, 2, 7, 8, 3, 4, 5, 6},
          "the mirrored triangle names the copies");
  require(m.mesh.draws[0].vertex_count == 9U && m.mesh.draws[0].min_index == 0U,
          "the draw's vertex_count reaches the copies, min_index kept");
  const cc::MorphTargetsData& set = m.morph_sets[0];
  require(set.vertex_count == 9U, "the morph set counts the copies");
  for (std::size_t t = 0; t < set.targets.size(); ++t) {
    const std::vector<std::byte>& deltas = set.targets[t].deltas;
    require(deltas.size() == std::size_t{9} * g::kMorphDeltaBytes, t,
            "a delta per vertex, copies included");
    for (const auto& [copy, original] : kCopies) {
      require(same(deltas, copy * g::kMorphDeltaBytes, original * g::kMorphDeltaBytes,
                   g::kMorphDeltaBytes),
              t, "the copy repeats its original's delta");
    }
  }
  const cc::MeshGroupData& bare = m.mesh.groups[1];
  for (std::size_t v = 0; v < bare.vertex_count; ++v) {
    require(i16_at(bare.stream0, (v * kStride1) + kTangentW1) == kMinus, v,
            "a group without texture coordinate set 0 takes -1");
  }

  using E = cc::TangentHandednessError;
  struct Row {
    std::string_view name;
    std::function<void(HandedMesh&)> change;
    E error;
    std::uint32_t group;
  };
  const std::array<Row, 5> rows{{
      {"a tangent stored as ubyte4n",
       [](HandedMesh& h) {
         h.mesh.groups[1].layout.attributes[2].type = g::VertexElementType::UByte4Norm;
       },
       E::TangentTypeUnsupported, 1},
      {"a draw past the group's indices", [](HandedMesh& h) { h.mesh.draws[1].index_count = 6; },
       E::DrawOutOfRange, 1},
      {"two draws sharing an index position",
       [](HandedMesh& h) {
         h.mesh.draws.push_back(h.mesh.draws[1]);
         h.mesh.groups[1].draw_count = 2;
       },
       E::DrawRangesOverlap, 1},
      {"a morph set of another vertex count", [](HandedMesh& h) { h.morph_sets[0].group = 1; },
       E::MorphVertexCountMismatch, 1},
      {"copies past 65,535 vertices",
       [](HandedMesh& h) {
         cc::MeshGroupData& crowded = h.mesh.groups[0];
         crowded.vertex_count = 65534;
         crowded.stream0.resize(std::size_t{65534} * kStride0);
         crowded.stream1.resize(std::size_t{65534} * kPositionStride);
         h.morph_sets.clear();
       },
       E::VertexCountOverflow, 0},
  }};
  for (std::size_t i = 0; i < rows.size(); ++i) {
    HandedMesh broken = handed_mesh();
    rows[i].change(broken);
    const cc::TangentHandednessResult r =
        cc::write_tangent_handedness(broken.mesh, broken.morph_sets);
    require(r.error == rows[i].error && r.group == rows[i].group, i, rows[i].name);
  }
}

constexpr std::array<bw::testing::Scenario<void>, 5> kScenarios{{
    {"parse_arguments", parse_arguments},
    {"plan_kinds_and_order", plan_kinds_and_order},
    {"report_text", report_text},
    {"input_hash_order", input_hash_order},
    {"tangent_handedness", tangent_handedness},
}};

} // namespace

int main(int argc, char** argv) { return bw::testing::run_scenario(argc, argv, kScenarios); }
