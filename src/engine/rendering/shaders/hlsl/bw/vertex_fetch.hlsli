// Vertex pulling: the decode functions a vertex shader uses to read its
// attributes from the stream buffers by vertex index, with no input layout
// (docs/architecture/shaders.md, invariant 5; the C++ side is
// bw/rendering/geometry/vertex_streams.hpp, whose VertexStreams block
// carries the stream indices, strides, counts, base vertex and attribute
// table). Include bw/bindless.hlsli first: the loads go through BW_BUFFER.
//
// A shader reads vertex `vertex_id + streams.base_vertex`: every draw passes
// base vertex 0 to the API and carries the draw's base in the block, so the
// same code reads the same vertex on every target. It builds a
// BwVertexStream per stream from the block and decodes each attribute with
// the function of its element type, at the offset the attribute table gives.
// Offsets and strides are multiples of 4, the alignment a raw buffer load
// requires on every target; no function here checks bounds, the draw's
// range was validated before it was recorded.
//
// Element decode, one function per type:
//   float3     three floats as stored;
//   short4n    four 16-bit signed integers, each max(-1, c / 32767);
//   half2      two 16-bit floats, low half first;
//   ubyte4     four bytes as integers, low byte first;
//   ubyte4n    four bytes, each c / 255.
// The attribute slot numbers equal bw::geometry::VertexAttribute; a slot the
// layout does not name reads BW_ATTRIBUTE_ABSENT.
//
// The short4n and ubyte4n conversions are the standard snorm 16 and unorm 8
// conversions, written out here in HLSL arithmetic on every target;
// f16tof32 reads the float16 in the low half of a uint.
// The content's vertices are byte-aligned fixed formats, so each attribute
// is one aligned load and its format is the attribute table's; indices stay
// in hardware index buffers.
#ifndef BW_VERTEX_FETCH_HLSLI
#define BW_VERTEX_FETCH_HLSLI

#define BW_ATTRIBUTE_POSITION 0
#define BW_ATTRIBUTE_NORMAL 1
#define BW_ATTRIBUTE_TANGENT 2
#define BW_ATTRIBUTE_COLOR0 3
#define BW_ATTRIBUTE_COLOR1 4
#define BW_ATTRIBUTE_COLOR2 5
#define BW_ATTRIBUTE_TEXCOORD1 6
#define BW_ATTRIBUTE_TEXCOORD2 7
#define BW_ATTRIBUTE_TEXCOORD3 8
#define BW_ATTRIBUTE_TEXCOORD4 9
#define BW_ATTRIBUTE_BONE_SLOTS 10
#define BW_ATTRIBUTE_BONE_WEIGHTS 11
#define BW_ATTRIBUTE_ABSENT 0xFFFFFFFFu

// The attribute word of slot `slot` in a VertexStreams block value: the
// byte offset in the low 16 bits, the stream in the high 16.
#define BW_ATTRIBUTE_WORD(streams, slot) ((streams).attributes[(slot) >> 2][(slot) & 3])

uint bw_attribute_stream(uint word) { return word >> 16; }
uint bw_attribute_offset(uint word) { return word & 0xFFFFu; }

// One stream: the buffer's descriptor index and its byte stride.
struct BwVertexStream {
  uint buffer;
  uint stride;
};

// Stream i of a VertexStreams block value (i is 0 or 1).
#define BW_VERTEX_STREAM(streams, i)                                                               \
  bw_make_vertex_stream((i) == 0 ? (streams).buffer0 : (streams).buffer1,                         \
                        (i) == 0 ? (streams).stride0 : (streams).stride1)

BwVertexStream bw_make_vertex_stream(uint buffer, uint stride) {
  BwVertexStream stream;
  stream.buffer = buffer;
  stream.stride = stride;
  return stream;
}

uint bw_vertex_address(BwVertexStream stream, uint vertex, uint offset) {
  return vertex * stream.stride + offset;
}

float3 bw_load_float3(BwVertexStream stream, uint vertex, uint offset) {
  return asfloat(BW_BUFFER(stream.buffer).Load3(bw_vertex_address(stream, vertex, offset)));
}

float4 bw_load_short4n(BwVertexStream stream, uint vertex, uint offset) {
  const uint2 words = BW_BUFFER(stream.buffer).Load2(bw_vertex_address(stream, vertex, offset));
  // Sign-extend each 16-bit lane: shift it to the top of an int, then
  // arithmetic shift down.
  const int4 raw = int4(int(words.x << 16), int(words.x), int(words.y << 16), int(words.y)) >> 16;
  return max(-1.0, float4(raw) / 32767.0);
}

float2 bw_load_half2(BwVertexStream stream, uint vertex, uint offset) {
  const uint word = BW_BUFFER(stream.buffer).Load(bw_vertex_address(stream, vertex, offset));
  return float2(f16tof32(word & 0xFFFFu), f16tof32(word >> 16));
}

uint4 bw_load_ubyte4(BwVertexStream stream, uint vertex, uint offset) {
  const uint word = BW_BUFFER(stream.buffer).Load(bw_vertex_address(stream, vertex, offset));
  return uint4(word & 0xFFu, (word >> 8) & 0xFFu, (word >> 16) & 0xFFu, word >> 24);
}

float4 bw_load_ubyte4n(BwVertexStream stream, uint vertex, uint offset) {
  return float4(bw_load_ubyte4(stream, vertex, offset)) / 255.0;
}

#endif
