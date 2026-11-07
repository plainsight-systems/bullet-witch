// Unit test: public contracts of bw/content_converter/asset_data.hpp; each
// scenario names the declaration it asserts. Every encoder's output is read
// back through the consuming system's own decoder.
#include "bw/animation/skeleton_asset.hpp"
#include "bw/content/asset_id.hpp"
#include "bw/content/cooked_asset.hpp"
#include "bw/content_converter/asset_data.hpp"
#include "bw/rendering/geometry/mesh_asset.hpp"
#include "bw/rendering/geometry/morph_targets_asset.hpp"
#include "bw/rendering/geometry/vertex_streams.hpp"
#include "bw/rendering/materials/material_asset.hpp"
#include "bw/rendering/rhi/format.hpp"
#include "bw/rendering/texture_asset.hpp"
#include "bw/testing/harness.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

namespace cc = bw::content_converter;
namespace g = bw::geometry;
namespace r = bw::rendering;
namespace a = bw::animation;
namespace mat = bw::materials;
using bw::testing::require;
using Bytes = std::vector<std::byte>;

Bytes counting(std::size_t count, std::uint8_t first) {
  Bytes bytes(count);
  for (std::size_t i = 0; i < count; ++i) {
    bytes[i] = static_cast<std::byte>(first + i);
  }
  return bytes;
}
bool same(std::span<const std::byte> x, std::span<const std::byte> y) {
  return std::ranges::equal(x, y);
}
bool aligned(std::span<const std::byte> part, std::span<const std::byte> whole) {
  return static_cast<std::size_t>(part.data() - whole.data()) % 16U == 0U;
}
std::size_t offset_in(std::span<const std::byte> part, std::span<const std::byte> whole) {
  return static_cast<std::size_t>(part.data() - whole.data());
}

// A section of a payload as its layout header locates it.
struct Section {
  std::size_t offset;
  std::size_t size;
};
// The bytes of `payload` outside every section: how many, and whether all
// are zero. asset_data.hpp pads each section with zero bytes to the next
// 16-byte boundary, so the bytes no section claims are padding.
struct Padding {
  std::size_t count;
  bool zero;
};
Padding padding_of(std::span<const std::byte> payload, std::span<const Section> sections) {
  std::vector<bool> claimed(payload.size(), false);
  for (const Section& section : sections) {
    for (std::size_t i = section.offset; i < section.offset + section.size && i < payload.size();
         ++i) {
      claimed[i] = true;
    }
  }
  Padding padding{0, true};
  for (std::size_t i = 0; i < payload.size(); ++i) {
    if (!claimed[i]) {
      ++padding.count;
      padding.zero = padding.zero && payload[i] == std::byte{0};
    }
  }
  return padding;
}

// Group 0: two streams (position float3 in stream 1, stride 12; normal
// short4n and texture coordinate half2 in stream 0, stride 12), 4 vertices,
// 6 indices, two draws, morph set 0. Group 1: one stream (position, bone
// slots, bone weights, stride 20), 3 vertices, 3 indices, one draw.
cc::MeshAssetData mesh_data() {
  using A = g::VertexAttribute;
  using T = g::VertexElementType;
  cc::MeshAssetData m;
  cc::MeshGroupData g0;
  g0.layout.attributes[0] = {A::Position, T::Float3, 1, 0};
  g0.layout.attributes[1] = {A::Normal, T::Short4Norm, 0, 0};
  g0.layout.attributes[2] = {A::TexCoord0, T::Half2, 0, 8};
  g0.layout.attribute_count = 3;
  g0.layout.stream_count = 2;
  g0.layout.streams = {g::VertexStreamDescription{12}, g::VertexStreamDescription{12}};
  g0.vertex_count = 4;
  g0.stream0 = counting(48, 1);
  g0.stream1 = counting(48, 100);
  g0.indices = {0, 1, 2, 0, 2, 3};
  g0.first_draw = 0;
  g0.draw_count = 2;
  g0.morph_set = 0;
  cc::MeshGroupData g1;
  g1.layout.attributes[0] = {A::Position, T::Float3, 0, 0};
  g1.layout.attributes[1] = {A::BoneSlots, T::UByte4, 0, 12};
  g1.layout.attributes[2] = {A::BoneWeights, T::UByte4Norm, 0, 16};
  g1.layout.attribute_count = 3;
  g1.layout.stream_count = 1;
  g1.layout.streams[0].stride = 20;
  g1.vertex_count = 3;
  g1.stream0 = counting(60, 160);
  g1.indices = {2, 1, 0};
  g1.first_draw = 2;
  g1.draw_count = 1;
  m.groups = {g0, g1};
  m.draws = {{0, 0, 4, 0, 3, 0, 0, 0},
             {0, 0, 4, 3, 3, 1, g::kNoBoneMap, 0},
             {0, 0, 3, 0, 3, g::kNoMaterial, 1, 0}};
  m.material_count = 2;
  m.bone_maps = {{5, 9}, {124}};
  m.bounds_max = {1.0F, 2.0F, 3.0F};
  m.bounds_min = {-1.0F, -2.0F, -3.0F};
  m.center = {0.0F, 0.5F, 0.0F};
  m.radius = 4.0F;
  m.skeleton_id = "model.pw0.mesh.skeleton";
  return m;
}

void mesh_round_trip() {
  const cc::MeshAssetData m = mesh_data();
  cc::EncodeFailure failure;
  const Bytes payload = cc::encode_mesh_asset(m, failure);
  require(failure.ok() && !payload.empty(), "the mesh encodes");
  require(payload.size() % 16U == 0U, "sections padded to 16 bytes");
  g::MeshAssetView view;
  require(g::decode_mesh_asset(payload, view).error == g::MeshAssetError::None, "and decodes");
  const g::MeshPayloadHeader& h = view.header();
  require(h.group_count == 2U && h.draw_count == 3U && h.material_count == 2U &&
              h.bone_map_count == 2U && h.bounds_max == m.bounds_max &&
              h.bounds_min == m.bounds_min && h.center == m.center && h.radius == m.radius,
          "header fields");
  require(view.skeleton_id() == m.skeleton_id, "skeleton id");
  for (std::uint32_t gi = 0; gi < 2; ++gi) {
    const cc::MeshGroupData& src = m.groups[gi];
    const g::MeshGroupRecord rec = view.group(gi);
    require(view.layout(rec) == src.layout, gi, "layout");
    require(rec.vertex_count == src.vertex_count && rec.first_draw == src.first_draw &&
                rec.draw_count == src.draw_count && rec.morph_set == src.morph_set,
            gi, "group fields");
    require(same(view.stream_bytes(rec, 0), src.stream0) &&
                aligned(view.stream_bytes(rec, 0), payload),
            gi, "stream 0 bytes");
    if (src.layout.stream_count == 2U) {
      require(same(view.stream_bytes(rec, 1), src.stream1), gi, "stream 1 bytes");
    }
    std::vector<std::uint16_t> indices;
    const std::span<const std::byte> ib = view.index_bytes(rec);
    indices.reserve(ib.size() / 2U);
    for (std::size_t i = 0; i < ib.size(); i += 2) {
      indices.push_back(static_cast<std::uint16_t>(std::to_integer<unsigned>(ib[i]) |
                                                   (std::to_integer<unsigned>(ib[i + 1]) << 8U)));
    }
    require(indices == src.indices, gi, "16-bit little-endian indices");
  }
  for (std::uint32_t d = 0; d < 3; ++d) {
    const g::MeshDrawRecord x = view.draw(d);
    const g::MeshDrawRecord& y = m.draws[d];
    require(x.base_vertex == y.base_vertex && x.min_index == y.min_index &&
                x.vertex_count == y.vertex_count && x.first_index == y.first_index &&
                x.index_count == y.index_count && x.material == y.material &&
                x.bone_map == y.bone_map,
            d, "draw");
  }
  for (std::uint32_t b = 0; b < 2; ++b) {
    const g::MeshBoneMapRecord map = view.bone_map(b);
    std::vector<std::uint32_t> entries;
    entries.reserve(map.entry_count);
    for (std::uint32_t e = 0; e < map.entry_count; ++e) {
      entries.push_back(view.bone_entry(map.first_entry + e));
    }
    require(entries == m.bone_maps[b], b, "bone map entries");
  }

  std::vector<Section> sections{
      {0, sizeof(g::MeshPayloadHeader)},
      {h.groups_offset, h.group_count * sizeof(g::MeshGroupRecord)},
      {h.draws_offset, h.draw_count * sizeof(g::MeshDrawRecord)},
      {h.bone_maps_offset, h.bone_map_count * sizeof(g::MeshBoneMapRecord)},
      {h.bone_entries_offset, h.bone_entry_count * sizeof(std::uint32_t)},
      {h.strings_offset, h.string_bytes}};
  for (std::uint32_t gi = 0; gi < h.group_count; ++gi) {
    const g::MeshGroupRecord rec = view.group(gi);
    sections.push_back({rec.stream0_offset, rec.stream0_size});
    sections.push_back({rec.stream1_offset, rec.stream1_size});
    sections.push_back({rec.index_offset, rec.index_count * sizeof(std::uint16_t)});
  }
  const Padding padding = padding_of(payload, sections);
  require(padding.count > 0U && padding.zero, "the padding between sections is zero");
  cc::EncodeFailure again;
  require(cc::encode_mesh_asset(mesh_data(), again) == payload, "equal data, equal bytes");
}

void morph_round_trip() {
  cc::MorphTargetsData set;
  set.group = 0;
  set.vertex_count = 4;
  set.targets = {{"smile", counting(32, 1)}, {"blink", counting(32, 50)}};
  cc::EncodeFailure failure;
  const Bytes payload = cc::encode_morph_targets(set, failure);
  require(failure.ok() && !payload.empty(), "the morph set encodes");
  g::MorphTargetsView view;
  require(g::decode_morph_targets(payload, view).error == g::MorphTargetsError::None, "decodes");
  require(view.header().group == 0U && view.header().vertex_count == 4U &&
              view.target_count() == 2U,
          "header");
  for (std::uint32_t t = 0; t < 2; ++t) {
    require(view.name(t) == set.targets[t].name && same(view.deltas(t), set.targets[t].deltas), t,
            "target name and deltas");
  }
  const g::MorphTargetsPayloadHeader& h = view.header();
  const std::array<Section, 4> sections{{
      {0, sizeof(g::MorphTargetsPayloadHeader)},
      {h.targets_offset, h.target_count * sizeof(g::MorphTargetRecord)},
      {h.deltas_offset,
       static_cast<std::size_t>(h.target_count) * h.vertex_count * g::kMorphDeltaBytes},
      {h.strings_offset, h.string_bytes},
  }};
  const Padding padding = padding_of(payload, sections);
  require(padding.count > 0U && padding.zero, "the padding between sections is zero");
  cc::EncodeFailure again;
  require(cc::encode_morph_targets(set, again) == payload, "equal data, equal bytes");
}

// A 4x4 cube in R8G8B8A8Unorm: six faces of three mips, each subresource's
// bytes counting from a different start.
cc::TextureAssetData cube_data() {
  cc::TextureAssetData t;
  t.width = 4;
  t.height = 4;
  t.faces = 6;
  t.mip_levels = 3;
  t.format = r::TextureFormat::R8G8B8A8Unorm;
  t.dimension = r::TextureAssetDimension::Cube;
  for (std::uint32_t face = 0; face < 6; ++face) {
    for (std::uint32_t mip = 0; mip < 3; ++mip) {
      const std::uint32_t side = 4U >> mip;
      cc::SubresourceData s;
      s.row_pitch = side * 4U;
      s.bytes = counting(static_cast<std::size_t>(side) * side * 4U,
                         static_cast<std::uint8_t>((face * 16U) + mip));
      t.subresources.push_back(s);
    }
  }
  return t;
}

void texture_round_trip() {
  cc::TextureAssetData t = cube_data();
  t.flags = 0;
  cc::EncodeFailure failure;
  const Bytes payload = cc::encode_texture_asset(t, failure);
  require(failure.ok() && !payload.empty(), "the cube encodes");
  r::TextureAssetView view;
  require(r::decode_texture_asset(payload, view).error == r::TextureAssetError::None, "decodes");
  const r::TexturePayloadHeader& h = view.header();
  require(h.width == 4U && h.height == 4U && h.faces == 6U && h.mip_levels == 3U &&
              view.format() == t.format && view.dimension() == t.dimension && h.flags == 0U &&
              view.subresource_count() == 18U,
          "header");
  for (std::uint32_t i = 0; i < 18; ++i) {
    const bw::rhi::SubresourceData s = view.subresource(i);
    require(same(s.bytes, t.subresources[i].bytes) && s.row_pitch == t.subresources[i].row_pitch &&
                aligned(s.bytes, payload),
            i, "subresource in slice-major, mip-minor order");
  }
  std::vector<Section> sections{
      {0, sizeof(r::TexturePayloadHeader)},
      {h.subresources_offset, h.subresource_count * sizeof(r::SubresourceRecord)}};
  for (std::uint32_t i = 0; i < h.subresource_count; ++i) {
    const std::span<const std::byte> bytes = view.subresource(i).bytes;
    sections.push_back({offset_in(bytes, payload), bytes.size()});
  }
  const Padding padding = padding_of(payload, sections);
  require(padding.count > 0U && padding.zero, "the padding between sections is zero");
  cc::EncodeFailure again;
  require(cc::encode_texture_asset(t, again) == payload, "equal data, equal bytes");
}

cc::MaterialAssetData material_data() {
  cc::MaterialAssetData m;
  m.template_name = std::string(mat::kLitTemplate);
  m.switches = {{std::string(mat::kNormalMapSwitch), true},
                {std::string(mat::kOcclusionReflectanceMapSwitch), false}};
  m.scalars = {{std::string(mat::kRoughness), 0.25F}, {std::string(mat::kReflectance), 3.0F}};
  m.vectors = {{std::string(mat::kBaseColorTint), {4.0F, 0.85F, 0.15F, 0.0F}},
               {std::string(mat::kEmissive), {40.0F, 0.1F, 0.0F, 0.0F}}};
  m.textures = {{std::string(mat::kBaseColorTexture), "model.pw0.texture.dds.pw0_body_m1_game_psd"},
                {std::string(mat::kNormalTexture), "model.pw0.texture.dds.pw0_body_n_game_psd"}};
  return m;
}

void material_round_trip() {
  const cc::MaterialAssetData m = material_data();
  cc::EncodeFailure failure;
  const Bytes payload = cc::encode_material_asset(m, failure);
  require(failure.ok() && !payload.empty(), "the material encodes");
  mat::MaterialAssetView view;
  require(mat::decode_material_asset(payload, view).error == mat::MaterialAssetError::None,
          "decodes");
  require(view.template_name() == m.template_name, "template name");
  const mat::MaterialPayloadHeader& h = view.header();
  require(h.switch_count == 2U && h.scalar_count == 2U && h.vector_count == 2U &&
              h.texture_count == 2U,
          "every record kind's count");
  for (std::uint32_t i = 0; i < 2; ++i) {
    const mat::SwitchRecord sw = view.switch_record(i);
    require(view.string(sw.name_offset, sw.name_length) == m.switches[i].name &&
                (sw.value == 1U) == m.switches[i].value,
            i, "switch in order");
    const mat::ScalarRecord sc = view.scalar_record(i);
    require(view.string(sc.name_offset, sc.name_length) == m.scalars[i].name &&
                sc.value == m.scalars[i].value,
            i, "scalar in order");
    const mat::VectorRecord ve = view.vector_record(i);
    require(view.string(ve.name_offset, ve.name_length) == m.vectors[i].name &&
                ve.value == m.vectors[i].value,
            i, "vector in order");
    const mat::TextureRecord te = view.texture_record(i);
    require(view.string(te.name_offset, te.name_length) == m.textures[i].name &&
                view.string(te.id_offset, te.id_length) == m.textures[i].id,
            i, "texture in order");
  }
  const std::array<Section, 6> sections{{
      {0, sizeof(mat::MaterialPayloadHeader)},
      {h.switches_offset, h.switch_count * sizeof(mat::SwitchRecord)},
      {h.scalars_offset, h.scalar_count * sizeof(mat::ScalarRecord)},
      {h.vectors_offset, h.vector_count * sizeof(mat::VectorRecord)},
      {h.textures_offset, h.texture_count * sizeof(mat::TextureRecord)},
      {h.strings_offset, h.string_bytes},
  }};
  const Padding padding = padding_of(payload, sections);
  require(padding.count > 0U && padding.zero, "the padding between sections is zero");
  cc::EncodeFailure again;
  require(cc::encode_material_asset(m, again) == payload, "equal data, equal bytes");
}

cc::SkeletonAssetData skeleton_data() {
  cc::SkeletonAssetData s;
  s.bind_block = a::BindBlock::B;
  for (std::uint32_t k = 0; k < 3; ++k) {
    cc::BoneData b;
    b.name = k == 0U ? "N_ALL" : (k == 1U ? "Waist_direction" : "HipsBone");
    b.id = 0x9461U + k;
    b.parent = static_cast<std::int32_t>(k) - 1;
    b.field_12 = k;
    b.field_20 = k == 2U ? 3 : -1;
    const auto f = static_cast<float>(k);
    b.a = {{f, 0.5F, 0.25F, 1.0F}, {f, 1.0F, 2.0F}, {1.0F, 1.0F, 1.0F}};
    b.b = {{0.0F, f, 0.0F, 1.0F}, {f, 3.0F, 4.0F}, {1.0F, 2.0F, 1.0F}};
    s.bones.push_back(b);
  }
  return s;
}

void skeleton_round_trip() {
  const cc::SkeletonAssetData s = skeleton_data();
  cc::EncodeFailure failure;
  const Bytes payload = cc::encode_skeleton_asset(s, failure);
  require(failure.ok() && !payload.empty(), "the skeleton encodes");
  a::SkeletonAssetView view;
  require(a::decode_skeleton_asset(payload, view).error == a::SkeletonAssetError::None, "decodes");
  require(view.bind_block() == a::BindBlock::B && view.bone_count() == 3U, "bind B, three bones");
  for (std::uint32_t k = 0; k < 3; ++k) {
    const a::BoneRecord b = view.bone(k);
    const cc::BoneData& src = s.bones[k];
    require(view.name(k) == src.name && b.id == src.id && b.parent == src.parent &&
                b.field_12 == src.field_12 && b.field_20 == src.field_20,
            k, "bone fields");
    require(b.a.rotation == src.a.rotation && b.a.translation == src.a.translation &&
                b.a.scale == src.a.scale && b.b.rotation == src.b.rotation &&
                b.b.translation == src.b.translation && b.b.scale == src.b.scale,
            k, "both transform blocks as given");
  }
  const a::SkeletonPayloadHeader& h = view.header();
  const std::array<Section, 3> sections{{
      {0, sizeof(a::SkeletonPayloadHeader)},
      {h.bones_offset, h.bone_count * sizeof(a::BoneRecord)},
      {h.strings_offset, h.string_bytes},
  }};
  const Padding padding = padding_of(payload, sections);
  require(padding.count > 0U && padding.zero, "the padding between sections is zero");
  cc::EncodeFailure again;
  require(cc::encode_skeleton_asset(s, again) == payload, "equal data, equal bytes");
}

// (asset_data.hpp, encode_mesh_asset, encode_morph_targets,
// encode_texture_asset, encode_material_asset, encode_skeleton_asset: the
// round trip, the zero padding between sections, equal data giving equal
// bytes, and the refusal with the decoder's error, index and material
// table; each layout header's decode_*)
void round_trips() {
  mesh_round_trip();
  morph_round_trip();
  texture_round_trip();
  material_round_trip();
  skeleton_round_trip();

  // An encoder refuses what its decoder would refuse, naming the decoder's
  // error and index (and, for a material, the table), and returns no bytes.
  struct Refusal {
    std::string_view name;
    Bytes bytes;
    cc::EncodeFailure failure;
    std::uint32_t error;
    std::uint32_t index;
    std::uint32_t table;
  };
  std::vector<Refusal> refusals;
  {
    cc::MeshAssetData m = mesh_data();
    m.draws[1].material = 2;
    Refusal x{"mesh draw material",
              {},
              {},
              static_cast<std::uint32_t>(g::MeshAssetError::MaterialOutOfRange),
              1,
              0};
    x.bytes = cc::encode_mesh_asset(m, x.failure);
    refusals.push_back(x);
  }
  {
    // Group 1 has one stream; the layout has no place for stream 1 bytes.
    cc::MeshAssetData m = mesh_data();
    m.groups[1].stream1 = counting(12, 1);
    Refusal x{"one-stream group with stream 1 bytes",
              {},
              {},
              static_cast<std::uint32_t>(g::MeshAssetError::StreamSizeMismatch),
              1,
              0};
    x.bytes = cc::encode_mesh_asset(m, x.failure);
    refusals.push_back(x);
  }
  {
    cc::MorphTargetsData set{0, 4, {{"smile", counting(32, 1)}, {"blink", counting(24, 1)}}};
    Refusal x{"morph delta bytes",
              {},
              {},
              static_cast<std::uint32_t>(g::MorphTargetsError::DeltaSizeMismatch),
              1,
              0};
    x.bytes = cc::encode_morph_targets(set, x.failure);
    refusals.push_back(x);
  }
  {
    cc::TextureAssetData t = cube_data();
    t.mip_levels = 2;
    Refusal x{"texture mip count",
              {},
              {},
              static_cast<std::uint32_t>(r::TextureAssetError::MipCountInvalid),
              0,
              0};
    x.bytes = cc::encode_texture_asset(t, x.failure);
    refusals.push_back(x);
  }
  {
    // material_asset.hpp numbers the tables 0 switches, 1 scalars.
    cc::MaterialAssetData m = material_data();
    m.switches[1].name = m.switches[0].name;
    Refusal x{"material duplicate switch name",
              {},
              {},
              static_cast<std::uint32_t>(mat::MaterialAssetError::NameDuplicate),
              1,
              0};
    x.bytes = cc::encode_material_asset(m, x.failure);
    refusals.push_back(x);
  }
  {
    cc::MaterialAssetData m = material_data();
    m.scalars[1].name = m.scalars[0].name;
    Refusal x{"material duplicate scalar name",
              {},
              {},
              static_cast<std::uint32_t>(mat::MaterialAssetError::NameDuplicate),
              1,
              1};
    x.bytes = cc::encode_material_asset(m, x.failure);
    refusals.push_back(x);
  }
  {
    cc::SkeletonAssetData s = skeleton_data();
    s.bones[2].parent = -1;
    Refusal x{"skeleton roots",
              {},
              {},
              static_cast<std::uint32_t>(a::SkeletonAssetError::RootCountInvalid),
              0,
              0};
    x.bytes = cc::encode_skeleton_asset(s, x.failure);
    refusals.push_back(x);
  }
  for (std::size_t i = 0; i < refusals.size(); ++i) {
    const Refusal& x = refusals[i];
    require(x.bytes.empty() && x.failure.error == x.error && x.failure.index == x.index &&
                x.failure.table == x.table,
            i, x.name);
  }
}

constexpr std::array<bw::testing::Scenario<void>, 1> kScenarios{{
    {"round_trips", round_trips},
}};

} // namespace

int main(int argc, char** argv) { return bw::testing::run_scenario(argc, argv, kScenarios); }
