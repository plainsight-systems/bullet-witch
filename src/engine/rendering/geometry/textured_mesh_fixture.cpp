#include "bw/rendering/geometry/textured_mesh_fixture.hpp"

#include "bw/rendering/geometry/vertex_streams.hpp"

#include <cstddef>

// Responsibility: the out-of-line part of
// bw/rendering/geometry/textured_mesh_fixture.hpp.
namespace bw::geometry {

bool textured_pair_reads(const VertexStreamLayout& layout) noexcept {
  bool position = false;
  bool normal = false;
  bool uv = false;
  for (std::size_t i = 0; i < layout.attribute_count; ++i) {
    const VertexAttributeDescription& a = layout.attributes[i];
    if (a.attribute == VertexAttribute::Position) {
      position = a.type == VertexElementType::Float3;
    } else if (a.attribute == VertexAttribute::Normal) {
      normal = a.type == VertexElementType::Short4Norm;
    } else if (a.attribute == VertexAttribute::TexCoord1) {
      uv = a.type == VertexElementType::Half2;
    }
  }
  return position && normal && uv;
}

} // namespace bw::geometry
