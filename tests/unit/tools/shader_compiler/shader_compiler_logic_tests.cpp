// Unit test: public contracts of tools/shader_compiler's pure library,
// bw/shader_compiler/logic.hpp and bw/shader_compiler/enumeration.hpp, one
// scenario per CTest entry. Nothing here starts a process, opens a file or
// loads a toolchain. Each scenario names the header declaration it asserts.
#include "bw/rendering/shaders/artifact.hpp"
#include "bw/rendering/shaders/compiler_options.hpp"
#include "bw/rendering/shaders/parameter_block.hpp"
#include "bw/rendering/shaders/permutation.hpp"
#include "bw/rendering/shaders/root_layout.hpp"
#include "bw/shader_compiler/enumeration.hpp"
#include "bw/shader_compiler/logic.hpp"

#include "bw/testing/harness.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

// Test blocks. TestDraw: tint 0, texture 16, size 32. TestView: view 0,
// scale 64, size 80. TestOuterA and TestOuterB both nest TestInner.
struct alignas(16) TestDraw {
  bw::shaders::Float4 tint;
  bw::shaders::TextureIndex texture;
};
struct alignas(16) TestView {
  bw::shaders::Float4x4 view;
  float scale{};
};
struct alignas(16) TestInner {
  bw::shaders::Float4 value;
};
struct alignas(16) TestOuterA {
  TestInner inner;
};
struct alignas(16) TestOuterB {
  TestInner inner;
};

} // namespace

BW_PARAMETER_BLOCK_LAYOUT(TestDraw, BW_PARAMETER_MEMBER(TestDraw, tint),
                          BW_PARAMETER_MEMBER(TestDraw, texture));
BW_PARAMETER_BLOCK_LAYOUT(TestView, BW_PARAMETER_MEMBER(TestView, view),
                          BW_PARAMETER_MEMBER(TestView, scale));
BW_PARAMETER_BLOCK_LAYOUT(TestInner, BW_PARAMETER_MEMBER(TestInner, value));
BW_PARAMETER_BLOCK_LAYOUT(TestOuterA, BW_PARAMETER_MEMBER(TestOuterA, inner));
BW_PARAMETER_BLOCK_LAYOUT(TestOuterB, BW_PARAMETER_MEMBER(TestOuterB, inner));

namespace {

using bw::shader_compiler::BackendReflection;
using bw::shader_compiler::BlockBinding;
using bw::shader_compiler::BlockBindingKind;
using bw::shader_compiler::BoundBlock;
using bw::shader_compiler::IncludeError;
using bw::shader_compiler::ReflectionError;
using bw::shader_compiler::Semantic;
using bw::shaders::BlockLayout;
using bw::shaders::BlockLayoutOf;
using bw::shaders::DescriptorArray;
using bw::shaders::PermutationId;
using bw::shaders::ShaderDescription;
using bw::shaders::ShaderStage;
using bw::shaders::ShaderTarget;

using bw::testing::require;

// Filled from the implementation once; see input_hash_items.
constexpr std::string_view kReferenceInputHash =
    "7b76fe96f69c2eb0fe75c6afa6237cc20f68f1d94bc32ddd9ae7f399d47d8cac";

constexpr std::array<const BlockLayout*, 1> kViewStatic{&BlockLayoutOf<TestView>::value};
constexpr std::array<const BlockLayout*, 1> kNullStatic{nullptr};
constexpr std::array<const BlockLayout*, 1> kDrawAsStatic{&BlockLayoutOf<TestDraw>::value};
constexpr std::array<const BlockLayout*, 1> kOuterBStatic{&BlockLayoutOf<TestOuterB>::value};

ShaderDescription description(std::span<const BlockLayout* const> statics,
                              const BlockLayout* draw) {
  return ShaderDescription{.name = "test_shader",
                           .source_path = "test_shader.hlsl",
                           .entry_point = "main",
                           .static_blocks = statics,
                           .draw_block = draw,
                           .emit_defines = nullptr,
                           .should_compile = nullptr,
                           .permutation_count = 1,
                           .dimension_count = 0,
                           .stage = ShaderStage::Vertex};
}

// TestView in static slot 2, with its own layout hash.
bw::shaders::RootLayout layout_with_view() {
  bw::shaders::RootLayout layout{};
  layout.static_slots[2] = bw::shaders::StaticSlot{"TestView", BlockLayoutOf<TestView>::value.hash};
  return layout;
}

// (logic.hpp, classify_dxil_constant_buffer): space 4 b<i> (i < 8) is static
// slot i, space 0 b0 the draw block, space 3 b0 the root constants, anything
// else no value; (logic.hpp, classify_vulkan_uniform_buffer): set 2 binding i
// (i < 8) static slot i, binding 8 the draw
// block, anything else no value.
void constant_buffer_classification() {
  using bw::shader_compiler::classify_dxil_constant_buffer;
  using bw::shader_compiler::classify_vulkan_uniform_buffer;
  for (std::uint32_t slot = 0; slot < bw::shaders::kMaxStaticBlocks; ++slot) {
    require(classify_dxil_constant_buffer(slot, 4) == BlockBinding{BlockBindingKind::Static, slot},
            "space 4 b<i> is static slot i");
    require(classify_vulkan_uniform_buffer(2, slot) == BlockBinding{BlockBindingKind::Static, slot},
            "set 2 binding i is static slot i");
  }
  require(classify_dxil_constant_buffer(0, 0) == BlockBinding{BlockBindingKind::Draw, 0},
          "space 0 b0 is the draw block");
  require(classify_dxil_constant_buffer(0, 3) == BlockBinding{BlockBindingKind::RootConstants, 0},
          "space 3 b0 is the root constants");
  require(classify_vulkan_uniform_buffer(2, 8) == BlockBinding{BlockBindingKind::Draw, 0},
          "set 2 binding 8 is the draw block");
  require(!classify_dxil_constant_buffer(8, 4).has_value(), "space 4 b8 is outside the layout");
  require(!classify_dxil_constant_buffer(1, 0).has_value(), "space 0 b1 is outside the layout");
  require(!classify_dxil_constant_buffer(1, 3).has_value(), "space 3 b1 is outside the layout");
  require(!classify_dxil_constant_buffer(0, 5).has_value(), "space 5 is outside the layout");
  require(!classify_dxil_constant_buffer(0, 11).has_value(),
          "a descriptor array space holds no constant buffer");
  require(!classify_vulkan_uniform_buffer(2, 9).has_value(), "set 2 binding 9 is outside");
  require(!classify_vulkan_uniform_buffer(1, 0).has_value(), "set 1 holds no uniform buffer");
  require(!classify_vulkan_uniform_buffer(0, 8).has_value(), "set 0 holds no uniform buffer");
}

// (logic.hpp, descriptor_array_for_register) and
// (logic.hpp, descriptor_array_for_vulkan_binding): an array is found from
// exactly its placement() register
// class and space, or its Vulkan set and binding, and from nothing else.
void descriptor_array_round_trip() {
  using bw::shader_compiler::descriptor_array_for_register;
  using bw::shader_compiler::descriptor_array_for_vulkan_binding;
  for (std::uint32_t index = 0; index < bw::shaders::kDescriptorArrayCount; ++index) {
    const auto array = static_cast<DescriptorArray>(index);
    const bw::shaders::DescriptorArrayPlacement where = bw::shaders::placement(array);
    require(descriptor_array_for_register(where.register_class, where.register_space) == array,
            "register class and space give the array back");
    require(descriptor_array_for_vulkan_binding(where.vulkan_set, where.vulkan_binding) == array,
            "set and binding give the array back");
  }
  require(!descriptor_array_for_register('t', 10).has_value(),
          "a texture register in the sampler space is no array");
  require(!descriptor_array_for_register('s', 11).has_value(),
          "a sampler register in a texture space is no array");
  require(!descriptor_array_for_register('u', 15).has_value(),
          "a storage register in the buffer space is no array");
  require(!descriptor_array_for_register('b', 0).has_value(), "a constant buffer is no array");
  require(!descriptor_array_for_vulkan_binding(0, 1).has_value(), "set 0 binding 1 is no array");
  require(!descriptor_array_for_vulkan_binding(1, 7).has_value(), "set 1 binding 7 is no array");
  require(!descriptor_array_for_vulkan_binding(2, 0).has_value(), "set 2 holds no array");
}

// (logic.hpp, split_semantic): trailing digits are the index, the name is
// upper-cased; an empty semantic, one of digits only, or one with an index
// past 32 bits is no value.
void semantic_split() {
  using bw::shader_compiler::split_semantic;
  require(split_semantic("TEXCOORD1") == Semantic{"TEXCOORD", 1}, "TEXCOORD1");
  require(split_semantic("COLOR") == Semantic{"COLOR", 0}, "no digits is index 0");
  require(split_semantic("texCoord12") == Semantic{"TEXCOORD", 12}, "upper-cased, two digits");
  require(split_semantic("POSITION0") == Semantic{"POSITION", 0}, "explicit 0");
  require(!split_semantic("12").has_value(), "digits only");
  require(!split_semantic("").has_value(), "empty");
  require(!split_semantic("A99999999999").has_value(), "index past 32 bits");
}

// (logic.hpp, keeps_vertex_input): the DXIL view (semantic, SystemValueType !=
// UNDEFINED) and the SPIR-V view (semantic from -fspv-reflect, BuiltIn) agree
// for the system values and for an ordinary input.
void vertex_input_predicate() {
  using bw::shader_compiler::keeps_vertex_input;
  // SV_Position and SV_VertexID: a system value in the DXIL signature and a
  // built-in in SPIR-V; the "SV_" semantic alone also excludes them.
  for (const std::string_view semantic : {"SV_Position", "SV_VertexID", "sv_vertexid"}) {
    require(!keeps_vertex_input(semantic, true), "system value flag excludes");
    require(!keeps_vertex_input(semantic, false), "an SV_ semantic excludes without the flag");
  }
  require(keeps_vertex_input("TEXCOORD0", false), "an ordinary semantic is kept");
  require(!keeps_vertex_input("TEXCOORD0", true), "the flag excludes whatever the semantic");
  require(keeps_vertex_input("SVELTE", false), "only the SV_ prefix marks a system value");
}

// (logic.hpp, assemble_reflection), with (logic.hpp, ReflectionError): blocks
// are named and hashed from the
// registry by slot, in the stated (kind, slot) order; root constant words come from the
// layout when used; the named errors for undeclared, duplicate and
// over-long input lists.
void assemble_reflection_by_slot() {
  using bw::shader_compiler::assemble_reflection;
  const bw::shaders::RootLayout layout = layout_with_view();
  const ShaderDescription shader = description(kViewStatic, &BlockLayoutOf<TestDraw>::value);

  BackendReflection backend{};
  backend.blocks.push_back(
      BoundBlock{BlockBinding{BlockBindingKind::Draw, 0}, {{"bw_block_8", 0}}});
  backend.blocks.push_back(BoundBlock{BlockBinding{BlockBindingKind::Static, 2}, {}});
  backend.uses_root_constants = true;
  backend.descriptor_arrays = 0x5U;
  backend.vertex_inputs.push_back({Semantic{"POSITION", 0}, 0});
  backend.vertex_inputs.push_back({Semantic{"TEXCOORD", 1}, 1});
  const auto assembled = assemble_reflection(backend, shader, layout);
  require(assembled.error == ReflectionError::None, "assembles");
  const bw::shaders::Reflection& reflection = assembled.reflection;
  require(reflection.block_count == 2, "two blocks");
  require(reflection.blocks[0].kind == bw::shaders::BlockSlotKind::Static &&
              reflection.blocks[0].slot == 2 && reflection.blocks[0].name == "TestView" &&
              reflection.blocks[0].layout_hash == BlockLayoutOf<TestView>::value.hash,
          "static block first, named and hashed by the registry");
  require(reflection.blocks[1].kind == bw::shaders::BlockSlotKind::Draw &&
              reflection.blocks[1].slot == 0 && reflection.blocks[1].name == "TestDraw" &&
              reflection.blocks[1].layout_hash == BlockLayoutOf<TestDraw>::value.hash,
          "draw block named by the registry, not by the backend's resource name");
  require(reflection.root_constant_words == layout.root_constant_words, "root constants used");
  require(reflection.descriptor_arrays == 0x5U, "descriptor arrays copied");
  require(reflection.vertex_input_count == 2 &&
              reflection.vertex_inputs[1].semantic_name == "TEXCOORD" &&
              reflection.vertex_inputs[1].semantic_index == 1 &&
              reflection.vertex_inputs[1].location == 1,
          "vertex inputs copied, location included");

  ShaderDescription compute = description(kViewStatic, nullptr);
  compute.stage = ShaderStage::Compute;
  BackendReflection dispatch{};
  dispatch.thread_group_size = {8, 4, 2};
  require(assemble_reflection(dispatch, compute, layout).reflection.thread_group_size ==
              std::array<std::uint32_t, 3>{8, 4, 2},
          "thread group size copied");

  BackendReflection unused = backend;
  unused.uses_root_constants = false;
  require(assemble_reflection(unused, shader, layout).reflection.root_constant_words == 0,
          "root constants unused is 0 words");

  BackendReflection empty_slot{};
  empty_slot.blocks.push_back(BoundBlock{BlockBinding{BlockBindingKind::Static, 3}, {}});
  const auto unplaced = assemble_reflection(empty_slot, shader, layout);
  require(unplaced.error == ReflectionError::StaticBlockNotDeclared && unplaced.slot == 3,
          "a slot the layout leaves empty");

  const ShaderDescription no_statics = description({}, &BlockLayoutOf<TestDraw>::value);
  BackendReflection undeclared{};
  undeclared.blocks.push_back(BoundBlock{BlockBinding{BlockBindingKind::Static, 2}, {}});
  require(assemble_reflection(undeclared, no_statics, layout).error ==
              ReflectionError::StaticBlockNotDeclared,
          "a slot whose block the description does not list");

  const ShaderDescription no_draw = description(kViewStatic, nullptr);
  BackendReflection draw_only{};
  draw_only.blocks.push_back(BoundBlock{BlockBinding{BlockBindingKind::Draw, 0}, {}});
  require(assemble_reflection(draw_only, no_draw, layout).error ==
              ReflectionError::DrawBlockNotDeclared,
          "a draw block the description does not declare");

  BackendReflection twice{};
  twice.blocks.push_back(BoundBlock{BlockBinding{BlockBindingKind::Static, 2}, {}});
  twice.blocks.push_back(BoundBlock{BlockBinding{BlockBindingKind::Static, 2}, {}});
  const auto duplicated = assemble_reflection(twice, shader, layout);
  require(duplicated.error == ReflectionError::DuplicateBlock && duplicated.slot == 2,
          "the same slot twice");

  BackendReflection crowded{};
  for (std::uint32_t i = 0; i <= bw::shaders::kMaxVertexInputs; ++i) {
    crowded.vertex_inputs.push_back({Semantic{"ATTRIBUTE", i}, i});
  }
  require(assemble_reflection(crowded, shader, layout).error ==
              ReflectionError::TooManyVertexInputs,
          "more inputs than the format holds");

  BackendReflection long_name{};
  long_name.vertex_inputs.push_back(
      {Semantic{std::string(bw::shaders::kMaxArtifactString + 1, 'A'), 0}, 0});
  require(assemble_reflection(long_name, shader, layout).error == ReflectionError::SemanticTooLong,
          "a semantic longer than the format holds");
}

// (logic.hpp, compute_input_hash) and (artifact.hpp, InputHash): the same inputs give the
// same hash, a change in any one item changes it, and items and lists are
// length-prefixed, so equal concatenations split differently differ.
void input_hash_items() {
  using bw::shader_compiler::compute_input_hash;
  using bw::shader_compiler::HashInputs;
  using bw::shader_compiler::IncludedFile;
  const std::array<bw::shaders::Define, 1> defines{bw::shaders::Define{"BW_FLIP_Y", 1}};
  const std::vector<std::string> arguments{"-O3", "-E", "main"};
  const std::vector<IncludedFile> includes{IncludedFile{"bw/bindless.hlsli", "// include"}};
  const HashInputs base{.compiler_identity = "dxc 1.9",
                        .entry_point = "main",
                        .defines = defines,
                        .arguments = arguments,
                        .generated_include = "struct BW_X {};\n",
                        .main_source = "float4 main() : SV_Position { return 0; }",
                        .includes = includes,
                        .root_layout_hash = bw::shaders::LayoutHash{42},
                        .target = ShaderTarget::Vulkan,
                        .stage = ShaderStage::Vertex};
  const bw::shaders::InputHash reference = compute_input_hash(base);
  require(compute_input_hash(base) == reference, "same inputs, same hash");

  const std::array<bw::shaders::Define, 1> other_define_value{bw::shaders::Define{"BW_FLIP_Y", 0}};
  const std::array<bw::shaders::Define, 1> other_define_name{bw::shaders::Define{"BW_FLIP_X", 1}};
  const std::vector<std::string> other_arguments{"-O3", "-E", "main2"};
  const std::vector<IncludedFile> other_include_path{IncludedFile{"bw/other.hlsli", "// include"}};
  const std::vector<IncludedFile> other_include_text{IncludedFile{"bw/bindless.hlsli", "// edit"}};
  const std::vector<std::function<void(HashInputs&)>> changes{
      [](HashInputs& in) { in.compiler_identity = "dxc 1.10"; },
      [](HashInputs& in) { in.entry_point = "main_vs"; },
      [&](HashInputs& in) { in.defines = other_define_value; },
      [&](HashInputs& in) { in.defines = other_define_name; },
      [&](HashInputs& in) { in.arguments = other_arguments; },
      [](HashInputs& in) { in.generated_include = "struct BW_Y {};\n"; },
      [](HashInputs& in) { in.main_source = "// edited"; },
      [&](HashInputs& in) { in.includes = other_include_path; },
      [&](HashInputs& in) { in.includes = other_include_text; },
      [](HashInputs& in) { in.root_layout_hash = bw::shaders::LayoutHash{43}; },
      [](HashInputs& in) { in.target = ShaderTarget::Metal; },
      [](HashInputs& in) { in.stage = ShaderStage::Pixel; },
  };
  for (const auto& change : changes) {
    HashInputs changed = base;
    change(changed);
    require(compute_input_hash(changed) != reference, "every item is hashed");
  }

  const std::vector<std::string> split_late{"ab", "c"};
  const std::vector<std::string> split_early{"a", "bc"};
  HashInputs late = base;
  late.arguments = split_late;
  HashInputs early = base;
  early.arguments = split_early;
  require(compute_input_hash(late) != compute_input_hash(early), "items are length-prefixed");

  const std::array<bw::shaders::Define, 1> one_define{bw::shaders::Define{"X", 1}};
  const std::vector<std::string> define_as_argument{"X=1"};
  HashInputs as_define = base;
  as_define.defines = one_define;
  as_define.arguments = {};
  HashInputs as_argument = base;
  as_argument.defines = {};
  as_argument.arguments = define_as_argument;
  require(compute_input_hash(as_define) != compute_input_hash(as_argument),
          "lists are count-prefixed");

  const std::vector<IncludedFile> path_long{IncludedFile{"ab", "c"}};
  const std::vector<IncludedFile> path_short{IncludedFile{"a", "bc"}};
  HashInputs long_path = base;
  long_path.includes = path_long;
  HashInputs short_path = base;
  short_path.includes = path_short;
  require(compute_input_hash(long_path) != compute_input_hash(short_path),
          "an include's path and text are separate items");

  const std::string joined = std::string{base.generated_include} + std::string{base.main_source};
  HashInputs source_heavy = base;
  source_heavy.generated_include = "";
  source_heavy.main_source = joined;
  require(compute_input_hash(source_heavy) != reference,
          "generated include and main source are separate items");

  // Known answer: pins the item order, the framing and the hash function. A
  // change to any of them changes this value, and the change is then a
  // deliberate edit of kReferenceInputHash, never an accident. Obtained once
  // from the implementation on 2026-11-02.
  const std::string reference_hex = bw::shader_compiler::input_hash_hex(reference);
  require(reference_hex == kReferenceInputHash, "known-answer input hash; got " + reference_hex);
}

// (logic.hpp, append_generated_include): exact text, structs then binding
// lines, draw block first; (logic.hpp, IncludeError): the named errors before
// any text is trusted.
void generated_include_text() {
  using bw::shader_compiler::append_generated_include;
  const bw::shaders::RootLayout layout = layout_with_view();
  std::string text;
  const auto result = append_generated_include(
      description(kViewStatic, &BlockLayoutOf<TestDraw>::value), layout, text);
  require(result.error == IncludeError::None, "generates");
  const std::string_view expected = "struct BW_TestDraw {\n"
                                    "  float4 tint;\n"
                                    "  uint texture;\n"
                                    "  uint bw_pad_20;\n"
                                    "  uint bw_pad_24;\n"
                                    "  uint bw_pad_28;\n"
                                    "};\n"
                                    "struct BW_TestView {\n"
                                    "  float4x4 view;\n"
                                    "  float scale;\n"
                                    "  uint bw_pad_68;\n"
                                    "  uint bw_pad_72;\n"
                                    "  uint bw_pad_76;\n"
                                    "};\n"
                                    "BW_DRAW_BLOCK(BW_TestDraw, TestDraw)\n"
                                    "BW_STATIC_BLOCK(BW_TestView, TestView, 2)\n";
  require(text == expected, "exact generated include text");

  std::string scratch;
  const auto no_slot = append_generated_include(description(kViewStatic, nullptr),
                                                bw::shaders::RootLayout{}, scratch);
  require(no_slot.error == IncludeError::StaticBlockWithoutSlot && no_slot.block == "TestView",
          "a static block the layout does not place");

  bw::shaders::RootLayout stale = layout;
  stale.static_slots[2].layout_hash = bw::shaders::LayoutHash{1};
  scratch.clear();
  const auto stale_result =
      append_generated_include(description(kViewStatic, nullptr), stale, scratch);
  require(stale_result.error == IncludeError::StaticSlotHashMismatch &&
              stale_result.block == "TestView",
          "a layout slot hash that is not the block's, naming the block");

  scratch.clear();
  const auto null_result =
      append_generated_include(description(kNullStatic, nullptr), layout, scratch);
  require(null_result.error == IncludeError::NullBlock && null_result.block.empty(),
          "a null static block entry, with no block to name");

  // A layout that did not come from the macros: its name is not an HLSL
  // identifier, so append_hlsl_struct fails and the include reports how.
  const bw::shaders::BlockLayout bad_name{.name = "not an identifier",
                                          .members = BlockLayoutOf<TestDraw>::value.members,
                                          .hash = BlockLayoutOf<TestDraw>::value.hash,
                                          .size = BlockLayoutOf<TestDraw>::value.size};
  scratch.clear();
  const auto hlsl_result = append_generated_include(description({}, &bad_name), layout, scratch);
  require(hlsl_result.error == IncludeError::Hlsl &&
              hlsl_result.hlsl == bw::shaders::HlslError::InvalidName,
          "a failing append_hlsl_struct is IncludeError::Hlsl with its HlslError");

  bw::shaders::RootLayout draw_static = layout;
  draw_static.static_slots[0] =
      bw::shaders::StaticSlot{"TestDraw", BlockLayoutOf<TestDraw>::value.hash};
  scratch.clear();
  require(append_generated_include(description(kDrawAsStatic, &BlockLayoutOf<TestDraw>::value),
                                   draw_static, scratch)
                  .error == IncludeError::SharedBlock,
          "one block as both draw and static block");

  bw::shaders::RootLayout outer = layout;
  outer.static_slots[1] =
      bw::shaders::StaticSlot{"TestOuterB", BlockLayoutOf<TestOuterB>::value.hash};
  scratch.clear();
  const auto shared = append_generated_include(
      description(kOuterBStatic, &BlockLayoutOf<TestOuterA>::value), outer, scratch);
  require(shared.error == IncludeError::SharedBlock && shared.block == "TestInner",
          "a nested block reachable from two top-level blocks");

  require(bw::shader_compiler::generated_include_path("fullscreen_triangle") ==
              "bw/generated/fullscreen_triangle.hlsli",
          "generated include path (logic.hpp, generated_include_path)");
}

// (logic.hpp, normalize_include_path): what DXC asks for, made relative to the shader root, or
// refused.
void include_path_normalization() {
  using bw::shader_compiler::normalize_include_path;
  require(normalize_include_path("./bw/bindless.hlsli") ==
              std::optional<std::string>{"bw/bindless.hlsli"},
          "leading ./ dropped");
  require(normalize_include_path("bw\\generated\\x.hlsli") ==
              std::optional<std::string>{"bw/generated/x.hlsli"},
          "backslashes become slashes");
  require(normalize_include_path("./bw/../common.hlsli") ==
              std::optional<std::string>{"common.hlsli"},
          ".. removes the previous component");
  require(normalize_include_path("bw//x.hlsli") == std::optional<std::string>{"bw/x.hlsli"},
          "empty components dropped");
  require(!normalize_include_path("../outside.hlsli").has_value(), "climbing out of the root");
  require(!normalize_include_path("bw/../../outside.hlsli").has_value(), "climbing out later");
  require(!normalize_include_path("/usr/include/x.h").has_value(), "absolute path");
  require(!normalize_include_path("C:/x.hlsli").has_value(), "drive prefix");
  require(!normalize_include_path("./.").has_value(), "nothing left");
  require(!normalize_include_path("").has_value(), "empty");
}

// (logic.hpp, append_depfile): exact Makefile text, main source first, root joined with
// one slash, special characters escaped, no generated include.
void depfile_text() {
  using bw::shader_compiler::append_depfile;
  const std::vector<std::string> includes{"bw/bindless.hlsli", "bw/common.hlsli"};
  std::string text;
  append_depfile("out/d3d12/s.0.vs.bwsh", "src/hlsl", "s.hlsl", includes, text);
  require(text == "out/d3d12/s.0.vs.bwsh: \\\n"
                  "  src/hlsl/s.hlsl \\\n"
                  "  src/hlsl/bw/bindless.hlsli \\\n"
                  "  src/hlsl/bw/common.hlsli\n",
          "exact depfile text");

  std::string trailing;
  append_depfile("o.bwsh", "src/hlsl/", "s.hlsl", {}, trailing);
  require(trailing == "o.bwsh: \\\n  src/hlsl/s.hlsl\n", "no double slash, main source alone");

  std::string escaped;
  append_depfile("my out/o#1.bwsh", "a b", "c$d.hlsl", {}, escaped);
  require(escaped == "my\\ out/o\\#1.bwsh: \\\n  a\\ b/c$$d.hlsl\n", "space, '#' and '$' escaped");

  require(bw::shader_compiler::join_path("root", "a/b") == "root/a/b", "join");
  require(bw::shader_compiler::join_path("root/", "a") == "root/a", "join with trailing slash");
}

// (logic.hpp, artifact_relative_path), (logic.hpp, symbol_file_name) and
// (logic.hpp, input_hash_hex): artifact path, symbol file and hash text;
// (logic.hpp, converter_identity): the converter identity text from a UUID.
void file_names() {
  using bw::shader_compiler::artifact_relative_path;
  using bw::shader_compiler::input_hash_hex;
  using bw::shader_compiler::symbol_file_name;
  require(artifact_relative_path(ShaderTarget::Direct3D12, "fullscreen_triangle", PermutationId{1},
                                 ShaderStage::Vertex) == "d3d12/fullscreen_triangle.1.vs.bwsh",
          "d3d12 artifact path");
  require(artifact_relative_path(ShaderTarget::Metal, "tonemap", PermutationId{0},
                                 ShaderStage::Pixel) == "metal/tonemap.0.ps.bwsh",
          "metal artifact path");
  require(artifact_relative_path(ShaderTarget::Vulkan, "cull", PermutationId{7},
                                 ShaderStage::Compute) == "vulkan/cull.7.cs.bwsh",
          "vulkan artifact path");

  bw::shaders::InputHash hash{};
  hash[0] = 0x00;
  hash[1] = 0xAB;
  hash[31] = 0x0F;
  const std::string hex = input_hash_hex(hash);
  require(hex.size() == 64 && hex.substr(0, 4) == "00ab" && hex.substr(62) == "0f",
          "lower-case hex, two digits per byte");
  require(symbol_file_name(hash, ShaderTarget::Direct3D12) == hex + ".pdb", "d3d12 symbols");
  require(symbol_file_name(hash, ShaderTarget::Metal) == hex + ".pdb", "metal symbols");
  require(symbol_file_name(hash, ShaderTarget::Vulkan) == hex + ".spv", "vulkan symbols");

  constexpr std::array<std::uint8_t, 16> uuid{0x00, 0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD,
                                              0xEF, 0x10, 0x32, 0x54, 0x76, 0x98, 0xBA, 0xFE};
  require(bw::shader_compiler::converter_identity(uuid) ==
              "metal-shaderconverter uuid:000123456789abcdef1032547698bafe",
          "converter identity text, 32 lower-case hex digits in byte order");
  std::array<std::uint8_t, 16> other = uuid;
  other[15] = 0xFF;
  require(bw::shader_compiler::converter_identity(other) !=
              bw::shader_compiler::converter_identity(uuid),
          "one byte of UUID changes the identity");
}

// Hand-built 64-bit Mach-O images for mach_o_uuid: a header, then load
// commands as (cmd, cmdsize, payload).
void append_u32(std::vector<std::byte>& out, std::uint32_t value) {
  for (unsigned shift = 0; shift < 32U; shift += 8U) {
    out.push_back(static_cast<std::byte>((value >> shift) & 0xFFU));
  }
}

struct LoadCommand {
  std::uint32_t cmd{};
  std::uint32_t cmdsize{};
  std::vector<std::byte> payload{};
};

std::vector<std::byte> mach_o_image(std::uint32_t magic, std::uint32_t ncmds,
                                    const std::vector<LoadCommand>& commands) {
  std::vector<std::byte> body;
  for (const LoadCommand& command : commands) {
    append_u32(body, command.cmd);
    append_u32(body, command.cmdsize);
    body.insert(body.end(), command.payload.begin(), command.payload.end());
  }
  std::vector<std::byte> image;
  append_u32(image, magic);
  append_u32(image, 0x0100000CU); // cputype arm64
  append_u32(image, 0);           // cpusubtype
  append_u32(image, 6);           // filetype MH_DYLIB
  append_u32(image, ncmds);
  append_u32(image, static_cast<std::uint32_t>(body.size()));
  append_u32(image, 0); // flags
  append_u32(image, 0); // reserved
  image.insert(image.end(), body.begin(), body.end());
  return image;
}

// (logic.hpp, mach_o_uuid): the first LC_UUID among the commands, and no value
// for each refusal the contract lists.
void mach_o_uuid_walk() {
  using bw::shader_compiler::mach_o_uuid;
  constexpr std::uint32_t kMagic = 0xfeedfacfU;
  constexpr std::uint32_t kLcUuid = 0x1bU;
  constexpr std::uint32_t kLcSegment64 = 0x19U;
  std::vector<std::byte> uuid_bytes;
  std::array<std::uint8_t, 16> expected{};
  for (std::uint8_t i = 0; i < 16; ++i) {
    expected[i] = static_cast<std::uint8_t>(0xA0U + i);
    uuid_bytes.push_back(static_cast<std::byte>(expected[i]));
  }
  const LoadCommand other{kLcSegment64, 16, std::vector<std::byte>(8, std::byte{0})};
  const LoadCommand uuid{kLcUuid, 24, uuid_bytes};

  const std::vector<std::byte> valid = mach_o_image(kMagic, 3, {other, uuid, other});
  require(mach_o_uuid(valid) == expected, "the LC_UUID among other commands");

  require(!mach_o_uuid(mach_o_image(kMagic, 2, {other, other})).has_value(), "no LC_UUID");

  std::vector<std::byte> wrong_magic = valid;
  wrong_magic[0] = std::byte{0xCE}; // MH_MAGIC, the 32-bit header
  require(!mach_o_uuid(wrong_magic).has_value(), "a magic other than MH_MAGIC_64");

  // ncmds says three, the list holds two.
  require(!mach_o_uuid(mach_o_image(kMagic, 3, {other, other})).has_value(),
          "a command list cut short of ncmds");

  // cmdsize 0xFFFFFFF8 after a 16-byte command: offset + cmdsize wraps a
  // 32-bit sum to 8, inside sizeofcmds; the walk must refuse it.
  const LoadCommand huge{kLcSegment64, 0xFFFFFFF8U, std::vector<std::byte>(8, std::byte{0})};
  require(!mach_o_uuid(mach_o_image(kMagic, 3, {other, huge, uuid})).has_value(),
          "a cmdsize that would overflow a 32-bit sum");

  const LoadCommand tiny{kLcSegment64, 4, {}};
  require(!mach_o_uuid(mach_o_image(kMagic, 2, {tiny, uuid})).has_value(),
          "a cmdsize below the load command size");

  const LoadCommand short_uuid{kLcUuid, 16, std::vector<std::byte>(8, std::byte{0})};
  require(!mach_o_uuid(mach_o_image(kMagic, 1, {short_uuid})).has_value(),
          "an LC_UUID too short for its UUID");

  std::vector<std::byte> truncated = valid;
  truncated.resize(valid.size() - 4);
  require(!mach_o_uuid(truncated).has_value(), "sizeofcmds running past the span");
  require(!mach_o_uuid(std::span<const std::byte>{valid.data(), 31}).has_value(),
          "a span shorter than the header");
}

// (enumeration.hpp, append_enumeration): registry order, then admitted permutations
// ascending, then targets in the order given; nothing else.
void enumeration_order() {
  ShaderDescription excluding = description({}, nullptr);
  excluding.name = "first";
  excluding.permutation_count = 3;
  excluding.should_compile = [](PermutationId id) noexcept { return id.value != 1U; };
  ShaderDescription single = description({}, nullptr);
  single.name = "second";
  single.stage = ShaderStage::Compute;
  const std::array registry{excluding, single};
  const std::array targets{ShaderTarget::Metal, ShaderTarget::Direct3D12};

  std::string text;
  bw::shader_compiler::append_enumeration(registry, targets, text);
  std::string expected;
  for (const PermutationId id : {PermutationId{0}, PermutationId{2}}) {
    for (const ShaderTarget target : targets) {
      bw::shaders::append_enumerate_line(excluding, id, target, expected);
    }
  }
  for (const ShaderTarget target : targets) {
    bw::shaders::append_enumerate_line(single, PermutationId{0}, target, expected);
  }
  require(text == expected, "enumeration order");
}

using Scenario = bw::testing::Scenario<void>;

constexpr std::array scenarios{
    Scenario{"constant_buffer_classification", constant_buffer_classification},
    Scenario{"descriptor_array_round_trip", descriptor_array_round_trip},
    Scenario{"semantic_split", semantic_split},
    Scenario{"vertex_input_predicate", vertex_input_predicate},
    Scenario{"assemble_reflection_by_slot", assemble_reflection_by_slot},
    Scenario{"input_hash_items", input_hash_items},
    Scenario{"generated_include_text", generated_include_text},
    Scenario{"include_path_normalization", include_path_normalization},
    Scenario{"depfile_text", depfile_text},
    Scenario{"file_names", file_names},
    Scenario{"mach_o_uuid_walk", mach_o_uuid_walk},
    Scenario{"enumeration_order", enumeration_order},
};

} // namespace

int main(int argc, char** argv) { return bw::testing::run_scenario(argc, argv, scenarios); }
