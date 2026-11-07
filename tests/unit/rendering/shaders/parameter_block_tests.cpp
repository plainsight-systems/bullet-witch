// Unit test: public contracts of bw/rendering/shaders/parameter_block.hpp, one
// scenario per CTest entry. Each scenario names the header declaration it asserts.
#include "bw/rendering/shaders/parameter_block.hpp"
#include "bw/rendering/shaders/permutation.hpp"

#include "bw/testing/harness.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fixture {
using bw::shaders::Array;
using bw::shaders::Float2;
using bw::shaders::Float3;
using bw::shaders::Float4x4;
using bw::shaders::Int4;
using bw::shaders::SamplerIndex;
using bw::shaders::StorageBufferIndex;
using bw::shaders::StorageTextureIndex;
using bw::shaders::TextureIndex;

// tint 0 (C++ 16 bytes, HLSL 12), weight 16; size 32.
struct alignas(16) InnerBlock {
  Float3 tint{};
  float weight{};
};

// world_to_clip 0, eye 64, near_plane 80, offsets 96 (3 x 16), inner 144,
// rim 176, weights 208 (2 x 16), albedo 240, linear_sampler 244, flags 256;
// size 272.
struct alignas(16) OuterBlock {
  Float4x4 world_to_clip{};
  Float3 eye{};
  float near_plane{};
  Array<Float2, 3> offsets{};
  InnerBlock inner{};
  InnerBlock rim{};
  Array<float, 2> weights{};
  TextureIndex albedo{};
  SamplerIndex linear_sampler{};
  Int4 flags{};
};

// An array of blocks and the two storage index kinds: items 0 (2 x 32),
// output 64, counters 68; size 80.
struct alignas(16) ListBlock {
  Array<InnerBlock, 2> items{};
  StorageTextureIndex output{};
  StorageBufferIndex counters{};
};

// The kinds the two blocks above lack: count 0, mask 4, data 8, uv 16,
// color 32, ids 48; size 64.
struct alignas(16) KindsBlock {
  std::int32_t count{};
  std::uint32_t mask{};
  bw::shaders::BufferIndex data{};
  Float2 uv{};
  bw::shaders::Float4 color{};
  bw::shaders::Uint4 ids{};
};
} // namespace fixture

using fixture::InnerBlock;
using fixture::KindsBlock;
using fixture::ListBlock;
using fixture::OuterBlock;

BW_PARAMETER_BLOCK_LAYOUT(InnerBlock, BW_PARAMETER_MEMBER(InnerBlock, weight),
                          BW_PARAMETER_MEMBER(InnerBlock, tint));
// Listed out of declaration order on purpose: the layout sorts by offset.
BW_PARAMETER_BLOCK_LAYOUT(
    OuterBlock, BW_PARAMETER_MEMBER(OuterBlock, flags), BW_PARAMETER_MEMBER(OuterBlock, inner),
    BW_PARAMETER_MEMBER(OuterBlock, world_to_clip), BW_PARAMETER_MEMBER(OuterBlock, weights),
    BW_PARAMETER_MEMBER(OuterBlock, near_plane), BW_PARAMETER_MEMBER(OuterBlock, albedo),
    BW_PARAMETER_MEMBER(OuterBlock, offsets), BW_PARAMETER_MEMBER(OuterBlock, rim),
    BW_PARAMETER_MEMBER(OuterBlock, linear_sampler), BW_PARAMETER_MEMBER(OuterBlock, eye));
BW_PARAMETER_BLOCK_LAYOUT(ListBlock, BW_PARAMETER_MEMBER(ListBlock, counters),
                          BW_PARAMETER_MEMBER(ListBlock, items),
                          BW_PARAMETER_MEMBER(ListBlock, output));
BW_PARAMETER_BLOCK_LAYOUT(KindsBlock, BW_PARAMETER_MEMBER(KindsBlock, count),
                          BW_PARAMETER_MEMBER(KindsBlock, mask),
                          BW_PARAMETER_MEMBER(KindsBlock, data),
                          BW_PARAMETER_MEMBER(KindsBlock, uv),
                          BW_PARAMETER_MEMBER(KindsBlock, color),
                          BW_PARAMETER_MEMBER(KindsBlock, ids));

namespace {
using bw::shaders::BlockLayout;
using bw::shaders::BlockLayoutOf;
using bw::shaders::HlslError;
using bw::shaders::HlslPlacement;
using bw::shaders::HlslResult;
using bw::shaders::LayoutHash;
using bw::shaders::MemberLayout;
using bw::shaders::ParameterKind;

using bw::testing::require;

const BlockLayout& inner_layout() { return BlockLayoutOf<InnerBlock>::value; }
const BlockLayout& outer_layout() { return BlockLayoutOf<OuterBlock>::value; }
const BlockLayout& list_layout() { return BlockLayoutOf<ListBlock>::value; }

struct ExpectedMember {
  std::string_view name;
  std::size_t offset;
  std::size_t size;
  ParameterKind kind;
  std::uint32_t element_count;
  const BlockLayout* nested;
};

// (parameter_block.hpp, BW_PARAMETER_BLOCK_LAYOUT), (parameter_block.hpp, MemberLayout) and
// (parameter_block.hpp, BlockLayout) (the preconditions' failing side is
// the compile probes under compile_failures/, tests/CMakeLists.txt): preconditions hold and members
// are sorted by offset, each at its C++ offsetof with its sizeof, kind from the declared type,
// element count N for Array<T, N>, nested layout for blocks; size is sizeof(Block).
void dense_layout_offsets_equal_offsetof() {
  const std::array<ExpectedMember, 10> outer{{
      {"world_to_clip", offsetof(OuterBlock, world_to_clip), sizeof(OuterBlock::world_to_clip),
       ParameterKind::Float4x4, 0, nullptr},
      {"eye", offsetof(OuterBlock, eye), sizeof(OuterBlock::eye), ParameterKind::Float3, 0,
       nullptr},
      {"near_plane", offsetof(OuterBlock, near_plane), sizeof(OuterBlock::near_plane),
       ParameterKind::Float, 0, nullptr},
      {"offsets", offsetof(OuterBlock, offsets), sizeof(OuterBlock::offsets), ParameterKind::Float2,
       3, nullptr},
      {"inner", offsetof(OuterBlock, inner), sizeof(OuterBlock::inner), ParameterKind::Block, 0,
       &inner_layout()},
      {"rim", offsetof(OuterBlock, rim), sizeof(OuterBlock::rim), ParameterKind::Block, 0,
       &inner_layout()},
      {"weights", offsetof(OuterBlock, weights), sizeof(OuterBlock::weights), ParameterKind::Float,
       2, nullptr},
      {"albedo", offsetof(OuterBlock, albedo), sizeof(OuterBlock::albedo),
       ParameterKind::TextureIndex, 0, nullptr},
      {"linear_sampler", offsetof(OuterBlock, linear_sampler), sizeof(OuterBlock::linear_sampler),
       ParameterKind::SamplerIndex, 0, nullptr},
      {"flags", offsetof(OuterBlock, flags), sizeof(OuterBlock::flags), ParameterKind::Int4, 0,
       nullptr},
  }};
  const BlockLayout& layout = outer_layout();
  require(layout.name == "OuterBlock", "block name is the type name");
  require(layout.size == sizeof(OuterBlock), "block size is sizeof");
  require(layout.size % 16U == 0U, "block size is a multiple of 16");
  require(layout.members.size() == outer.size(), "every member is listed once");
  for (std::size_t i = 0; i < outer.size(); ++i) {
    const MemberLayout& member = layout.members[i];
    require(member.name == outer[i].name, "members are sorted by offset");
    require(member.offset == outer[i].offset, "member offset is offsetof");
    require(member.size == outer[i].size, "member size is sizeof");
    require(member.kind == outer[i].kind, "member kind follows the declared type");
    require(member.element_count == outer[i].element_count, "array element count");
    require(member.nested == outer[i].nested, "nested layout observed");
  }
  const BlockLayout& inner = inner_layout();
  require(inner.size == sizeof(InnerBlock), "nested block size is sizeof");
  require(inner.members.size() == 2U && inner.members[0].name == "tint" &&
              inner.members[0].offset == offsetof(InnerBlock, tint) &&
              inner.members[1].name == "weight" &&
              inner.members[1].offset == offsetof(InnerBlock, weight),
          "nested block members sorted at offsetof");
  const BlockLayout& list = list_layout();
  require(list.size == sizeof(ListBlock) && list.members.size() == 3U, "list block");
  require(list.members[0].name == "items" && list.members[0].kind == ParameterKind::Block &&
              list.members[0].element_count == 2U && list.members[0].nested == &inner_layout() &&
              list.members[0].offset == offsetof(ListBlock, items),
          "an array of blocks records kind Block, the count and the element layout");
  require(list.members[1].kind == ParameterKind::StorageTextureIndex &&
              list.members[1].offset == offsetof(ListBlock, output) &&
              list.members[2].kind == ParameterKind::StorageBufferIndex &&
              list.members[2].offset == offsetof(ListBlock, counters),
          "storage index kinds");
}

// (parameter_block.hpp, place_in_hlsl): after padding from the previous member's HLSL
// end, place_in_hlsl lands on the C++ offset, for every member of every
// declared block: the walk descends into each nested block, including the
// element block of an array of blocks, and covers the registry's blocks.
void check_placement(const BlockLayout& layout) {
  std::uint32_t running = 0;
  for (const MemberLayout& member : layout.members) {
    const HlslPlacement from_previous = bw::shaders::place_in_hlsl(member, running);
    require(from_previous.offset >= running, "placement never moves backwards");
    require(from_previous.offset <= member.offset, "padding can reach the C++ offset");
    const HlslPlacement padded = bw::shaders::place_in_hlsl(member, member.offset);
    require(padded.offset == member.offset, "HLSL placement equals the C++ offset");
    require(padded.size <= member.size, "HLSL extent fits the C++ member");
    running = padded.offset + padded.size;
    if (member.nested != nullptr) {
      check_placement(*member.nested);
    }
  }
  require(running <= layout.size, "HLSL extent fits the block");
}

void hlsl_placement_matches_cpp_offsets() {
  check_placement(outer_layout());
  check_placement(list_layout());
  for (const bw::shaders::ShaderDescription& shader : bw::shaders::shader_registry()) {
    for (const BlockLayout* block : shader.static_blocks) {
      check_placement(*block);
    }
    if (shader.draw_block != nullptr) {
      check_placement(*shader.draw_block);
    }
  }
  // Sizes DXC assigns (parameter_block.hpp, kind_hlsl_size): float3 12, matrix 64,
  // array (N - 1) * 16 + element, an array of blocks (N - 1) * stride + size.
  const BlockLayout& layout = outer_layout();
  require(bw::shaders::place_in_hlsl(layout.members[1], 64).size == 12U, "float3 is 12 bytes");
  require(bw::shaders::place_in_hlsl(layout.members[0], 0).size == 64U, "float4x4 is 64 bytes");
  require(bw::shaders::place_in_hlsl(layout.members[3], 96).size == 40U, "float2[3] is 40 bytes");
  require(bw::shaders::place_in_hlsl(layout.members[6], 208).size == 20U, "float[2] is 20 bytes");
  require(bw::shaders::place_in_hlsl(list_layout().members[0], 0).size == 64U,
          "InnerBlock[2] is 64 bytes");
}

// (parameter_block.hpp, layout_hash): the hash is a function of the members in
// offset order, whatever order the macro listed them in.
void hash_stable_under_member_reordering() {
  // Declaration order, independently of the macro's shuffled list.
  const std::array declared{
      BW_PARAMETER_MEMBER(OuterBlock, world_to_clip),  BW_PARAMETER_MEMBER(OuterBlock, eye),
      BW_PARAMETER_MEMBER(OuterBlock, near_plane),     BW_PARAMETER_MEMBER(OuterBlock, offsets),
      BW_PARAMETER_MEMBER(OuterBlock, inner),          BW_PARAMETER_MEMBER(OuterBlock, rim),
      BW_PARAMETER_MEMBER(OuterBlock, weights),        BW_PARAMETER_MEMBER(OuterBlock, albedo),
      BW_PARAMETER_MEMBER(OuterBlock, linear_sampler), BW_PARAMETER_MEMBER(OuterBlock, flags),
  };
  const LayoutHash hash = bw::shaders::layout_hash(
      "OuterBlock", static_cast<std::uint32_t>(sizeof(OuterBlock)), declared);
  require(hash == outer_layout().hash, "hash equals the hash of the sorted member list");
  require(bw::shaders::layout_hash(inner_layout().name, inner_layout().size,
                                   inner_layout().members) == inner_layout().hash,
          "nested hash is the hash of its sorted members");
}

// (parameter_block.hpp, layout_hash): any change of a member's name, kind, offset,
// count or nested layout, of the block name or of its size, changes the hash.
void hash_changes_on_any_member_change() {
  const BlockLayout& layout = outer_layout();
  std::vector<MemberLayout> base(layout.members.begin(), layout.members.end());
  const LayoutHash original = bw::shaders::layout_hash(layout.name, layout.size, base);
  require(original == layout.hash, "baseline hash");
  const auto changed = [&](auto mutate) {
    std::vector<MemberLayout> copy = base;
    mutate(copy);
    return bw::shaders::layout_hash(layout.name, layout.size, copy) != original;
  };
  require(changed([](std::vector<MemberLayout>& m) { m[2].name = "far_plane"; }), "name");
  require(changed([](std::vector<MemberLayout>& m) { m[2].kind = ParameterKind::Uint32; }), "kind");
  require(changed([](std::vector<MemberLayout>& m) { m[9].offset = 272; }), "offset");
  require(changed([](std::vector<MemberLayout>& m) { m[3].element_count = 4; }), "count");
  const BlockLayout other_nested{.name = "InnerBlock",
                                 .members = {},
                                 .hash = LayoutHash{inner_layout().hash.value + 1U},
                                 .size = inner_layout().size};
  require(changed([&](std::vector<MemberLayout>& m) { m[4].nested = &other_nested; }), "nested");
  require(bw::shaders::layout_hash("OuterBlock2", layout.size, base) != original, "block name");
  require(bw::shaders::layout_hash(layout.name, layout.size + 16U, base) != original, "block size");
}

// (parameter_block.hpp, append_hlsl_struct): exact text, nested blocks first and once per
// call, 4-byte padding to each C++ offset and to the block size, uint for the
// index kinds, "<type> <member>[<N>]" for arrays including arrays of blocks.
void generated_struct_text_is_exact() {
  const std::string_view expected = "struct BW_InnerBlock {\n"
                                    "  float3 tint;\n"
                                    "  uint bw_pad_12;\n"
                                    "  float weight;\n"
                                    "  uint bw_pad_20;\n"
                                    "  uint bw_pad_24;\n"
                                    "  uint bw_pad_28;\n"
                                    "};\n"
                                    "struct BW_OuterBlock {\n"
                                    "  float4x4 world_to_clip;\n"
                                    "  float3 eye;\n"
                                    "  uint bw_pad_76;\n"
                                    "  float near_plane;\n"
                                    "  uint bw_pad_84;\n"
                                    "  uint bw_pad_88;\n"
                                    "  uint bw_pad_92;\n"
                                    "  float2 offsets[3];\n"
                                    "  uint bw_pad_136;\n"
                                    "  uint bw_pad_140;\n"
                                    "  BW_InnerBlock inner;\n"
                                    "  BW_InnerBlock rim;\n"
                                    "  float weights[2];\n"
                                    "  uint bw_pad_228;\n"
                                    "  uint bw_pad_232;\n"
                                    "  uint bw_pad_236;\n"
                                    "  uint albedo;\n"
                                    "  uint linear_sampler;\n"
                                    "  uint bw_pad_248;\n"
                                    "  uint bw_pad_252;\n"
                                    "  int4 flags;\n"
                                    "};\n";
  std::string text = "// kept\n";
  const HlslResult result = bw::shaders::append_hlsl_struct(outer_layout(), text);
  require(result.error == HlslError::None, "a macro-built layout generates");
  require(result.member.empty(),
          "member is empty on None (parameter_block.hpp, HlslResult::member)");
  require(text == std::string("// kept\n") + std::string(expected), "generated text is exact");

  const std::string_view expected_list = "struct BW_InnerBlock {\n"
                                         "  float3 tint;\n"
                                         "  uint bw_pad_12;\n"
                                         "  float weight;\n"
                                         "  uint bw_pad_20;\n"
                                         "  uint bw_pad_24;\n"
                                         "  uint bw_pad_28;\n"
                                         "};\n"
                                         "struct BW_ListBlock {\n"
                                         "  BW_InnerBlock items[2];\n"
                                         "  uint output;\n"
                                         "  uint counters;\n"
                                         "  uint bw_pad_72;\n"
                                         "  uint bw_pad_76;\n"
                                         "};\n";
  std::string list_text;
  require(bw::shaders::append_hlsl_struct(list_layout(), list_text).error == HlslError::None,
          "a macro-built layout with an array of blocks generates");
  require(list_text == expected_list, "array of blocks and storage indices text is exact");
}

// (parameter_block.hpp, HlslError): a layout that did not come from the macros
// reports the named error; HlslResult::member names what failed
// (parameter_block.hpp, HlslResult::member): the member for a member-level
// error, the block name for an invalid block name. On an error `out` holds
// what was appended before the failing member, which the caller discards
// (parameter_block.hpp, HlslResult), so only the caller's own earlier text is
// asserted to remain at its front.
void expect_hlsl_error(const BlockLayout& layout, HlslError error, std::string_view member,
                       std::string_view what) {
  const std::string prefix = "// caller text\n";
  std::string text = prefix;
  const HlslResult result = bw::shaders::append_hlsl_struct(layout, text);
  require(result.error == error && result.member == member, what);
  require(text.starts_with(prefix), "the caller's earlier text is kept");
}

void hlsl_errors_named() {
  const std::array bad_name_members{
      MemberLayout{.name = "bad name", .offset = 0, .size = 4, .kind = ParameterKind::Float}};
  const BlockLayout bad_name{.name = "Hand", .members = bad_name_members, .hash = {}, .size = 16};
  expect_hlsl_error(bad_name, HlslError::InvalidName, "bad name", "invalid member name");
  const BlockLayout bad_block{.name = "9Hand", .members = {}, .hash = {}, .size = 16};
  expect_hlsl_error(bad_block, HlslError::InvalidName, "9Hand", "invalid block name");
  // A float4 at offset 8 sits below where HLSL places it (16).
  const std::array low_members{
      MemberLayout{.name = "a", .offset = 0, .size = 4, .kind = ParameterKind::Float},
      MemberLayout{.name = "b", .offset = 8, .size = 16, .kind = ParameterKind::Float4}};
  const BlockLayout low{.name = "Low", .members = low_members, .hash = {}, .size = 32};
  expect_hlsl_error(low, HlslError::OffsetBelowHlslPlacement, "b", "offset below placement");
  // A 64-byte matrix in a 16-byte block: its HLSL end lies past the size.
  const std::array long_members{
      MemberLayout{.name = "m", .offset = 0, .size = 64, .kind = ParameterKind::Float4x4}};
  const BlockLayout too_long{.name = "Short", .members = long_members, .hash = {}, .size = 16};
  expect_hlsl_error(too_long, HlslError::LayoutExceedsBlockSize, "m", "layout exceeds block size");
  // A Block member with no nested layout.
  const std::array orphan_members{MemberLayout{
      .name = "inner", .nested = nullptr, .offset = 0, .size = 16, .kind = ParameterKind::Block}};
  const BlockLayout orphan{.name = "Orphan", .members = orphan_members, .hash = {}, .size = 16};
  expect_hlsl_error(orphan, HlslError::MissingNestedLayout, "inner", "missing nested layout");
}

// (parameter_block.hpp, append_hlsl_buffer_loader): exact text for every
// load form of the header's table, nested loaders first and once per call,
// one line per array element at the C++ element stride (16 for a value, the
// block's size for an array of blocks), a nested block loaded at its offset.
void buffer_loader_text_is_exact() {
  const std::string_view inner = "BW_InnerBlock bw_load_InnerBlock(uint buffer, uint offset) {\n"
                                 "  BW_InnerBlock block = (BW_InnerBlock)0;\n"
                                 "  block.tint = asfloat(BW_BUFFER(buffer).Load3(offset + 0));\n"
                                 "  block.weight = asfloat(BW_BUFFER(buffer).Load(offset + 16));\n"
                                 "  return block;\n"
                                 "}\n";
  const std::string expected_outer =
      std::string(inner) +
      "BW_OuterBlock bw_load_OuterBlock(uint buffer, uint offset) {\n"
      "  BW_OuterBlock block = (BW_OuterBlock)0;\n"
      "  block.world_to_clip = float4x4(asfloat(BW_BUFFER(buffer).Load4(offset + 0)), "
      "asfloat(BW_BUFFER(buffer).Load4(offset + 16)), asfloat(BW_BUFFER(buffer).Load4(offset + "
      "32)), asfloat(BW_BUFFER(buffer).Load4(offset + 48)));\n"
      "  block.eye = asfloat(BW_BUFFER(buffer).Load3(offset + 64));\n"
      "  block.near_plane = asfloat(BW_BUFFER(buffer).Load(offset + 80));\n"
      "  block.offsets[0] = asfloat(BW_BUFFER(buffer).Load2(offset + 96));\n"
      "  block.offsets[1] = asfloat(BW_BUFFER(buffer).Load2(offset + 112));\n"
      "  block.offsets[2] = asfloat(BW_BUFFER(buffer).Load2(offset + 128));\n"
      "  block.inner = bw_load_InnerBlock(buffer, offset + 144);\n"
      "  block.rim = bw_load_InnerBlock(buffer, offset + 176);\n"
      "  block.weights[0] = asfloat(BW_BUFFER(buffer).Load(offset + 208));\n"
      "  block.weights[1] = asfloat(BW_BUFFER(buffer).Load(offset + 224));\n"
      "  block.albedo = BW_BUFFER(buffer).Load(offset + 240);\n"
      "  block.linear_sampler = BW_BUFFER(buffer).Load(offset + 244);\n"
      "  block.flags = asint(BW_BUFFER(buffer).Load4(offset + 256));\n"
      "  return block;\n"
      "}\n";
  const std::string expected_list = std::string(inner) +
                                    "BW_ListBlock bw_load_ListBlock(uint buffer, uint offset) {\n"
                                    "  BW_ListBlock block = (BW_ListBlock)0;\n"
                                    "  block.items[0] = bw_load_InnerBlock(buffer, offset + 0);\n"
                                    "  block.items[1] = bw_load_InnerBlock(buffer, offset + 32);\n"
                                    "  block.output = BW_BUFFER(buffer).Load(offset + 64);\n"
                                    "  block.counters = BW_BUFFER(buffer).Load(offset + 68);\n"
                                    "  return block;\n"
                                    "}\n";
  const std::string_view expected_kinds =
      "BW_KindsBlock bw_load_KindsBlock(uint buffer, uint offset) {\n"
      "  BW_KindsBlock block = (BW_KindsBlock)0;\n"
      "  block.count = asint(BW_BUFFER(buffer).Load(offset + 0));\n"
      "  block.mask = BW_BUFFER(buffer).Load(offset + 4);\n"
      "  block.data = BW_BUFFER(buffer).Load(offset + 8);\n"
      "  block.uv = asfloat(BW_BUFFER(buffer).Load2(offset + 16));\n"
      "  block.color = asfloat(BW_BUFFER(buffer).Load4(offset + 32));\n"
      "  block.ids = BW_BUFFER(buffer).Load4(offset + 48);\n"
      "  return block;\n"
      "}\n";
  const std::array<std::pair<const BlockLayout*, std::string_view>, 3> rows{{
      {&outer_layout(), expected_outer},
      {&list_layout(), expected_list},
      {&BlockLayoutOf<KindsBlock>::value, expected_kinds},
  }};
  for (std::size_t i = 0; i < rows.size(); ++i) {
    std::string text;
    const HlslResult result = bw::shaders::append_hlsl_buffer_loader(*rows[i].first, text);
    require(result.error == HlslError::None && text == rows[i].second, i,
            "the loader text is exact");
  }
}

// (parameter_block.hpp, append_hlsl_block_binding): the binding line for each slot kind.
void binding_line_text() {
  std::string text;
  bw::shaders::append_hlsl_block_binding(outer_layout(), true, 3, text);
  bw::shaders::append_hlsl_block_binding(inner_layout(), false, 7, text);
  require(text == "BW_STATIC_BLOCK(BW_OuterBlock, OuterBlock, 3)\n"
                  "BW_DRAW_BLOCK(BW_InnerBlock, InnerBlock)\n",
          "binding lines are exact");
}

using Scenario = bw::testing::Scenario<void>;
constexpr std::array scenarios{
    Scenario{"dense_layout_offsets_equal_offsetof", dense_layout_offsets_equal_offsetof},
    Scenario{"hlsl_placement_matches_cpp_offsets", hlsl_placement_matches_cpp_offsets},
    Scenario{"hash_stable_under_member_reordering", hash_stable_under_member_reordering},
    Scenario{"hash_changes_on_any_member_change", hash_changes_on_any_member_change},
    Scenario{"generated_struct_text_is_exact", generated_struct_text_is_exact},
    Scenario{"hlsl_errors_named", hlsl_errors_named},
    Scenario{"binding_line_text", binding_line_text},
    Scenario{"buffer_loader_text_is_exact", buffer_loader_text_is_exact},
};
} // namespace

int main(int argc, char** argv) { return bw::testing::run_scenario(argc, argv, scenarios); }
