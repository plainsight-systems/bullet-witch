// The RHI's indirect-work fixture vertex shader: four quads side by side,
// each 16 by 64 pixels of a 64 by 64 target, generated from the index value
// with no vertex buffer, so a draw's index range chooses its quads. The RHI
// integration test draws them through the indirect draws (command_list.hpp,
// Indirect work) and reads back which quads were covered.
//
// Registry entry (src/engine/rendering/shaders/permutation.cpp,
// IndirectQuads): name indirect_quads, entry point main, vertex stage, an
// empty permutation domain, no static block, no per-draw block. It reads no
// descriptor array and no block; it reads the draw's index (BW_DRAW_INDEX).
//
// Vertex v is corner v % 4 of quad v / 4: corner bit 0 selects the quad's
// right edge, bit 1 its top edge. COLOR0 is float4((index + 1) / 4, 1, 0, 1)
// with the draw's index, so each quad's texels name the draw that covered
// it. The output matches vertex_color.hlsl's
// input (SV_Position, COLOR0), the pixel shader it is paired with.
#include "bw/bindless.hlsli"
#include "bw/generated/indirect_quads.hlsli"

struct IndirectQuadsOutput {
  float4 position : SV_Position;
  float4 color : COLOR0;
};

IndirectQuadsOutput main(uint vertex_id : SV_VertexID) {
  const uint quad = vertex_id >> 2;
  const uint corner = vertex_id & 3;
  const float left = float(quad) * 0.5 - 1.0;

  IndirectQuadsOutput output;
  output.position =
      float4(left + float(corner & 1) * 0.5, (corner & 2) != 0 ? 1.0 : -1.0, 0.0, 1.0);
  output.color = float4(float(BW_DRAW_INDEX + 1) * 0.25, 1.0, 0.0, 1.0);
  return output;
}
