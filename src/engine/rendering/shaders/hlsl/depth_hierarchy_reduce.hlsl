// The depth hierarchy's build kernel (bw/rendering/scene/depth_hierarchy.hpp,
// contract 3; compute_shaders.hpp, contract 2): one dispatch per level, 8
// by 8 threads a group (depth_hierarchy.cpp, kReduceGroupWidth and
// kReduceGroupHeight), each thread one 32-bit word of the level: two
// adjacent texels, low half first.
//
// Registry entry (src/engine/rendering/shaders/permutation.cpp,
// DepthHierarchyReduce): compute, entry main, per-draw block
// DepthHierarchyReduce, no buffer block.
//
// Texel (x, y) of a level is the largest of the four texels (2x, 2y) to
// (2x + 1, 2y + 1) below it, each coordinate clamped to the source's last
// column and row, stored as the code of the 16-bit float nearest it
// (bw/scene/half_depth.hlsli), the same bytes on every target. Level 0
// reads the depth texture through a sampled load; a later level reads the
// level below from the scratch buffer through raw storage loads, never a
// typed one. A level's rows are destination_pitch bytes from
// destination_offset (depth_hierarchy.cpp places them for the copy into
// the texture). Each dispatch builds one level (compute_shaders.hpp says
// why).
#include "bw/bindless.hlsli"
#include "bw/generated/depth_hierarchy_reduce.hlsli"
#include "bw/scene/half_depth.hlsli"

float bw_source_texel(uint x, uint y) {
  const uint cx = min(x, DepthHierarchyReduce.source_width - 1u);
  const uint cy = min(y, DepthHierarchyReduce.source_height - 1u);
  if (DepthHierarchyReduce.level == 0u) {
    return BW_TEXTURE2D(DepthHierarchyReduce.depth).Load(int3(int(cx), int(cy), 0)).r;
  }
  const uint word = BW_STORAGE_BUFFER(DepthHierarchyReduce.levels)
                        .Load(DepthHierarchyReduce.source_offset +
                              cy * DepthHierarchyReduce.source_pitch + (cx >> 1) * 4u);
  return f16tof32((cx & 1u) != 0u ? word >> 16 : word & 0xFFFFu);
}

float bw_reduced_texel(uint x, uint y) {
  const float a = bw_source_texel(2u * x, 2u * y);
  const float b = bw_source_texel(2u * x + 1u, 2u * y);
  const float c = bw_source_texel(2u * x, 2u * y + 1u);
  const float d = bw_source_texel(2u * x + 1u, 2u * y + 1u);
  return max(max(a, b), max(c, d));
}

[numthreads(8, 8, 1)] void main(uint3 thread : SV_DispatchThreadID) {
  const uint first = thread.x * 2u;
  const uint y = thread.y;
  if (y >= DepthHierarchyReduce.destination_height ||
      first >= DepthHierarchyReduce.destination_width) {
    return;
  }
  const uint low = bw_depth_to_half(bw_reduced_texel(first, y));
  const uint high = first + 1u < DepthHierarchyReduce.destination_width
                        ? bw_depth_to_half(bw_reduced_texel(first + 1u, y))
                        : 0u;
  BW_STORAGE_BUFFER(DepthHierarchyReduce.levels)
      .Store(DepthHierarchyReduce.destination_offset + y * DepthHierarchyReduce.destination_pitch +
                 thread.x * 4u,
             low | (high << 16));
}
