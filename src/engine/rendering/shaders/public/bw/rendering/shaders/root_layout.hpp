#ifndef BW_RENDERING_SHADERS_ROOT_LAYOUT_HPP
#define BW_RENDERING_SHADERS_ROOT_LAYOUT_HPP
#include "bw/rendering/shaders/parameter_block.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

// Responsibility: the one binding layout every pipeline on every API binds
// against, as data, fixed before any shader is compiled. Owned by the shader
// system; consumed by shader_compiler (the root signature embedded in DXIL,
// the register spaces and sets of the descriptor arrays) and by each RHI
// backend (root signature, pipeline layout, argument buffer table). See
// docs/architecture/shaders.md, "Draw-time binding" and invariant 5: one
// binding model, typed unbounded descriptor arrays, on every target.
//
// Contract: the layout is a value. Nothing here allocates except the root
// signature text generator, which appends to a std::string at build time.
// The engine's instance is returned by engine_root_layout(); its hash is
// written into every artifact and compared at load (artifact.hpp), so a
// change to the instance invalidates every artifact rather than binding
// against the wrong layout. Failure: a block name that is not in the static
// slot table is reported by find_static_slot as no value; nothing degrades
// to another model.
//
// Core guidelines read for this header: I.1 (the layout is an explicit
// parameter of the tool and the loader, never ambient), Enum.3 (every
// enumeration is a class enum), Con.5 (all constants, placements and the
// hash are constexpr). Performance guidelines read: EMB.6 (the layout, the
// placement table and the hash are compile-time data), GDSA.17 (the root
// layout is the key the pipeline cache is built against; it changes only
// with a rebuild). A search of the performance corpus for GPU root signature
// or binding layout rules returned no applicable rule; that search is
// recorded here rather than citing an unrelated rule.
namespace bw::shaders {

// The three bytecode targets. Xbox Series X uses the Direct3D 12 target.
// Unreal f45729c0b6f097038e72b52d366a62baeca3275b
// Engine/Source/Runtime/RHI/Public/RHIDefinitions.h:188-206 enumerates shader
// frequencies, not targets; Unreal names targets by shader format string. The
// game ships three backends, so a fixed enum replaces the format registry.
enum class ShaderTarget : std::uint8_t {
  Direct3D12 = 1,
  Vulkan = 2,
  Metal = 3,
};

// Elements of the root layout. Identical on every API; the table says what
// each becomes.
//
//   Element          Direct3D 12               Vulkan                    Metal (converter)
//   StaticBlock i    root CBV, space 4, b<i>   push constant index i     CBV table entry i
//   DrawBlock        root CBV, space 0, b0     push constant index 8     CBV table entry 8
//   RootConstants    root constants, space 3   push constants, 16 words  argument buffer bytes
//   SamplerTable     sampler descriptor table  set 0, runtime array      sampler table entry
//   ResourceTable    SRV descriptor table      set 1, one runtime array  argument buffer table
//                    of unbounded typed ranges per type                  per range
//   StorageTable     UAV descriptor table      set 1, 64-entry arrays    argument buffer table
//                    of 64-entry typed ranges
//
// Unreal f45729c0b6f097038e72b52d366a62baeca3275b
// Engine/Shaders/Shared/HLSLReservedSpaces.h:20-54 reserves space 4 for the
// static shader bindings and space 3 for shader root constants; the two space
// numbers are kept so the pinned HLSL conventions read unchanged.
// Engine/Source/Runtime/VulkanRHI/Private/VulkanBindlessDescriptorManager.cpp:154-164
// is the push constant range of MaxUniformBufferEntries * 4 bytes for the
// static uniform buffers, stage flags ALL.
// Engine/Source/Runtime/VulkanRHI/Public/VulkanCommon.h:95-157 fixes the
// sampler set at 0 and the resource set at 1.
// Engine/Source/Developer/Apple/MetalShaderFormat/Private/MetalCompileShaderMSC.cpp:1198-1211
// builds one CBV root parameter per static entry in space 4 for the converter
// root signature; :1216-1220 is root signature version 1.1;
// Engine/Source/Runtime/Apple/MetalRHI/Private/MetalStateCache.cpp:1948-1952
// sets the CBV table as argument buffer bytes at kIRArgumentBufferBindPoint.
// Engine/Source/Runtime/D3D12RHI/Private/D3D12RootSignature.cpp:411-428 adds
// one root CBV per static binding entry at (CBVResourceIndex, RegisterSpace),
// visibility ALL; :430-437 adds the root constants at register 0 of space 3.
// Departure: the pin's root constants are 4 words (:434); 16 here, fixed for
// every pipeline, so the layout never varies per shader.
// Omitted: the ray tracing local and global spaces (0, 1, 2), the work graph
// spaces, the diagnostic space 999 and the NVIDIA extension space 1001 at
// HLSLReservedSpaces.h:23-39,48-54. The game has no ray tracing or work graph
// pass and ships no vendor extension.
enum class RootElement : std::uint8_t {
  StaticBlock,
  DrawBlock,
  RootConstants,
  SamplerTable,
  ResourceTable,
  StorageTable,
};

// The typed descriptor arrays. Each is one unbounded HLSL array in its own
// register space (Direct3D 12 and Metal) or one runtime array at its own
// binding (Vulkan); a resource is reached by a 32-bit index into the array
// of its type, always through NonUniformResourceIndex. Every read array
// range starts at descriptor offset 0 of the resource table, so one index
// names the same slot in every read array and the engine writes the slot in
// the array that matches the resource's type; the same holds for the two
// storage arrays within the storage table. The bindless-access include
// declares exactly these arrays, per target, and nothing else.
// Unreal f45729c0b6f097038e72b52d366a62baeca3275b
// Engine/Source/Runtime/RenderCore/Private/ShaderParameterParser.cpp:971-1028:
// a bindless parameter becomes a uint index (:989) and, under
// BindlessUsesArrays, an unbounded array of its type (:1006-1014);
// Engine/Source/Developer/VulkanShaderFormat/Private/VulkanShaderCompiler.cpp:221-251
// reads it as array[NonUniformResourceIndex(index)] (:243) or array[index]
// (:248); Engine/Source/Developer/VulkanShaderFormat/Private/SpirVShaderCompiler.inl:567-600
// moves each array to binding 0 of the set of its descriptor type (samplers
// :589, sampled images :594, storage buffers :592-593, storage images
// :595-596). That Vulkan path is the guide for all three targets here.
// Departures: one array per resource type instead of one per parameter, at
// fixed distinct bindings of set 1, so the include is fixed text and no
// SPIR-V patching is needed; non-uniform indexing always, since one draw may
// read different slots per lane.
// Omitted: the shader model 6.6 descriptor heap access of the pin's Direct3D
// path (Engine/Source/Developer/Windows/ShaderFormatD3D/Private/D3DShaderCompiler.cpp:1384-1421,
// ResourceDescriptorHeap and SamplerDescriptorHeap at :1397), because the
// Xbox Dev Mode container stops at shader model 6.4 and the game ships one
// model; and the converter's six static samplers in space 1000
// (Engine/Source/Runtime/Apple/MetalRHI/Public/MetalShaderResources.h:124-132),
// because samplers come through the sampler table like every other
// descriptor.
enum class DescriptorArray : std::uint8_t {
  Sampler = 0,
  Texture2D = 1,
  TextureCube = 2,
  Texture2DArray = 3,
  Texture3D = 4,
  Buffer = 5,
  StorageTexture2D = 6,
  StorageBuffer = 7,
};
inline constexpr std::uint32_t kDescriptorArrayCount = 8;

// Where one array lives on each API. register_space and vulkan_binding are
// fixed below; vulkan_set is kVulkanSamplerSet for the sampler array and
// kVulkanResourceSet otherwise; max_descriptors is 0 for an unbounded range.
struct DescriptorArrayPlacement {
  std::string_view hlsl_type{};
  RootElement table{RootElement::ResourceTable};
  std::uint32_t register_space{};
  std::uint32_t vulkan_set{};
  std::uint32_t vulkan_binding{};
  std::uint32_t max_descriptors{};
  char register_class{};
};
[[nodiscard]] constexpr DescriptorArrayPlacement placement(DescriptorArray array) noexcept;

// Unreal f45729c0b6f097038e72b52d366a62baeca3275b
// Engine/Source/Runtime/RHI/Public/RHIShaderBindingLayout.h:77:
// MaxUniformBufferEntries = 8.
inline constexpr std::uint32_t kMaxStaticBlocks = 8;
// Root constants, in 32-bit words. Chosen so the Vulkan push constant block
// (8 static indices + 1 draw index + 16 words = 100 bytes) fits the 128-byte
// minimum every Vulkan 1.3 device guarantees. Not a pinned value: Unreal sizes
// root constants per shader; one fixed size is what makes the layout fixed.
inline constexpr std::uint32_t kRootConstantWords = 16;
// HLSL register spaces for the Direct3D 12 and Metal (DXIL) targets.
inline constexpr std::uint32_t kDrawBlockSpace = 0;
inline constexpr std::uint32_t kRootConstantSpace = 3;
inline constexpr std::uint32_t kStaticBlockSpace = 4;
inline constexpr std::uint32_t kSamplerSpace = 10;
inline constexpr std::uint32_t kTexture2DSpace = 11;
inline constexpr std::uint32_t kTextureCubeSpace = 12;
inline constexpr std::uint32_t kTexture2DArraySpace = 13;
inline constexpr std::uint32_t kTexture3DSpace = 14;
inline constexpr std::uint32_t kBufferSpace = 15;
inline constexpr std::uint32_t kStorageTexture2DSpace = 16;
inline constexpr std::uint32_t kStorageBufferSpace = 17;
// Resource binding tier 2 bounds (docs/architecture/shaders.md, "Targets"):
// unbounded sampler and SRV ranges, a sampler heap of at most 2048, and at
// most 64 UAVs per stage. The storage arrays are bounded at 64 so the layout
// is valid on tier 2 hardware; a storage index is below this bound.
// Unreal f45729c0b6f097038e72b52d366a62baeca3275b
// Engine/Source/Runtime/D3D12RHI/Private/D3D12Util.cpp:1194-1204: at tier 2
// the sampler and SRV counts become the maxima and UAV counts round up to
// a power of two below MAX_UAVS (Engine/Source/Runtime/D3D12RHI/Public/D3D12RHI.h:17-20:
// MAX_SRVS 64, MAX_SAMPLERS 32, MAX_UAVS 16). Departure: the pin's maxima are
// per-shader register counts for its bound-resource model; here the ranges
// are the tier's own limits because every resource is reached by index.
inline constexpr std::uint32_t kMaxSamplerDescriptors = 2048;
inline constexpr std::uint32_t kStorageDescriptors = 64;
// Vulkan descriptor sets, and the push constant block layout in bytes.
inline constexpr std::uint32_t kVulkanSamplerSet = 0;
inline constexpr std::uint32_t kVulkanResourceSet = 1;
// Uniform buffers reached by index live in their own set, after the two
// heaps, as the pin's single-use uniform buffer set does
// (VulkanCommon.h:117-120: the set after NumBindlessSets).
inline constexpr std::uint32_t kVulkanUniformSet = 2;
inline constexpr std::uint32_t kVulkanStaticIndexOffset = 0;
inline constexpr std::uint32_t kVulkanDrawIndexOffset = kMaxStaticBlocks * 4U;
inline constexpr std::uint32_t kVulkanRootConstantOffset = kVulkanDrawIndexOffset + 4U;
inline constexpr std::uint32_t kVulkanPushConstantBytes =
    kVulkanRootConstantOffset + kRootConstantWords * 4U;
static_assert(kVulkanPushConstantBytes <= 128U, "push constants exceed the Vulkan 1.3 minimum");

// One static slot: the block bound there for the whole frame, by name and
// layout hash. An empty block_name is an unused slot.
// Unreal f45729c0b6f097038e72b52d366a62baeca3275b
// Engine/Source/Runtime/RHI/Public/RHIShaderBindingLayout.h:13-50: the pinned
// entry carries a layout name and packed register indices (RegisterSpace,
// CBVResourceIndex, BaseSRV, BaseUAV, BaseSampler). Omitted: the SRV, UAV and
// sampler base indices, which exist for the non-bindless branch; every
// resource here is an array index, so a slot binds one constant buffer only.
struct StaticSlot {
  std::string_view block_name{};
  LayoutHash layout_hash{};
};

// The fixed layout. Slot i is register b<i> in kStaticBlockSpace, push
// constant word i on Vulkan, CBV table entry i on Metal. Equality of two
// layouts is equality of their hashes. The descriptor arrays are not part of
// the value because they never vary: placement() is the whole table.
struct RootLayout {
  std::array<StaticSlot, kMaxStaticBlocks> static_slots{};
  std::uint32_t root_constant_words{kRootConstantWords};
};

// FNV-1a 64 over the slot table in slot order (name bytes, layout hash as
// eight little-endian bytes, per slot) then root_constant_words as four
// little-endian bytes. Two layouts with the same slots in the same order hash
// equal; moving a block to another slot changes the hash.
// Unreal f45729c0b6f097038e72b52d366a62baeca3275b
// Engine/Source/Runtime/RHI/Public/RHIShaderBindingLayout.h:136-141 hashes
// flags, entry count and each entry's name and flags. The hashed inputs are
// kept; the hash function is FNV-1a so it is constexpr with no dependency.
[[nodiscard]] constexpr LayoutHash root_layout_hash(const RootLayout& layout) noexcept;

// The slot a block occupies, by block name; no value when it is not static.
// Unreal f45729c0b6f097038e72b52d366a62baeca3275b
// Engine/Source/Runtime/RHI/Public/RHIShaderBindingLayout.h:99-110: FindEntry
// scans the entries by layout name.
[[nodiscard]] constexpr std::optional<std::uint32_t>
find_static_slot(const RootLayout& layout, std::string_view block_name) noexcept;

// The engine's one layout. Defined by the shader module; the renderer slices
// that introduce static blocks edit that definition, and nothing else. It
// holds no block until the first renderer slice adds one.
[[nodiscard]] const RootLayout& engine_root_layout() noexcept;

// The HLSL root signature of a layout, as the text DXC compiles with
// -rootsig-define and embeds in the DXIL; the converter reads it from there
// for its explicit argument buffer layout, and the Direct3D 12 backend
// creates its root signature from the same text or from the blob. Exactly,
// with ", " between parameters and no newline, root parameters in the pin's
// order (descriptor tables, then the per-pipeline CBV, then the static CBVs,
// then the root constants):
//   RootFlags(ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT),
//   DescriptorTable(SRV(t0, space=11, numDescriptors=unbounded, offset=0,
//                       flags=DESCRIPTORS_VOLATILE|DATA_STATIC_WHILE_SET_AT_EXECUTE),
//                   the same for spaces 12, 13, 14 and 15),
//   DescriptorTable(Sampler(s0, space=10, numDescriptors=unbounded, offset=0,
//                           flags=DESCRIPTORS_VOLATILE)),
//   DescriptorTable(UAV(u0, space=16, numDescriptors=64, offset=0,
//                       flags=DESCRIPTORS_VOLATILE|DATA_VOLATILE),
//                   the same for space 17),
//   CBV(b0, space=0, flags=DATA_STATIC),
//   CBV(b0, space=4, flags=DATA_STATIC), ..., CBV(b7, space=4, flags=DATA_STATIC),
//   RootConstants(num32BitConstants=<root_constant_words>, b0, space=3)
// Root parameter indices are therefore: 0 the resource table, 1 the sampler
// table, 2 the storage table, 3 the draw block, 4 to 11 the static slots,
// 12 the root constants. All eight static CBVs are present whether or not a
// slot is used, so every pipeline has the same indices. Every typed range
// starts at offset 0 of its table (DXC rejects a second unbounded range with
// an implicit offset, and offset 0 is what gives one index space per table).
// The text is an input of the artifact hash through the argument list.
// Unreal f45729c0b6f097038e72b52d366a62baeca3275b
// Engine/Source/Runtime/D3D12RHI/Private/D3D12RootSignature.cpp:287-328 builds
// the SRV, sampler and UAV descriptor tables first, in that order, each one
// range; :332-338 the per-shader root CBVs after them; :411-428 the static
// binding CBVs; :430-437 the root constants last. :181-197 picks the range
// flags by tier: SRV DATA_STATIC_WHILE_SET_AT_EXECUTE plus
// DESCRIPTORS_VOLATILE above tier 1, UAV DATA_VOLATILE plus
// DESCRIPTORS_VOLATILE above tier 2, sampler DESCRIPTORS_VOLATILE above tier
// 1, and :197 DATA_STATIC for root CBVs; the flags above are those of
// Engine/Source/Runtime/D3D12RHI/Public/D3D12RootSignatureDefinitions.h:24-30.
// Departures: the pin sizes each range from the shader's quantized register
// counts (:292, :313, :322) and repeats the tables per visibility
// (:165-176); here one unbounded or 64-entry range per type, visibility ALL,
// so every pipeline shares one root signature. The pin's converter root
// signature holds root CBVs only (MetalCompileShaderMSC.cpp:1198-1211) and
// its descriptor range helper (:117-145) is unused; the tables above are
// what the converter's explicit layout mode requires for unbounded arrays.
void append_root_signature_text(const RootLayout& layout, std::string& out);

// What one compiled shader uses of the root layout, derived from reflection at
// load by every backend (artifact.hpp, derive_binding_use). The same
// reflection gives the same BindingUse on every target; that equality is a
// test. draw_block_hash is meaningful only when uses_draw_block.
// descriptor_arrays has bit (1 << DescriptorArray) set for each array the
// shader declares and reads or writes.
struct BindingUse {
  LayoutHash draw_block_hash{};
  std::uint32_t root_constant_words{};
  std::uint32_t descriptor_arrays{};
  std::uint8_t static_slot_mask{};
  bool uses_draw_block{};
  [[nodiscard]] friend constexpr bool operator==(const BindingUse&,
                                                 const BindingUse&) noexcept = default;
};

// Omitted from this layout, with the pinned step each replaces:
// - The non-bindless root signature built from quantized per-stage register
//   counts and the USE_STATIC_ROOT_SIGNATURE Windows branch
//   (Engine/Source/Runtime/D3D12RHI/Private/D3D12Util.cpp:1383-1470,
//   Engine/Source/Runtime/D3D12RHI/Private/Windows/WindowsD3D12RHIDefinitions.h:29).
//   The game ships one bindless model on every target (invariant 5).
// - Vulkan's legacy per-pipeline descriptor set layouts and the descriptor
//   buffer mode (Engine/Source/Runtime/VulkanRHI/Private/VulkanDescriptorSets.cpp:16-93,
//   VulkanBindlessDescriptorManager.cpp:691-960; mode selection at :395-410).
//   One mode: descriptor heaps.
// - Metal's direct SetShaderBuffer binding when the converter is not in use
//   (Engine/Source/Runtime/Apple/MetalRHI/Private/MetalCommands.cpp:111-132).
//   Every Metal pipeline here is converter-built.

// ---- constexpr definitions ------------------------------------------------

constexpr DescriptorArrayPlacement placement(DescriptorArray array) noexcept {
  switch (array) {
  case DescriptorArray::Sampler:
    return {"SamplerState", RootElement::SamplerTable, kSamplerSpace, kVulkanSamplerSet, 0, 0, 's'};
  case DescriptorArray::Texture2D:
    return {"Texture2D", RootElement::ResourceTable, kTexture2DSpace, kVulkanResourceSet, 0, 0,
            't'};
  case DescriptorArray::TextureCube:
    return {"TextureCube",
            RootElement::ResourceTable,
            kTextureCubeSpace,
            kVulkanResourceSet,
            1,
            0,
            't'};
  case DescriptorArray::Texture2DArray:
    return {"Texture2DArray",
            RootElement::ResourceTable,
            kTexture2DArraySpace,
            kVulkanResourceSet,
            2,
            0,
            't'};
  case DescriptorArray::Texture3D:
    return {"Texture3D", RootElement::ResourceTable, kTexture3DSpace, kVulkanResourceSet, 3, 0,
            't'};
  case DescriptorArray::Buffer:
    return {"ByteAddressBuffer",
            RootElement::ResourceTable,
            kBufferSpace,
            kVulkanResourceSet,
            4,
            0,
            't'};
  case DescriptorArray::StorageTexture2D:
    return {"RWTexture2D<float4>",
            RootElement::StorageTable,
            kStorageTexture2DSpace,
            kVulkanResourceSet,
            5,
            kStorageDescriptors,
            'u'};
  case DescriptorArray::StorageBuffer:
    return {"RWByteAddressBuffer",
            RootElement::StorageTable,
            kStorageBufferSpace,
            kVulkanResourceSet,
            6,
            kStorageDescriptors,
            'u'};
  }
  return {};
}

constexpr LayoutHash root_layout_hash(const RootLayout& layout) noexcept {
  detail::Fnv1a64 hash{};
  for (const StaticSlot& slot : layout.static_slots) {
    hash.append(slot.block_name);
    hash.append_u64(slot.layout_hash.value);
  }
  hash.append_u32(layout.root_constant_words);
  return LayoutHash{hash.state};
}

constexpr std::optional<std::uint32_t> find_static_slot(const RootLayout& layout,
                                                        std::string_view block_name) noexcept {
  for (std::uint32_t index = 0; index < kMaxStaticBlocks; ++index) {
    const StaticSlot& slot = layout.static_slots[index];
    if (!slot.block_name.empty() && slot.block_name == block_name) {
      return index;
    }
  }
  return std::nullopt;
}

} // namespace bw::shaders
#endif
