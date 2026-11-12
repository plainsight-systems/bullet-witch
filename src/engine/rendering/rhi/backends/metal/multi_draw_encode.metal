// The Metal backend's own kernel (docs/architecture/source-layout.md: a
// kernel in the API's language for an operation the API offers no other way
// to perform). It performs the counted multi-draw of command_list.hpp,
// Indirect work, on this backend: thread i resets command i of the indirect
// command buffer and, when i is below the draw count, writes command i's own
// copy of the top-level argument buffer with the draw's index in its last
// root constant word, binds that copy and the two descriptor heaps for both
// stages, and encodes an indexed draw from record i; thread 16,384 r writes
// execution range r. The
// render encoder then executes the ranges (metal_context.cpp,
// draw_indexed_indirect_count). Built by the Metal toolchain at build time
// and embedded in bw_rhi (src/engine/rendering/rhi/CMakeLists.txt).
//
// The kernel reaches the indirect command buffer through an argument
// buffer. A reset command does nothing when executed, and a command is
// reset before a new command is encoded into it. An execution range is
// location then length, two 32-bit words, at most 16,384 commands per range
// on one GPU family. A command that does not inherit buffers sets each one
// it reads, up to the descriptor's maximum index. The indirect command
// buffer is not optimized after the encode: an optimized range can only run
// whole, and the executed range here is the count's. Every command
// inherits the render encoder's pipeline (metal_context.cpp). A command
// does not bind the converter's draw arguments and index type bind points
// (4 and 5): no vertex function the engine builds reads them
// (metal_context.cpp, draw_indexed_indirect_count).
#include <metal_stdlib>
using namespace metal;

// The layout metal_context.cpp writes with setBytes (EncodeArguments there):
// 88 bytes, the command buffer's resource id first, then GPU addresses, then
// six words.
struct EncodeArguments {
  command_buffer commands;
  device const uchar* records;
  device const uint* count;
  device uint2* ranges;
  device const uchar* indices;
  device uint* copies;
  device const uchar* resource_heap;
  device const uchar* sampler_heap;
  uint stride;
  uint maximum;
  uint index_32;
  uint primitive;
  uint draw_index_word;
  uint padding;
};

// The commands one execution range covers.
constant uint kRangeCommands = 16384;
// The top-level argument buffer's size in words (binding_structure.hpp,
// kEngineArgumentBufferBytes) and the stride of the per-command copies in
// words (metal_multi_draw.hpp, kArgumentCopyStride).
constant uint kArgumentWords = 40;
constant uint kCopyStrideWords = 64;
// The converter's bind points (metal_api.hpp).
constant uint kDescriptorHeapBindPoint = 0;
constant uint kSamplerHeapBindPoint = 1;
constant uint kArgumentBufferBindPoint = 2;

kernel void encode_multi_draw(constant EncodeArguments& arguments [[buffer(0)]],
                              constant uint* top_level [[buffer(1)]],
                              uint i [[thread_position_in_grid]]) {
  const uint count = min(*arguments.count, arguments.maximum);
  if (i % kRangeCommands == 0) {
    const uint first = i;
    const uint length = count > first ? min(count - first, kRangeCommands) : 0;
    arguments.ranges[i / kRangeCommands] = uint2(first, length);
  }
  render_command command(arguments.commands, i);
  command.reset();
  if (i >= count) {
    return;
  }
  // IndirectDrawRecord (command_list.hpp): draw_index, then the five words
  // of DrawIndexedArguments.
  device const uint* record = (device const uint*)(arguments.records + ulong(i) * arguments.stride);
  const uint index_count = record[1];
  const uint instance_count = record[2];
  const uint first_index = record[3];
  const uint base_vertex = record[4];
  const uint first_instance = record[5];
  device uint* copy = arguments.copies + ulong(i) * kCopyStrideWords;
  for (uint word = 0; word < kArgumentWords; ++word) {
    copy[word] = top_level[word];
  }
  copy[arguments.draw_index_word] = i;
  command.set_vertex_buffer(arguments.resource_heap, kDescriptorHeapBindPoint);
  command.set_vertex_buffer(arguments.sampler_heap, kSamplerHeapBindPoint);
  command.set_vertex_buffer(copy, kArgumentBufferBindPoint);
  command.set_fragment_buffer(arguments.resource_heap, kDescriptorHeapBindPoint);
  command.set_fragment_buffer(arguments.sampler_heap, kSamplerHeapBindPoint);
  command.set_fragment_buffer(copy, kArgumentBufferBindPoint);
  const primitive_type primitive = static_cast<primitive_type>(arguments.primitive);
  if (arguments.index_32 != 0) {
    command.draw_indexed_primitives(primitive, index_count,
                                    (device const uint*)arguments.indices + first_index,
                                    instance_count, base_vertex, first_instance);
  } else {
    command.draw_indexed_primitives(primitive, index_count,
                                    (device const ushort*)arguments.indices + first_index,
                                    instance_count, base_vertex, first_instance);
  }
}
