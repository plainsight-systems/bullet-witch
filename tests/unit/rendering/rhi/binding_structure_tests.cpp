// Unit test: public contracts of bw/rendering/rhi/binding_structure.hpp, one
// scenario per CTest entry. Each scenario names the header lines it asserts.
#include "bw/rendering/rhi/binding_structure.hpp"

#include "bw/rendering/shaders/root_layout.hpp"
#include "test_main.hpp"

#include <array>
#include <cstdint>

namespace {
using bw::rhi::BindingStructure;
using bw::rhi::PipelineBinding;
using bw::rhi::RootParameterKind;
using bw::shaders::BindingUse;
using bw::shaders::DescriptorArray;
using bw::shaders::LayoutHash;
using bw_rhi_test::require;

constexpr std::uint32_t bit(DescriptorArray array) noexcept {
  return 1U << static_cast<std::uint32_t>(array);
}

// binding_structure.hpp:18-27 and 85-91, 112-132: thirteen parameters in
// the root signature's order, 8 bytes per table and block, 4 per root
// constant word, consecutive from 0; for the engine layout, 160 bytes with
// tables at 0, 8, 16, the draw block at 24, static slot i at 32 + 8 i and
// the root constants at 96 for 64 bytes.
void engine_layout_is_160_bytes_in_order() {
  const BindingStructure structure =
      bw::rhi::derive_binding_structure(bw::shaders::engine_root_layout());
  require(structure.argument_buffer_bytes == bw::rhi::kEngineArgumentBufferBytes &&
              structure.argument_buffer_bytes == 160U,
          "160 bytes");
  require(bw::rhi::kRootParameterCount == 13U, "thirteen root parameters");
  const auto& resource = structure.table(RootParameterKind::ResourceTable);
  const auto& sampler = structure.table(RootParameterKind::SamplerTable);
  const auto& storage = structure.table(RootParameterKind::StorageTable);
  require(resource.index == 0U && resource.argument_offset == 0U && resource.argument_size == 8U,
          "the resource table is parameter 0 at 0");
  require(sampler.index == 1U && sampler.argument_offset == 8U, "the sampler table at 8");
  require(storage.index == 2U && storage.argument_offset == 16U, "the storage table at 16");
  require(structure.draw_block().kind == RootParameterKind::DrawBlock &&
              structure.draw_block().index == 3U && structure.draw_block().argument_offset == 24U,
          "the draw block is parameter 3 at 24");
  for (std::uint32_t slot = 0; slot < bw::shaders::kMaxStaticBlocks; ++slot) {
    const auto& block = structure.static_block(slot);
    require(block.kind == RootParameterKind::StaticBlock && block.static_slot == slot &&
                block.index == 4U + slot && block.argument_offset == 32U + 8U * slot &&
                block.argument_size == 8U,
            "static slot i is parameter 4 + i at 32 + 8 i");
  }
  const auto& constants = structure.root_constants();
  require(constants.kind == RootParameterKind::RootConstants && constants.index == 12U &&
              constants.argument_offset == 96U && constants.argument_size == 64U,
          "the root constants are parameter 12 at 96 for 64 bytes");
}

// binding_structure.hpp:12-16: the same root layout gives the same
// structure; the structure follows the layout's root constant words.
void same_inputs_same_structure() {
  const bw::shaders::RootLayout layout = bw::shaders::engine_root_layout();
  require(bw::rhi::derive_binding_structure(layout) ==
              bw::rhi::derive_binding_structure(bw::shaders::engine_root_layout()),
          "equal layouts give equal structures");
  bw::shaders::RootLayout fewer = layout;
  fewer.root_constant_words = 4;
  const BindingStructure smaller = bw::rhi::derive_binding_structure(fewer);
  require(smaller.root_constants().argument_size == 16U && smaller.argument_buffer_bytes == 112U,
          "four root constant words are 16 bytes");
}

// binding_structure.hpp:134-197: a pipeline binds the union of its stages'
// uses: static slots and arrays OR-ed, the larger root constant count, the
// draw block of whichever stage uses one; the tables follow the arrays (the
// five read arrays share the resource table, the two storage arrays the
// storage table).
void pipeline_binding_is_the_union() {
  BindingUse vertex{};
  vertex.static_slot_mask = 0b0000'0011U;
  vertex.root_constant_words = 4;
  vertex.descriptor_arrays = bit(DescriptorArray::Buffer);
  BindingUse pixel{};
  pixel.static_slot_mask = 0b0000'0100U;
  pixel.root_constant_words = 8;
  pixel.descriptor_arrays = bit(DescriptorArray::Texture2D) | bit(DescriptorArray::Sampler);
  pixel.uses_draw_block = true;
  pixel.draw_block_hash = LayoutHash{0xABCDU};

  const PipelineBinding binding = bw::rhi::derive_pipeline_binding(vertex, &pixel);
  require(binding.static_slot_mask == 0b0000'0111U, "static slots are the union");
  require(binding.root_constant_words == 8U, "the larger root constant count");
  require(binding.descriptor_arrays ==
              (bit(DescriptorArray::Buffer) | bit(DescriptorArray::Texture2D) |
               bit(DescriptorArray::Sampler)),
          "arrays are the union");
  require(binding.uses_draw_block && binding.draw_block_hash == LayoutHash{0xABCDU},
          "the pixel stage's draw block");
  require(binding.uses_resource_table && binding.uses_sampler_table && !binding.uses_storage_table,
          "tables follow the arrays");

  const PipelineBinding vertex_only = bw::rhi::derive_pipeline_binding(vertex, nullptr);
  require(vertex_only.static_slot_mask == vertex.static_slot_mask && !vertex_only.uses_draw_block,
          "a single stage binds its own use");
  require(vertex_only.uses_resource_table && !vertex_only.uses_sampler_table,
          "a buffer uses the resource table");

  BindingUse compute{};
  compute.descriptor_arrays = bit(DescriptorArray::StorageTexture2D);
  const PipelineBinding storage = bw::rhi::derive_pipeline_binding(compute, nullptr);
  require(storage.uses_storage_table && !storage.uses_resource_table,
          "storage arrays use the storage table");
  require(bw::rhi::derive_pipeline_binding(vertex, &pixel) ==
              bw::rhi::derive_pipeline_binding(vertex, &pixel),
          "the same uses give the same binding");
}

constexpr std::array scenarios{
    bw_rhi_test::Scenario{"engine_layout_is_160_bytes_in_order",
                          engine_layout_is_160_bytes_in_order},
    bw_rhi_test::Scenario{"same_inputs_same_structure", same_inputs_same_structure},
    bw_rhi_test::Scenario{"pipeline_binding_is_the_union", pipeline_binding_is_the_union},
};
} // namespace

int main(int argc, char** argv) { return bw_rhi_test::run_scenario(argc, argv, scenarios); }
