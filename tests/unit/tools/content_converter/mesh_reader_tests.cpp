// Unit test: public contracts of bw/content_converter/mesh_reader.hpp; each
// scenario names the declaration it asserts. The model container is
// bw/testing/cpr_fixture.hpp's, built from the header's stated framing.
#include "bw/content_converter/asset_data.hpp"
#include "bw/content_converter/cpr_container.hpp"
#include "bw/content_converter/mesh_reader.hpp"
#include "bw/rendering/geometry/mesh_asset.hpp"
#include "bw/rendering/geometry/vertex_streams.hpp"
#include "bw/testing/cpr_fixture.hpp"
#include "bw/testing/harness.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

namespace cc = bw::content_converter;
namespace g = bw::geometry;
namespace fx = bw::testing::cpr;
using bw::testing::require;
using fx::Bytes;
using Tag = std::array<char, 4>;

Tag tag_of(std::string_view text) { return {text[0], text[1], text[2], text[3]}; }

bool same(std::span<const std::byte> a, std::span<const std::byte> b) {
  return std::ranges::equal(a, b);
}

// (mesh_reader.hpp, read_mesh_container: layout, buffers, draws, materials,
// bone maps, bounds, morph sets, unread_chunks)
void synthetic_model() {
  const fx::Model model = fx::build_model(fx::ModelSpec{});
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
  layout0.attributes[2] = {A::TexCoord0, T::Half2, 0, 8};
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
  layout1.attributes[2] = {A::TexCoord0, T::Half2, 0, 20};
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
  const Bytes deltas = fx::counting(64, 60);
  require(set.targets[0].name == "smile" && set.targets[1].name == "blink" &&
              same(set.targets[0].deltas, std::span(deltas).first(32)) &&
              same(set.targets[1].deltas, std::span(deltas).subspan(32)),
          "named targets with their deltas, shape-major");
  require(out.unread_chunks == std::vector<Tag>{tag_of("GENV"), tag_of("TANM")},
          "tags not read, once each, first-seen order");

  // (mesh_reader.hpp, read_mesh_container: "A mesh with no CF1I material has
  // material_count 0 and every draw kNoMaterial")
  {
    fx::ModelSpec spec;
    spec.no_materials = true;
    const fx::Model bare = fx::build_model(spec);
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
    fx::ModelSpec spec;
    spec.nested_vdcl1 = true;
    const fx::Model nested = fx::build_model(spec);
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
  std::function<void(fx::ModelSpec&)> change;
  cc::MeshReadError expected;
};

// (mesh_reader.hpp, MeshReadError order, MeshReadFailure offset and tag)
void errors_named() {
  using E = cc::MeshReadError;
  const std::array<ErrorRow, 15> rows{{
      {"header record", [](fx::ModelSpec& s) { s.header_name = "header.dat"; },
       E::HeaderRecordMissing},
      {"model magic", [](fx::ModelSpec& s) { s.magic = "2XFW"; }, E::BadModelMagic},
      {"chain end", [](fx::ModelSpec& s) { s.trailing_bytes = true; }, E::ChunkOverrun},
      {"element type", [](fx::ModelSpec& s) { s.group1_normal_type = 3; },
       E::DeclarationUnsupported},
      {"attribute offset", [](fx::ModelSpec& s) { s.group1_texcoord_offset = 22; },
       E::LayoutInvalid},
      {"index record", [](fx::ModelSpec& s) { s.index1_name = "index_009.bin"; }, E::BufferMissing},
      {"vertex bytes", [](fx::ModelSpec& s) { s.vertex1_extra = 1; },
       E::BufferSizeNotMultipleOfStride},
      {"index bytes", [](fx::ModelSpec& s) { s.index1_bytes = 4; }, E::IndexBufferSizeInvalid},
      {"draw before any group", [](fx::ModelSpec& s) { s.dip_before_vdcl = true; },
       E::DrawOutsideGroup},
      {"draw range", [](fx::ModelSpec& s) { s.dip2_primitives = 2; }, E::DrawRangeInvalid},
      {"primitive type", [](fx::ModelSpec& s) { s.dip1_type = 5; }, E::PrimitiveTypeUnsupported},
      {"bone map slot", [](fx::ModelSpec& s) { s.cm1r_slot1 = 0; }, E::BoneMapMalformed},
      {"CF1I count", [](fx::ModelSpec& s) { s.cf1i_count1 = 8; }, E::MaterialBlockMalformed},
      {"morph vertex count", [](fx::ModelSpec& s) { s.ts2v_vertices = 5; }, E::MorphSetMalformed},
      {"morph set without group", [](fx::ModelSpec& s) { s.extra_shape_record = true; },
       E::MorphSetGroupMissing},
  }};
  for (std::size_t i = 0; i < rows.size(); ++i) {
    fx::ModelSpec spec;
    rows[i].change(spec);
    const fx::Model model = fx::build_model(spec);
    cc::CprContainer container;
    require(cc::parse_cpr(model.file, container) == cc::CprError::None, i, "container parses");
    cc::MeshSource out;
    require(cc::read_mesh_container(container, out).error == rows[i].expected, i, rows[i].name);
  }
  // The failure names the chunk: its offset in the header payload and tag.
  // Chunk 20 is group 0's second DIP; chunk 23 is group 1's VTX.
  struct Located {
    std::string_view name;
    std::function<void(fx::ModelSpec&)> change;
    std::string_view tag;
    std::size_t chunk;
  };
  const std::array<Located, 2> located{{
      {"primitive type", [](fx::ModelSpec& s) { s.dip1_type = 5; }, "DIP ", 20},
      {"vertex chunk too short", [](fx::ModelSpec& s) { s.short_vtx1 = true; }, "VTX ", 23},
  }};
  for (std::size_t i = 0; i < located.size(); ++i) {
    fx::ModelSpec spec;
    located[i].change(spec);
    const fx::Model model = fx::build_model(spec);
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
