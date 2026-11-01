// Unit test: public contracts of bw/math/rotation/quaternion_register.hpp.
#include "bw/math/rotation/quaternion_register.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <stdexcept>
#include <type_traits>

namespace {
namespace simd = bw::math::simd;

void require(bool condition, const char* message) {
  if (!condition) {
    throw std::runtime_error{message};
  }
}

template <typename T> using Register = decltype(simd::broadcast(T{}));

template <typename T> Register<T> value(std::array<T, 4> components) {
  return simd::load(components.data());
}

template <typename T> std::array<T, 4> lanes(Register<T> input) {
  std::array<T, 4> result{};
  simd::store(input, result.data());
  return result;
}

template <typename T>
void exact(Register<T> actual, std::array<T, 4> expected, const char* message) {
  require(lanes<T>(actual) == expected, message);
}

template <typename T>
void close(Register<T> actual, std::array<T, 4> expected, const char* message) {
  constexpr double tolerance = std::is_same_v<T, float> ? 2.e-6 : 2.e-9;
  const auto components = lanes<T>(actual);
  for (std::size_t i = 0; i < components.size(); ++i) {
    require(std::abs(static_cast<double>(components[i] - expected[i])) <= tolerance, message);
  }
}

template <typename T> void products() {
  const auto identity = value<T>({T{0}, T{0}, T{0}, T{1}});
  const auto a = value<T>({T{1}, T{2}, T{3}, T{4}});
  const auto b = value<T>({T{5}, T{6}, T{7}, T{8}});
  exact<T>(simd::quaternion_multiply(a, b), {T{24}, T{48}, T{48}, T{-6}},
           "non-unit Hamilton product has no normalization");
  if constexpr (std::is_same_v<T, double>) {
    const double fine = 1.0 + std::ldexp(1.0, -40);
    exact<T>(simd::quaternion_multiply(value<T>({fine, 0., 0., 1.}), identity), {fine, 0., 0., 1.},
             "double product keeps double precision");
  }
}

template <typename T> void rotations() {
  const T half_root = static_cast<T>(std::sqrt(0.5));
  const auto z_turn = value<T>({T{0}, T{0}, half_root, half_root});
  const auto x_turn = value<T>({half_root, T{0}, T{0}, half_root});
  const auto x = value<T>({T{1}, T{0}, T{0}, T{0}});
  const auto y = value<T>({T{0}, T{1}, T{0}, T{0}});
  close<T>(simd::quaternion_rotate(z_turn, x), {T{0}, T{1}, T{0}, T{0}},
           "Z quarter-turn maps X to Y");
  close<T>(simd::quaternion_inverse_rotate(z_turn, y), {T{1}, T{0}, T{0}, T{0}},
           "inverse Z quarter-turn maps Y to X");
  close<T>(simd::quaternion_rotate(simd::quaternion_multiply(x_turn, z_turn), x),
           {T{0}, T{0}, T{1}, T{0}}, "A*B applies B then A");
  close<T>(simd::quaternion_multiply(z_turn, simd::quaternion_inverse(z_turn)),
           {T{0}, T{0}, T{0}, T{1}}, "unit inverse product");
}

template <typename T> void normalization() {
  const auto fallback = value<T>({T{7}, T{8}, T{9}, T{10}});
  exact<T>(simd::normalize_safe(simd::broadcast(T{0}), fallback), {T{7}, T{8}, T{9}, T{10}},
           "zero uses caller fallback");
  exact<T>(simd::normalize_quaternion(simd::broadcast(T{0})), {T{0}, T{0}, T{0}, T{1}},
           "zero quaternion uses identity fallback");
  const auto tiny = value<T>({static_cast<T>(0.00005), T{0}, T{0}, T{0}});
  exact<T>(simd::normalize_safe(tiny, fallback), {T{7}, T{8}, T{9}, T{10}},
           "below squared-length threshold");
  close<T>(simd::normalize_safe(value<T>({static_cast<T>(0.0002), T{0}, T{0}, T{0}}), fallback),
           {T{1}, T{0}, T{0}, T{0}}, "above squared-length threshold");
  close<T>(simd::normalize_safe(value<T>({T{0}, T{0}, T{3}, T{4}}), fallback),
           {T{0}, T{0}, static_cast<T>(0.6), static_cast<T>(0.8)}, "four-lane normalization");
  close<T>(simd::normalize_quaternion(value<T>({T{0}, T{0}, T{0}, T{-2}})),
           {T{0}, T{0}, T{0}, T{-1}}, "normalization does not force positive W");
}
} // namespace

int main() {
  try {
    products<float>();
    products<double>();
    rotations<float>();
    rotations<double>();
    normalization<float>();
    normalization<double>();
    std::cout << "quaternion register contracts passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
