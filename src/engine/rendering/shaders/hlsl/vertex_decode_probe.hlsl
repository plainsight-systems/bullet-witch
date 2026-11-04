// The presentation slice's decode probe: a vertex shader that writes the
// decoded value of one attribute of one vertex into one pixel, so the RHI
// integration test can read every element type back through a float target
// and compare it with the bytes it uploaded.
//
// Registry entry (src/engine/rendering/shaders/permutation.cpp,
// VertexDecodeProbe): name vertex_decode_probe, entry point main, vertex
// stage, one dimension BW_DECODE_TYPE (EnumDimension over
// bw::geometry::VertexElementType: 0 float3, 1 short4n, 2 half2, 3 ubyte4,
// 4 ubyte4n), no static block, per-draw block VertexDecodeProbeDraw
// (pulled_mesh_fixture.hpp: the VertexStreams block, the attribute slot to
// decode, the target width, two reserved words).
//
// Geometry (pulled_mesh_fixture.hpp, VertexDecodeProbeDraw): the draw is
// 6 * width vertices as a triangle list with first vertex 0. Vertex id v
// belongs to probe p = v / 6 and corner v % 6; the six corners are two
// triangles covering the clip-space rectangle x in [p / width, (p + 1) /
// width] mapped to [-1, 1], y over the full [-1, 1], which is exactly pixel
// column p of a one-row target. Every corner of probe p writes the same
// COLOR0: the decoded value of vertex p + base_vertex for the slot
// `attribute`, as float4: a float3 in xyz with w 0, a short4n as decoded, a
// half2 in xy with zw 0, a ubyte4 as float(uint4), a ubyte4n as decoded.
// The attribute word gives the stream and the byte offset; the element type
// is the permutation's.
//
// Reflection on every target: the draw block and the ByteAddressBuffer
// array; no texture or sampler array.
#include "bw/bindless.hlsli"
#include "bw/generated/vertex_decode_probe.hlsli"
#include "bw/vertex_fetch.hlsli"

#if !defined(BW_DECODE_TYPE) || BW_DECODE_TYPE < 0 || BW_DECODE_TYPE > 4
#error "BW_DECODE_TYPE must be a bw::geometry::VertexElementType value, 0 to 4"
#endif

struct VertexDecodeProbeOutput {
  float4 position : SV_Position;
  float4 color : COLOR0;
};

// The corners of the two triangles of a probe, in units of its pixel column
// (x) and the target's height (y).
static const float2 kProbeCorners[6] = {float2(0.0, 0.0), float2(1.0, 0.0), float2(1.0, 1.0),
                                        float2(0.0, 0.0), float2(1.0, 1.0), float2(0.0, 1.0)};

float4 vertex_decode_probe_value(BwVertexStream stream, uint vertex, uint offset) {
#if BW_DECODE_TYPE == 0
  return float4(bw_load_float3(stream, vertex, offset), 0.0);
#elif BW_DECODE_TYPE == 1
  return bw_load_short4n(stream, vertex, offset);
#elif BW_DECODE_TYPE == 2
  return float4(bw_load_half2(stream, vertex, offset), 0.0, 0.0);
#elif BW_DECODE_TYPE == 3
  return float4(bw_load_ubyte4(stream, vertex, offset));
#else
  return bw_load_ubyte4n(stream, vertex, offset);
#endif
}

VertexDecodeProbeOutput main(uint vertex_id : SV_VertexID) {
  const BW_VertexStreams streams = VertexDecodeProbeDraw.streams;
  const uint probe = vertex_id / 6;
  const float2 corner = kProbeCorners[vertex_id % 6];
  const float width = float(VertexDecodeProbeDraw.width);

  const uint word = BW_ATTRIBUTE_WORD(streams, VertexDecodeProbeDraw.attribute);
  const BwVertexStream stream = BW_VERTEX_STREAM(streams, bw_attribute_stream(word));

  VertexDecodeProbeOutput output;
  output.position =
      float4((float(probe) + corner.x) / width * 2.0 - 1.0, corner.y * 2.0 - 1.0, 0.0, 1.0);
  output.color =
      vertex_decode_probe_value(stream, probe + streams.base_vertex, bw_attribute_offset(word));
  return output;
}
