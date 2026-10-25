// Portable scalar interpolation with explicit source operation order. Pure,
// allocation-free value arithmetic; caller chooses/clamps the finite factor.
// No extrapolation or validation policy is hidden. Core I.1 (#ri-explicit) and
// Performance MEM.9 (guidelines/memory/MEM.9-embedded-allocate-at-init.md) keep
// precision and fixed cost explicit; both phases consulted. Unlike std::lerp,
// this preserves a + factor*(b-a) ordering for recovered animation sampling.
#pragma once
#include <cmath>
#include <concepts>
namespace bw::math {
template <std::floating_point T>
constexpr T linear_interpolate(T first, T last, T factor) {
  return factor * (last - first) + first;
}
// Original scalar easing: quintic remapping followed by a cubic
// endpoint-tangent response. Caller supplies a clamped [0,1] factor; shape
// controls intentionally permit overshoot. Pure arithmetic, fixed storage.
template <std::floating_point T>
struct EasedTangentEndpoints {
  T first{}, last{}, start_shape{}, end_shape{};
};
// Quintic endpoint easing shared by original sprite and tangent curves.
template <std::floating_point T>
constexpr T quintic_ease(T factor) {
  constexpr T kFifth = T(6), kFourth = T(15), kThird = T(10);
  const T square = factor * factor;
  return (square * factor * square * kFifth - square * square * kFourth) +
         square * factor * kThird;
}
template <std::floating_point T>
constexpr T eased_tangent_interpolate(const EasedTangentEndpoints<T>& curve,
                                      T factor) {
  const T eased = quintic_ease(factor);
  const T eased_square = eased * eased;
  const T half_cube = eased_square * eased * T(.5);
  const T scaled_cube = eased_square * eased * T(1.5);
  const T response =
      ((eased_square - half_cube) - eased * T(.5)) *
          (curve.start_shape * T(1.5) + T(1)) +
      (eased_square * T(2) - scaled_cube) + eased * T(.5) +
      (half_cube - eased_square * T(.5)) * (-curve.end_shape * T(1.5));
  return response * (curve.last - curve.first) + curve.first;
}
// Source periodic response; the owner advances/wraps normalized phase.
template <std::floating_point T>
struct PeriodicSineEndpoints {
  T first{}, last{};
};
template <std::floating_point T>
T periodic_sine_interpolate(const PeriodicSineEndpoints<T>& endpoints,
                            T unit_phase) {
  constexpr T kTwoPi = T(6.28318530717958647692);
  const T angle = unit_phase * kTwoPi;
  const T sine = static_cast<T>(std::sin(static_cast<double>(angle)));
  const T factor = T(1) - (T(2) - (sine + T(1))) * T(.5);
  return endpoints.first + (endpoints.last - endpoints.first) * factor;
}
// Critically damped source UI response. The caller owns velocity and supplies
// finite nonnegative time with a positive duration. The coefficients preserve
// original damped-mode evaluation; no clamping/time authority is hidden.
template <std::floating_point T>
struct DampedResponseState {
  T value{}, velocity{};
};
template <std::floating_point T>
struct DampedResponseTarget {
  T value{}, duration{};
};
template <std::floating_point T>
constexpr DampedResponseState<T> damped_response(DampedResponseState<T> state,
                                                 DampedResponseTarget<T> target,
                                                 T seconds) {
  constexpr T kQuadratic = T(.48), kCubic = T(.235);
  const T omega = T(2) / target.duration;
  const T step = omega * seconds;
  const T decay = T(1) / (T(1) + step + kQuadratic * step * step +
                          kCubic * step * step * step);
  const T change = state.value - target.value;
  const T temporary = (state.velocity + omega * change) * seconds;
  return {target.value + (change + temporary) * decay,
          (state.velocity - omega * temporary) * decay};
}
}  // namespace bw::math
