// The RHI's indirect-work fixture compute shader: each thread group stores
// one 32-bit word into a storage buffer. The RHI integration test writes a
// counted multi-draw's count with it and runs it through dispatch_indirect
// (command_list.hpp, Indirect work).
//
// Registry entry (src/engine/rendering/shaders/permutation.cpp,
// IndirectStore): name indirect_store, entry point main, compute stage, one
// thread per group, an empty permutation domain, no static block, no
// per-draw block. Root constants: word 0 the storage buffer's descriptor
// index, word 1 the byte offset of group 0's word, word 2 the value.
//
// Group g stores the value at byte offset word 1 + 4 * g, so the number of
// groups a dispatch ran is the number of words written.
#include "bw/bindless.hlsli"
#include "bw/generated/indirect_store.hlsli"

[numthreads(1, 1, 1)] void main(uint3 group : SV_GroupID) {
  BW_STORAGE_BUFFER(BW_ROOT_CONSTANT(0)).Store(BW_ROOT_CONSTANT(1) + group.x * 4, BW_ROOT_CONSTANT(2));
}
