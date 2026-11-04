// Unit test: public contracts of bw/rendering/shaders/root_layout.hpp, one
// scenario per CTest entry. Each scenario names the header declaration it asserts.
#include "bw/rendering/shaders/root_layout.hpp"

#include "bw/testing/harness.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace {
using bw::shaders::DescriptorArray;
using bw::shaders::DescriptorArrayPlacement;
using bw::shaders::LayoutHash;
using bw::shaders::RootElement;
using bw::shaders::RootLayout;
using bw::shaders::StaticSlot;

using bw::testing::require;

RootLayout layout_with(std::uint32_t slot, std::string_view name, std::uint64_t hash) {
  RootLayout layout{};
  layout.static_slots[slot] = StaticSlot{.block_name = name, .layout_hash = LayoutHash{hash}};
  return layout;
}

// (root_layout.hpp, root_layout_hash): same slots in the same order hash equal; moving a
// block to another slot changes the hash; a slot's layout hash and
// root_constant_words are hashed inputs.
void hash_changes_when_slot_moves() {
  const RootLayout at_zero = layout_with(0, "View", 0x1234);
  const RootLayout at_zero_again = layout_with(0, "View", 0x1234);
  const RootLayout at_one = layout_with(1, "View", 0x1234);
  require(bw::shaders::root_layout_hash(at_zero) == bw::shaders::root_layout_hash(at_zero_again),
          "equal tables hash equal");
  require(bw::shaders::root_layout_hash(at_zero) != bw::shaders::root_layout_hash(at_one),
          "moving a block changes the hash");
  require(bw::shaders::root_layout_hash(at_zero) !=
              bw::shaders::root_layout_hash(layout_with(0, "View", 0x1235)),
          "a slot's layout hash is hashed");
  RootLayout more_constants = at_zero;
  more_constants.root_constant_words = bw::shaders::kRootConstantWords - 1U;
  require(bw::shaders::root_layout_hash(at_zero) != bw::shaders::root_layout_hash(more_constants),
          "root constant words are hashed");
}

// (root_layout.hpp, find_static_slot): the slot of a block by name, no value
// when absent; unused slots never match. (root_layout.hpp, RootLayout) and
// (root_layout.hpp, engine_root_layout): the engine's one layout is
// a value whose hash is root_layout_hash of it, whose root_constant_words is
// kRootConstantWords, and in which find_static_slot finds each named block
// and nothing else. Nothing is assumed about which blocks it holds.
void find_static_slot() {
  const RootLayout layout = layout_with(5, "Frame", 0x99);
  require(bw::shaders::find_static_slot(layout, "Frame") == std::optional<std::uint32_t>{5},
          "a named block's slot");
  require(!bw::shaders::find_static_slot(layout, "View").has_value(), "absent block has none");
  require(!bw::shaders::find_static_slot(layout, "").has_value(), "an unused slot never matches");

  const RootLayout& engine = bw::shaders::engine_root_layout();
  require(&engine == &bw::shaders::engine_root_layout(), "one engine layout instance");
  const RootLayout copy = engine;
  require(bw::shaders::root_layout_hash(copy) == bw::shaders::root_layout_hash(engine),
          "the engine layout's hash is root_layout_hash of its value");
  require(engine.root_constant_words == bw::shaders::kRootConstantWords,
          "engine layout root constant words");
  // A probe name chosen to differ from every name the layout holds.
  std::string absent = "bw_test_absent_block";
  const auto held = [&engine](std::string_view name) {
    for (const StaticSlot& slot : engine.static_slots) {
      if (slot.block_name == name) {
        return true;
      }
    }
    return false;
  };
  while (held(absent)) {
    absent += '_';
  }
  require(!bw::shaders::find_static_slot(engine, absent).has_value(),
          "a name not in the engine layout has no slot");
  for (std::uint32_t index = 0; index < bw::shaders::kMaxStaticBlocks; ++index) {
    const std::string_view name = engine.static_slots[index].block_name;
    if (!name.empty()) {
      const std::optional<std::uint32_t> found = bw::shaders::find_static_slot(engine, name);
      require(found.has_value() && engine.static_slots[*found].block_name == name,
              "each named engine block is found at a slot holding that name");
    }
  }
}

// (root_layout.hpp, append_root_signature_text): the exact text, root parameters in the pin's order
// (SRV table, sampler table, UAV table, draw CBV, static CBVs b0-b7, root
// constants), every root CBV with flags=DATA_STATIC, ", " between parameters
// and between the ranges of one table, no newline, appended;
// num32BitConstants follows root_constant_words and nothing else of the
// layout changes the text (all eight static CBVs are present whether or not
// a slot is used).
void root_signature_text_is_exact() {
  const std::string_view srv_flags = "flags=DESCRIPTORS_VOLATILE|DATA_STATIC_WHILE_SET_AT_EXECUTE";
  const std::string_view uav_flags = "flags=DESCRIPTORS_VOLATILE|DATA_VOLATILE";
  std::string expected = "RootFlags(ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT), DescriptorTable(";
  for (const char* space : {"11", "12", "13", "14", "15"}) {
    if (std::string_view{space} != "11") {
      expected += ", ";
    }
    expected += "SRV(t0, space=";
    expected += space;
    expected += ", numDescriptors=unbounded, offset=0, ";
    expected += srv_flags;
    expected += ')';
  }
  expected += "), DescriptorTable(Sampler(s0, space=10, numDescriptors=unbounded, offset=0, "
              "flags=DESCRIPTORS_VOLATILE)), ";
  expected += "DescriptorTable(UAV(u0, space=16, numDescriptors=64, offset=0, ";
  expected += uav_flags;
  expected += "), UAV(u0, space=17, numDescriptors=64, offset=0, ";
  expected += uav_flags;
  expected += ")), CBV(b0, space=0, flags=DATA_STATIC), "
              "CBV(b0, space=4, flags=DATA_STATIC), CBV(b1, space=4, flags=DATA_STATIC), "
              "CBV(b2, space=4, flags=DATA_STATIC), CBV(b3, space=4, flags=DATA_STATIC), "
              "CBV(b4, space=4, flags=DATA_STATIC), CBV(b5, space=4, flags=DATA_STATIC), "
              "CBV(b6, space=4, flags=DATA_STATIC), CBV(b7, space=4, flags=DATA_STATIC), "
              "RootConstants(num32BitConstants=16, b0, space=3)";

  std::string text = "kept|";
  bw::shaders::append_root_signature_text(RootLayout{}, text);
  require(text == "kept|" + expected, "root signature text is exact and appended");
  require(text.find('\n') == std::string::npos, "no newline");

  std::string with_block;
  bw::shaders::append_root_signature_text(layout_with(3, "View", 0x42), with_block);
  require(with_block == expected, "a named static slot does not change the text");

  RootLayout four_words{};
  four_words.root_constant_words = 4;
  std::string fewer;
  bw::shaders::append_root_signature_text(four_words, fewer);
  std::string expected_fewer = expected;
  const std::string_view sixteen = "num32BitConstants=16";
  expected_fewer.replace(expected_fewer.find(sixteen), sixteen.size(), "num32BitConstants=4");
  require(fewer == expected_fewer, "num32BitConstants is root_constant_words");
}

struct ExpectedPlacement {
  DescriptorArray array;
  std::string_view hlsl_type;
  RootElement table;
  std::uint32_t space;
  std::uint32_t set;
  std::uint32_t binding;
  std::uint32_t max_descriptors;
  char register_class;
};

// (root_layout.hpp, DescriptorArray), (root_layout.hpp, DescriptorArrayPlacement)
// and the definition of (root_layout.hpp, placement): each array's
// HLSL type, table, register space (kSamplerSpace..kStorageBufferSpace),
// Vulkan set (samplers kVulkanSamplerSet, the rest kVulkanResourceSet) and
// distinct binding, bound (0 unbounded, kStorageDescriptors for the two
// storage arrays) and register class. (root_layout.hpp, kVulkanUniformSet) and
// (root_layout.hpp, kVulkanPushConstantBytes): the uniform set follows the
// two array sets, and the push constant block is 8 static indices, then the
// draw index at byte 32, then 16 root constant words from byte 36, 100 bytes.
void placement_table() {
  using bw::shaders::kStorageDescriptors;
  using bw::shaders::kVulkanResourceSet;
  using bw::shaders::kVulkanSamplerSet;
  const std::array<ExpectedPlacement, bw::shaders::kDescriptorArrayCount> expected{{
      {DescriptorArray::Sampler, "SamplerState", RootElement::SamplerTable,
       bw::shaders::kSamplerSpace, kVulkanSamplerSet, 0, 0, 's'},
      {DescriptorArray::Texture2D, "Texture2D", RootElement::ResourceTable,
       bw::shaders::kTexture2DSpace, kVulkanResourceSet, 0, 0, 't'},
      {DescriptorArray::TextureCube, "TextureCube", RootElement::ResourceTable,
       bw::shaders::kTextureCubeSpace, kVulkanResourceSet, 1, 0, 't'},
      {DescriptorArray::Texture2DArray, "Texture2DArray", RootElement::ResourceTable,
       bw::shaders::kTexture2DArraySpace, kVulkanResourceSet, 2, 0, 't'},
      {DescriptorArray::Texture3D, "Texture3D", RootElement::ResourceTable,
       bw::shaders::kTexture3DSpace, kVulkanResourceSet, 3, 0, 't'},
      {DescriptorArray::Buffer, "ByteAddressBuffer", RootElement::ResourceTable,
       bw::shaders::kBufferSpace, kVulkanResourceSet, 4, 0, 't'},
      {DescriptorArray::StorageTexture2D, "RWTexture2D<float4>", RootElement::StorageTable,
       bw::shaders::kStorageTexture2DSpace, kVulkanResourceSet, 5, kStorageDescriptors, 'u'},
      {DescriptorArray::StorageBuffer, "RWByteAddressBuffer", RootElement::StorageTable,
       bw::shaders::kStorageBufferSpace, kVulkanResourceSet, 6, kStorageDescriptors, 'u'},
  }};
  for (std::size_t i = 0; i < expected.size(); ++i) {
    const ExpectedPlacement& want = expected[i];
    require(static_cast<std::size_t>(want.array) == i, "enumerators are 0..count-1");
    const DescriptorArrayPlacement got = bw::shaders::placement(want.array);
    require(got.hlsl_type == want.hlsl_type, "HLSL type");
    require(got.table == want.table, "table");
    require(got.register_space == want.space, "register space");
    require(got.vulkan_set == want.set, "Vulkan set");
    require(got.vulkan_binding == want.binding, "Vulkan binding");
    require(got.max_descriptors == want.max_descriptors, "bound");
    require(got.register_class == want.register_class, "register class");
    for (std::size_t j = 0; j < i; ++j) {
      const DescriptorArrayPlacement other = bw::shaders::placement(expected[j].array);
      require(other.register_space != got.register_space, "register spaces distinct");
      require(other.vulkan_set != got.vulkan_set || other.vulkan_binding != got.vulkan_binding,
              "Vulkan (set, binding) distinct");
    }
  }
  require(bw::shaders::kSamplerSpace == 10U && bw::shaders::kStorageBufferSpace == 17U,
          "array spaces are 10 to 17");
  require(kStorageDescriptors == 64U, "storage arrays bounded at 64 for tier 2");
  require(bw::shaders::kVulkanUniformSet == 2U &&
              bw::shaders::kVulkanUniformSet != kVulkanSamplerSet &&
              bw::shaders::kVulkanUniformSet != kVulkanResourceSet,
          "uniform buffers in set 2, after the sampler and resource sets");
  require(bw::shaders::kVulkanStaticIndexOffset == 0U, "static indices at byte 0");
  require(bw::shaders::kVulkanDrawIndexOffset == 32U, "draw index at byte 32");
  require(bw::shaders::kVulkanRootConstantOffset == 36U, "root constants at byte 36");
  require(bw::shaders::kVulkanPushConstantBytes == 100U, "push constant block is 100 bytes");
}

using Scenario = bw::testing::Scenario<void>;
constexpr std::array scenarios{
    Scenario{"hash_changes_when_slot_moves", hash_changes_when_slot_moves},
    Scenario{"find_static_slot", find_static_slot},
    Scenario{"root_signature_text_is_exact", root_signature_text_is_exact},
    Scenario{"placement_table", placement_table},
};
} // namespace

int main(int argc, char** argv) { return bw::testing::run_scenario(argc, argv, scenarios); }
