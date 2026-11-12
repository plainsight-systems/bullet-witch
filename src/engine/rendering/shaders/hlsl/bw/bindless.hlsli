// The bindless-access include: the only place a target difference exists in
// HLSL (docs/architecture/shaders.md, invariants 1 and 5). It declares the
// typed descriptor arrays, the root constants and the two block-binding
// macros the generated includes use, from the placement table and the root
// layout constants of bw/rendering/shaders/root_layout.hpp and nothing else.
// shader_compiler defines exactly one of BW_TARGET_D3D12, BW_TARGET_METAL or
// BW_TARGET_VULKAN (compiler_options.hpp, append_compiler_arguments).
//
// A shader includes this file first, then its generated include
// "bw/generated/<shader>.hlsli", and reaches resources only through the
// access macros at the end:
//   BW_TEXTURE2D(block.texture).SampleLevel(BW_SAMPLER(block.sampler), uv, 0)
// Every index goes through NonUniformResourceIndex (root_layout.hpp,
// DescriptorArray: one draw may read different slots per lane).
//
// Placement (root_layout.hpp, placement()):
//   array               HLSL type              DXIL register     Vulkan (set, binding)  bound
//   bw_sampler          SamplerState           s0, space10       (0, 0)                 none
//   bw_texture2d        Texture2D              t0, space11       (1, 0)                 none
//   bw_texture_cube     TextureCube            t0, space12       (1, 1)                 none
//   bw_texture2d_array  Texture2DArray         t0, space13       (1, 2)                 none
//   bw_texture3d        Texture3D              t0, space14       (1, 3)                 none
//   bw_buffer           ByteAddressBuffer      t0, space15       (1, 4)                 none
//   bw_storage_texture2d RWTexture2D<float4>   u0, space16       (1, 5)                 64
//   bw_storage_buffer   RWByteAddressBuffer    u0, space17       (1, 6)                 64
// The two storage arrays are declared with their bound of 64
// (kStorageDescriptors), not as unbounded arrays: the root signature's UAV
// ranges hold 64 descriptors (resource binding tier 2), and a shader range
// larger than its root signature range is not a valid pipeline on Direct3D 12.
// DXC and the converter accept either form at compile time; the bound is what
// the placement table states.
//
// Blocks (root_layout.hpp, RootElement):
//   DXIL targets: static slot i is ConstantBuffer<T> at b<i>, space4; the draw
//   block is ConstantBuffer<T> at b0, space0; root constants are the cbuffer at
//   b0, space3.
//   Vulkan: static slot i is the runtime array of ConstantBuffer<T> at set 2,
//   binding i, indexed by push constant word i; the draw block is the runtime
//   array at set 2, binding 8, indexed by the push constant draw index; root
//   constants are the push constant block's words after the nine indices
//   (kVulkanStaticIndexOffset 0, kVulkanDrawIndexOffset 32,
//   kVulkanRootConstantOffset 36, kVulkanPushConstantBytes 100).
//   A macro cannot emit a #define, so on Vulkan the block's name is a static
//   const copy of the indexed element; DXC forwards the loads and drops the
//   copy when the shader reads no member, so an unread block is not bound.
//
// Root constants are read through BW_ROOT_CONSTANT(i), i below
// kRootConstantWords (16). The DXIL declaration is uint4 bw_root[4], not
// uint bw_root[16]: constant buffer packing gives a scalar array a 16-byte
// element stride, so uint[16] occupies 244 bytes and element 5 would be read
// from byte 80, outside the 64 bytes of 16 root constants. uint4[4] is the
// same 64 dense bytes the root signature declares. The Vulkan push constant
// block is dense (array stride 4) and keeps uint root[16].
//
// The draw's index within a counted multi-draw is read through BW_DRAW_INDEX
// (bw/rendering/rhi/command_list.hpp, Indirect work): i for draw i of a
// counted multi-draw, 0 in every other draw. It is the one read for every
// target, so a shader's own text has no target difference:
//   DXIL targets: root constant word kDrawIndexRootConstantWord (15,
//   root_layout.hpp), the block's last word, which callers do not write
//   (kCallerRootConstantWords) and the backend writes per draw.
//   Vulkan: the DrawIndex built-in. A push constant cannot vary within one
//   multi-draw, so the word is not used there. The built-in is declared
//   here as a global input (inline SPIR-V: the DrawParameters capability and
//   its extension), so no entry point names it; the declaration makes every
//   Vulkan shader declare the capability, which the counted multi-draw
//   requires of the device in any case.
// DrawIndex is a vertex-stage input on Vulkan, so only a vertex shader reads
// BW_DRAW_INDEX; a pixel shader that needs the value receives it from its
// vertex shader as a nointerpolation output.
#ifndef BW_BINDLESS_HLSLI
#define BW_BINDLESS_HLSLI

#if defined(BW_TARGET_D3D12) + defined(BW_TARGET_METAL) + defined(BW_TARGET_VULKAN) != 1
#error "exactly one of BW_TARGET_D3D12, BW_TARGET_METAL, BW_TARGET_VULKAN must be defined"
#endif

#if defined(BW_TARGET_VULKAN)

[[vk::binding(0, 0)]] SamplerState bw_sampler[];
[[vk::binding(0, 1)]] Texture2D bw_texture2d[];
[[vk::binding(1, 1)]] TextureCube bw_texture_cube[];
[[vk::binding(2, 1)]] Texture2DArray bw_texture2d_array[];
[[vk::binding(3, 1)]] Texture3D bw_texture3d[];
[[vk::binding(4, 1)]] ByteAddressBuffer bw_buffer[];
[[vk::binding(5, 1)]] RWTexture2D<float4> bw_storage_texture2d[64];
[[vk::binding(6, 1)]] RWByteAddressBuffer bw_storage_buffer[64];

struct BwPushConstants {
  uint static_index[8];
  uint draw_index;
  uint root[16];
};
[[vk::push_constant]] BwPushConstants bw_push;

#define BW_ROOT_CONSTANT(i) bw_push.root[(i)]

#define BW_STATIC_BLOCK(Type, Name, slot)                                                          \
  [[vk::binding(slot, 2)]] ConstantBuffer<Type> bw_block_##slot[];                                 \
  static const Type Name = bw_block_##slot[bw_push.static_index[slot]];

#define BW_DRAW_BLOCK(Type, Name)                                                                  \
  [[vk::binding(8, 2)]] ConstantBuffer<Type> bw_block_8[];                                         \
  static const Type Name = bw_block_8[bw_push.draw_index];

[[vk::ext_extension("SPV_KHR_shader_draw_parameters")]] [[vk::ext_capability(/* DrawParameters */ 4427)]] [[vk::ext_builtin_input(/* DrawIndex */ 4426)]] static const uint bw_vk_draw_index;

#define BW_DRAW_INDEX bw_vk_draw_index

#else // BW_TARGET_D3D12 or BW_TARGET_METAL: the same DXIL.

SamplerState bw_sampler[] : register(s0, space10);
Texture2D bw_texture2d[] : register(t0, space11);
TextureCube bw_texture_cube[] : register(t0, space12);
Texture2DArray bw_texture2d_array[] : register(t0, space13);
Texture3D bw_texture3d[] : register(t0, space14);
ByteAddressBuffer bw_buffer[] : register(t0, space15);
RWTexture2D<float4> bw_storage_texture2d[64] : register(u0, space16);
RWByteAddressBuffer bw_storage_buffer[64] : register(u0, space17);

cbuffer bw_root_constants : register(b0, space3) {
  uint4 bw_root[4];
};

#define BW_ROOT_CONSTANT(i) bw_root[(i) >> 2][(i) & 3]

#define BW_STATIC_BLOCK(Type, Name, slot) ConstantBuffer<Type> Name : register(b##slot, space4);

#define BW_DRAW_BLOCK(Type, Name) ConstantBuffer<Type> Name : register(b0, space0);

#define BW_DRAW_INDEX_ROOT_CONSTANT_WORD 15
#define BW_DRAW_INDEX BW_ROOT_CONSTANT(BW_DRAW_INDEX_ROOT_CONSTANT_WORD)

#endif

#define BW_SAMPLER(i) bw_sampler[NonUniformResourceIndex(i)]
#define BW_TEXTURE2D(i) bw_texture2d[NonUniformResourceIndex(i)]
#define BW_TEXTURE_CUBE(i) bw_texture_cube[NonUniformResourceIndex(i)]
#define BW_TEXTURE2D_ARRAY(i) bw_texture2d_array[NonUniformResourceIndex(i)]
#define BW_TEXTURE3D(i) bw_texture3d[NonUniformResourceIndex(i)]
#define BW_BUFFER(i) bw_buffer[NonUniformResourceIndex(i)]
#define BW_STORAGE_TEXTURE2D(i) bw_storage_texture2d[NonUniformResourceIndex(i)]
#define BW_STORAGE_BUFFER(i) bw_storage_buffer[NonUniformResourceIndex(i)]

#endif
