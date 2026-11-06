#ifndef BW_MATH_MATRIX_MATRIX4_INL
#define BW_MATH_MATRIX_MATRIX4_INL
// The tail of bw/math/matrix/matrix4.hpp. Opened alone it includes that header,
// which leaves this file out while the guard above is defined.
#include "bw/math/matrix/matrix4.hpp"

#include "bw/platform/math/math.hpp"

#include <cstddef>

// Definitions of bw/math/matrix/matrix4.hpp: scalar expressions in a fixed
// order, visible to every consumer.
namespace bw::math {

template <typename T> inline Matrix4<T>::Matrix4(const Rows& rows) : m{rows} {}

// Row r of the product is row r of this matrix times `other`.
template <typename T> inline Matrix4<T> Matrix4<T>::operator*(const Matrix4& other) const {
  // Every element is written by the loop below.
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-member-init)
  Matrix4 out;
  for (std::size_t r = 0; r < 4; ++r) {
    for (std::size_t c = 0; c < 4; ++c) {
      out.m[r][c] = m[r][0] * other.m[0][c] + m[r][1] * other.m[1][c] + m[r][2] * other.m[2][c] +
                    m[r][3] * other.m[3][c];
    }
  }
  return out;
}

template <typename T> inline Matrix4<T> Matrix4<T>::translation(const Vector3<T>& translation) {
  return Matrix4{Rows{{{T{1}, T{0}, T{0}, T{0}},
                       {T{0}, T{1}, T{0}, T{0}},
                       {T{0}, T{0}, T{1}, T{0}},
                       {translation.x, translation.y, translation.z, T{1}}}}};
}

// The rotation's three rows are the images of the x, y and z axes.
template <typename T>
inline Matrix4<T> Matrix4<T>::rotation_translation(const Quaternion4<T>& rotation,
                                                   const Vector3<T>& translation) {
  const T x2 = rotation.x + rotation.x;
  const T y2 = rotation.y + rotation.y;
  const T z2 = rotation.z + rotation.z;
  const T xx = rotation.x * x2;
  const T xy = rotation.x * y2;
  const T xz = rotation.x * z2;
  const T yy = rotation.y * y2;
  const T yz = rotation.y * z2;
  const T zz = rotation.z * z2;
  const T wx = rotation.w * x2;
  const T wy = rotation.w * y2;
  const T wz = rotation.w * z2;
  return Matrix4{Rows{{{T{1} - (yy + zz), xy + wz, xz - wy, T{0}},
                       {xy - wz, T{1} - (xx + zz), yz + wx, T{0}},
                       {xz + wy, yz - wx, T{1} - (xx + yy), T{0}},
                       {translation.x, translation.y, translation.z, T{1}}}}};
}

// The view's axes are the matrix's columns; the last row moves the eye to
// the origin.
// Three vectors in the order the header documents.
template <typename T>
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
inline Matrix4<T> Matrix4<T>::look_at(const Vector3<T>& eye, const Vector3<T>& target,
                                      const Vector3<T>& up) {
  const Vector3<T> z = (eye - target).safe_normal();
  const Vector3<T> x = up.cross(z).safe_normal();
  const Vector3<T> y = z.cross(x);
  return Matrix4{Rows{{{x.x, y.x, z.x, T{0}},
                       {x.y, y.y, z.y, T{0}},
                       {x.z, y.z, z.z, T{0}},
                       {-x.dot(eye), -y.dot(eye), -z.dot(eye), T{1}}}}};
}

// Same-typed parameters in the order the header documents.
template <typename T>
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
inline Matrix4<T> Matrix4<T>::perspective(T vertical_fov, T aspect, T near_plane, T far_plane) {
  const T scale = T{1} / platform::math::tangent(vertical_fov / T{2});
  const T depth = far_plane / (near_plane - far_plane);
  return Matrix4{Rows{{{scale / aspect, T{0}, T{0}, T{0}},
                       {T{0}, scale, T{0}, T{0}},
                       {T{0}, T{0}, depth, T{-1}},
                       {T{0}, T{0}, near_plane * depth, T{0}}}}};
}

} // namespace bw::math
#endif // BW_MATH_MATRIX_MATRIX4_INL
