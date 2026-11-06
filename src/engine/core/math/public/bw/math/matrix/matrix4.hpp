#ifndef BW_MATH_MATRIX_MATRIX4_HPP
#define BW_MATH_MATRIX_MATRIX4_HPP

#include "bw/math/rotation/quaternion.hpp"
#include "bw/math/vector/vector3.hpp"

#include <array>
#include <type_traits>

// Responsibility: the 4x4 transform matrix of the math library, in both
// precisions, and the transforms the runtime builds from it: translation,
// rotation with translation, a view from an eye and a target, and a
// perspective projection.
//
// Conventions.
//   Row vectors. A point p transforms as p * M; m[row][column]; translation
//   is row 3. a * b applies a, then b.
//   Storage is 16 contiguous T in row order, aligned to 16 bytes. The bytes
//   of Matrix4<float> are the bytes a shader's float4x4 reads
//   (bw/rendering/shaders/parameter_block.hpp, Float4x4: four rows).
//   View space is right-handed: +x right, +y up, the view looks down -z.
//   Clip space: x and y in [-w, w], depth 0 at the near plane and 1 at the
//   far plane, w = -z of view space.
//
// Contract.
//   1. operator* composes as stated above.
//   2. translation(t) moves a position by t and leaves a direction
//      unchanged.
//   3. rotation_translation(q, t) rotates as q.rotate does, then moves by
//      t. Precondition: q is normalized.
//   4. look_at(eye, target, up) maps eye to the view origin and target onto
//      the negative z axis at the distance |target - eye|; the view's x axis
//      is normalize(up x (eye - target)) and its y axis completes the
//      right-handed basis, so `up` lies in the view's y-z plane with
//      positive y. Preconditions: eye - target and up x (eye - target)
//      are each longer than Vector3::safe_normal's default tolerance (a
//      squared length of 1e-8), so eye and target are apart and up is
//      neither zero nor parallel to the view direction. With a precondition
//      broken that axis is safe_normal's zero fallback, the matrix is
//      singular, and the caller has a bug: nothing here repairs it.
//   5. perspective(vertical_fov, aspect, near, far) maps the view-space
//      frustum to clip space: a point on the view axis at distance near has
//      depth 0 after the divide by w, one at far has depth 1, and a point at
//      the top edge of the field of view has y == w. aspect is width over
//      height. Preconditions: 0 < vertical_fov < pi (radians), aspect > 0,
//      0 < near < far.
//
// A matrix has no non-finite diagnostic of its own: Vector3 and Quaternion4
// inputs report at their own construction, and perspective's scalars are
// not checked.
//
// Read guidance: F.20 (makers return values), C.49 (members initialized in
// constructors), GEN.6 (algorithms visible in the .inl), SIMD.1 (scalar
// expressions here; a register kernel goes behind the platform contract).
namespace bw::math {

template <typename T> struct alignas(16) Matrix4 {
  static_assert(std::is_same_v<T, float> || std::is_same_v<T, double>);
  using Rows = std::array<std::array<T, 4>, 4>;

  // m[row][column]. Ordinary default construction leaves it uninitialized;
  // do not read a default-initialized element.
  Rows m;

  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-member-init)
  Matrix4() = default;
  // The sixteen elements in row order.
  explicit Matrix4(const Rows& rows);

  [[nodiscard]] Matrix4 operator*(const Matrix4& other) const;

  [[nodiscard]] static Matrix4 translation(const Vector3<T>& translation);
  [[nodiscard]] static Matrix4 rotation_translation(const Quaternion4<T>& rotation,
                                                    const Vector3<T>& translation);
  // The view looks down -z, so the z axis is eye - target; x and y complete
  // the right-handed basis, each axis through safe_normal.
  [[nodiscard]] static Matrix4 look_at(const Vector3<T>& eye, const Vector3<T>& target,
                                       const Vector3<T>& up);
  // The right-handed form, w = -z, taking the full vertical angle and the
  // aspect.
  [[nodiscard]] static Matrix4 perspective(T vertical_fov, T aspect, T near_plane, T far_plane);
};

using Matrix4f = Matrix4<float>;
using Matrix4d = Matrix4<double>;
static_assert(sizeof(Matrix4f) == 64 && sizeof(Matrix4d) == 128);

} // namespace bw::math

#ifndef BW_MATH_MATRIX_MATRIX4_INL
#include "bw/math/matrix/matrix4.inl"
#endif

#endif // BW_MATH_MATRIX_MATRIX4_HPP
