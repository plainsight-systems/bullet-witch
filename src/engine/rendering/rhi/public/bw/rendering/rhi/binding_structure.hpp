#ifndef BW_RENDERING_RHI_BINDING_STRUCTURE_HPP
#define BW_RENDERING_RHI_BINDING_STRUCTURE_HPP
#include "bw/rendering/shaders/artifact.hpp"
#include "bw/rendering/shaders/root_layout.hpp"

#include <array>
#include <cstdint>

// Responsibility: the API binding structure derived once per device from the
// fixed root layout (docs/architecture/rhi.md, invariant 5: root signature,
// pipeline layout or argument buffer layout, built once; no pipeline gets its
// own), and the per-pipeline subset of it derived from reflection
// (docs/architecture/shaders.md, "Load-time flow"). Both are pure functions
// of their inputs: the same root layout gives the same structure on every
// backend, and the same reflection the same pipeline binding; those
// equalities are unit tests.
//
// Contract. The root parameters are the root signature's, in its order
// (root_layout.hpp, append_root_signature_text): 0 the resource table, 1 the
// sampler table, 2 the storage table, 3 the draw block, 4 to 11 the static
// slots 0 to 7, 12 the root constants. Each parameter has one byte range in
// the Metal top-level argument buffer: a descriptor table or a root constant
// buffer view is 8 bytes (a GPU address), root constants are 4 bytes per
// word, ranges are consecutive from 0 in parameter order. For the engine
// layout (16 root constant words) that is 160 bytes: tables at 0, 8, 16, the
// draw block at 24, static slot i at 32 + 8 i, root constants at 96 for 64
// bytes. On Direct3D 12 the parameter's index is its root parameter index;
// on Vulkan the block parameters are the push constant words of
// root_layout.hpp (kVulkanStaticIndexOffset, kVulkanDrawIndexOffset,
// kVulkanRootConstantOffset) and the tables are the descriptor sets. A
// backend binds every table a pipeline uses, every static slot in its mask
// and the draw block when it uses one; unused parameters are left as they
// are.
//
// Measured with the converter's resource-location query over the engine root
// signature and over the fixture artifact's embedded signature: thirteen
// entries at top-level offsets 0, 8, 16, 24 and 32 to 88 of 8 bytes each, and
// one at 96 of 64 bytes. The byte rule above is that measurement stated as
// the contract; the integration test that draws the fixture asserts it
// against the converter at run time.
//
// Core guidelines read: Con.5 and I.1 (the structure is a constexpr function
// of the layout; nothing per device is ambient). Performance guidelines
// read: EMB.6 (compile-time data), GDSA.17 (the root layout is the key the
// pipeline set is built against), CACHE.2 (the argument buffer's bytes are
// counted here so the backend's ring is sized from the structure).
namespace bw::rhi {

enum class RootParameterKind : std::uint8_t {
  ResourceTable,
  SamplerTable,
  StorageTable,
  DrawBlock,
  StaticBlock,
  RootConstants,
};

// One 8-byte address per table and per block, the root constants inline.
// The three tables are root parameters of the one root signature, so their
// addresses live in the argument buffer like the blocks.
struct RootParameter {
  RootParameterKind kind{RootParameterKind::ResourceTable};
  std::uint8_t static_slot{};
  std::uint8_t index{};
  std::uint32_t argument_offset{};
  std::uint32_t argument_size{};
  [[nodiscard]] friend constexpr bool operator==(RootParameter, RootParameter) noexcept = default;
};

inline constexpr std::uint32_t kTableParameterCount = 3;
inline constexpr std::uint32_t kRootParameterCount =
    kTableParameterCount + 1U + shaders::kMaxStaticBlocks + 1U;
inline constexpr std::uint32_t kArgumentAddressBytes = 8;
inline constexpr std::uint32_t kRootConstantWordBytes = 4;
// For the engine layout: 3 * 8 + 8 + 8 * 8 + 16 * 4.
inline constexpr std::uint32_t kEngineArgumentBufferBytes = 160;

struct BindingStructure {
  std::array<RootParameter, kRootParameterCount> parameters{};
  std::uint32_t argument_buffer_bytes{};
  [[nodiscard]] constexpr const RootParameter& table(RootParameterKind kind) const noexcept {
    return parameters[static_cast<std::uint8_t>(kind)];
  }
  [[nodiscard]] constexpr const RootParameter& draw_block() const noexcept {
    return parameters[kTableParameterCount];
  }
  [[nodiscard]] constexpr const RootParameter& static_block(std::uint32_t slot) const noexcept {
    return parameters[kTableParameterCount + 1U + slot];
  }
  [[nodiscard]] constexpr const RootParameter& root_constants() const noexcept {
    return parameters[kRootParameterCount - 1U];
  }
  [[nodiscard]] friend constexpr bool operator==(const BindingStructure&,
                                                 const BindingStructure&) noexcept = default;
};

[[nodiscard]] constexpr BindingStructure
derive_binding_structure(const shaders::RootLayout& layout) noexcept {
  BindingStructure structure{};
  std::uint32_t offset = 0;
  std::uint8_t index = 0;
  const auto place = [&](RootParameterKind kind, std::uint8_t slot, std::uint32_t size) {
    structure.parameters[index] = RootParameter{kind, slot, index, offset, size};
    offset += size;
    ++index;
  };
  place(RootParameterKind::ResourceTable, 0, kArgumentAddressBytes);
  place(RootParameterKind::SamplerTable, 0, kArgumentAddressBytes);
  place(RootParameterKind::StorageTable, 0, kArgumentAddressBytes);
  place(RootParameterKind::DrawBlock, 0, kArgumentAddressBytes);
  for (std::uint8_t slot = 0; slot < shaders::kMaxStaticBlocks; ++slot) {
    place(RootParameterKind::StaticBlock, slot, kArgumentAddressBytes);
  }
  place(RootParameterKind::RootConstants, 0, layout.root_constant_words * kRootConstantWordBytes);
  structure.argument_buffer_bytes = offset;
  return structure;
}

// What one pipeline binds of the structure: the union over its stages'
// BindingUse (artifact.hpp, derive_binding_use). Backends bind the tables
// whose arrays the pipeline declares; the two storage arrays share one
// table, the five read arrays another.
struct PipelineBinding {
  shaders::LayoutHash draw_block_hash{};
  std::uint32_t root_constant_words{};
  std::uint32_t descriptor_arrays{};
  std::uint8_t static_slot_mask{};
  bool uses_draw_block{};
  bool uses_resource_table{};
  bool uses_sampler_table{};
  bool uses_storage_table{};
  [[nodiscard]] friend constexpr bool operator==(const PipelineBinding&,
                                                 const PipelineBinding&) noexcept = default;
};

// pixel may be null (depth-only pipeline, or compute).
[[nodiscard]] constexpr PipelineBinding
derive_pipeline_binding(const shaders::BindingUse& first,
                        const shaders::BindingUse* second) noexcept {
  PipelineBinding binding{};
  binding.draw_block_hash = first.draw_block_hash;
  binding.uses_draw_block = first.uses_draw_block;
  binding.root_constant_words = first.root_constant_words;
  binding.descriptor_arrays = first.descriptor_arrays;
  binding.static_slot_mask = first.static_slot_mask;
  if (second != nullptr) {
    if (!binding.uses_draw_block && second->uses_draw_block) {
      binding.draw_block_hash = second->draw_block_hash;
    }
    binding.uses_draw_block = binding.uses_draw_block || second->uses_draw_block;
    binding.root_constant_words = binding.root_constant_words > second->root_constant_words
                                      ? binding.root_constant_words
                                      : second->root_constant_words;
    binding.descriptor_arrays |= second->descriptor_arrays;
    binding.static_slot_mask |= second->static_slot_mask;
  }
  for (std::uint32_t bit = 0; bit < shaders::kDescriptorArrayCount; ++bit) {
    if ((binding.descriptor_arrays & (1U << bit)) == 0U) {
      continue;
    }
    switch (shaders::placement(static_cast<shaders::DescriptorArray>(bit)).table) {
    case shaders::RootElement::SamplerTable:
      binding.uses_sampler_table = true;
      break;
    case shaders::RootElement::StorageTable:
      binding.uses_storage_table = true;
      break;
    case shaders::RootElement::ResourceTable:
    case shaders::RootElement::StaticBlock:
    case shaders::RootElement::DrawBlock:
    case shaders::RootElement::RootConstants:
      binding.uses_resource_table = true;
      break;
    }
  }
  return binding;
}

} // namespace bw::rhi
#endif
