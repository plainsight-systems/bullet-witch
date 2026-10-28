// Shared row-vector XYZ Euler transforms and their analytical inverse. Inputs
// are radians and translation; output applies X, Y, Z then translation. Inverse
// applies negative translation, -Z, -Y, -X. Caller validates finite inputs.
// Preserve scalar trig rounded to T before shared matrix multiplication; no
// normalization, allocation or synchronization. Core I.1/SL.con.3 expose order
// and bounded arrays; MEM.9 fixed storage. Planar rotation follows both-phase
// Core F.4 (#rf-constexpr): cached sine/cosine values support pure constant
// component arithmetic; trig evaluation remains runtime and rounded to T.
// Both phases queried for the spatial light consumer and common rotation.
// No renderer/game-specific coordinate convention is hidden.
// ZXY rotation additionally exposes a pure 3x3 row-vector basis, applying Z,
// then X, then Y. Trigonometry rounds to T before the expanded products;
// its ordering matches the reconstructed particle rotation basis. Core I.11
// (#ri-raw) borrows angles; SL.con.3 (#rsl-bounds) uses fixed arrays.
// Performance MEM.9 (guidelines/memory/MEM.9-embedded-allocate-at-init.md)
// keeps this shared helper allocation-free; both phases consulted.
#pragma once
#include <cmath>

#include "matrix.hpp"
namespace bw::math {
// Cached planar basis. Positive radians rotate (1,0) toward (0,1); callers
// select the physical plane and own finite-input/angle policy. Evaluate trig
// in double and round to T before products, matching original placement math.
template <std::floating_point T>
struct PlanarRotation {
  T cosine, sine;
};
template <std::floating_point T>
PlanarRotation<T> planar_rotation(T radians) {
  return {static_cast<T>(std::cos(static_cast<double>(radians))),
          static_cast<T>(std::sin(static_cast<double>(radians)))};
}
template <std::floating_point T>
constexpr Vector<T, 2> rotate_planar(const Vector<T, 2>& point,
                                     const PlanarRotation<T>& rotation) {
  return {rotation.cosine * point[0] - rotation.sine * point[1],
          rotation.sine * point[0] + rotation.cosine * point[1]};
}
// Explicit row-vector axis order; translation and homogeneous lanes belong to
// the caller's affine transform. Do not transpose this for column notation.
template <std::floating_point T>
Matrix<T, 3, 3> euler_zxy_rotation(const Vector3<T>& radians) {
  const auto x = planar_rotation(radians[0]);
  const auto y = planar_rotation(radians[1]);
  const auto z = planar_rotation(radians[2]);
  return {{{x.sine * z.sine * y.sine + y.cosine * z.cosine, x.cosine * z.sine,
            x.sine * z.sine * y.cosine - y.sine * z.cosine},
           {y.sine * x.sine * z.cosine - y.cosine * z.sine, x.cosine * z.cosine,
            x.sine * z.cosine * y.cosine + y.sine * z.sine},
           {y.sine * x.cosine, -x.sine, y.cosine * x.cosine}}};
}
template <std::floating_point T>
struct EulerTransform {
  Matrix4<T> forward, inverse;
};
template <std::floating_point T>
struct EulerPlacement {
  Vector3<T> radians, translation;
};
template <std::floating_point T>
EulerTransform<T> euler_xyz_transform(const EulerPlacement<T>& placement) {
  const auto& radians = placement.radians;
  const auto& translation = placement.translation;
  const auto sine = [](T angle) {
    return static_cast<T>(std::sin(static_cast<double>(angle)));
  };
  const auto cosine = [](T angle) {
    return static_cast<T>(std::cos(static_cast<double>(angle)));
  };
  const auto cx = cosine(radians[0]), sx = sine(radians[0]);
  const auto cy = cosine(radians[1]), sy = sine(radians[1]);
  const auto cz = cosine(radians[2]), sz = sine(radians[2]);
  const Matrix4<T> x{
      {{1, 0, 0, 0}, {0, cx, sx, 0}, {0, -sx, cx, 0}, {0, 0, 0, 1}}};
  const Matrix4<T> y{
      {{cy, 0, -sy, 0}, {0, 1, 0, 0}, {sy, 0, cy, 0}, {0, 0, 0, 1}}};
  const Matrix4<T> z{
      {{cz, sz, 0, 0}, {-sz, cz, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1}}};
  auto forward = multiply(multiply(x, y), z);
  forward[3] = {translation[0], translation[1], translation[2], 1};
  const Matrix4<T> t{{{1, 0, 0, 0},
                      {0, 1, 0, 0},
                      {0, 0, 1, 0},
                      {-translation[0], -translation[1], -translation[2], 1}}};
  return {forward, multiply(multiply(multiply(t, transpose(z)), transpose(y)),
                            transpose(x))};
}
}  // namespace bw::math
