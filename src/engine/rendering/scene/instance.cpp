#include "bw/rendering/scene/instance.hpp"

#include "bw/math/matrix/matrix4.hpp"
#include "bw/math/vector/vector3.hpp"

#include <algorithm>
#include <array>
#include <cstddef>

// Responsibility: the out-of-line part of bw/rendering/scene/instance.hpp:
// world_box, contract 3.
namespace bw::scene {

Box world_box(const Box& local, const math::Matrix4f& transform, float pad) noexcept {
  if (local.min.x > local.max.x || local.min.y > local.max.y || local.min.z > local.max.z) {
    return local;
  }
  const std::array<float, 2> xs{local.min.x, local.max.x};
  const std::array<float, 2> ys{local.min.y, local.max.y};
  const std::array<float, 2> zs{local.min.z, local.max.z};
  Box out{};
  bool first = true;
  for (const float x : xs) {
    for (const float y : ys) {
      for (const float z : zs) {
        // Row vectors: p * M, translation in row 3 (matrix4.hpp).
        std::array<float, 3> p{};
        for (std::size_t c = 0; c < 3U; ++c) {
          p[c] = (x * transform.m[0][c]) + (y * transform.m[1][c]) + (z * transform.m[2][c]) +
                 transform.m[3][c];
        }
        const math::Vector3f corner{p[0], p[1], p[2]};
        if (first) {
          out.min = corner;
          out.max = corner;
          first = false;
          continue;
        }
        out.min = math::Vector3f{std::min(out.min.x, corner.x), std::min(out.min.y, corner.y),
                                 std::min(out.min.z, corner.z)};
        out.max = math::Vector3f{std::max(out.max.x, corner.x), std::max(out.max.y, corner.y),
                                 std::max(out.max.z, corner.z)};
      }
    }
  }
  const math::Vector3f grow{pad, pad, pad};
  out.min = out.min - grow;
  out.max = out.max + grow;
  return out;
}

} // namespace bw::scene
