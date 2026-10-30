#include "bw/math/rotation/quaternion_register.hpp"

#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
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
template <typename T>
using Bits = std::conditional_t<std::is_same_v<T, float>, std::uint32_t, std::uint64_t>;
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
  // Fixed before implementation. Native two-refinement reciprocal square root
  // does not promise full double mantissa precision; exact fixtures use exact().
  constexpr double tolerance = std::is_same_v<T, float> ? 2.e-6 : 2.e-9;
  const auto components = lanes<T>(actual);
  for (std::size_t i = 0; i < components.size(); ++i) {
    require(std::abs(static_cast<double>(components[i] - expected[i])) <= tolerance, message);
  }
}

template <typename T> void products() {
  const auto identity = value<T>({T{0}, T{0}, T{0}, T{1}});
  const auto i = value<T>({T{1}, T{0}, T{0}, T{0}});
  const auto j = value<T>({T{0}, T{1}, T{0}, T{0}});
  exact<T>(simd::quaternion_multiply(i, j), {T{0}, T{0}, T{1}, T{0}}, "i*j=k");
  exact<T>(simd::quaternion_multiply(j, i), {T{0}, T{0}, T{-1}, T{0}}, "j*i=-k");
  exact<T>(simd::quaternion_multiply(i, i), {T{0}, T{0}, T{0}, T{-1}}, "i squared=-1");
  const auto a = value<T>({T{1}, T{2}, T{3}, T{4}});
  const auto b = value<T>({T{5}, T{6}, T{7}, T{8}});
  exact<T>(simd::quaternion_multiply(a, b), {T{24}, T{48}, T{48}, T{-6}},
           "non-unit Hamilton product has no normalization");
  exact<T>(simd::quaternion_multiply(identity, a), {T{1}, T{2}, T{3}, T{4}}, "left identity");
  exact<T>(simd::quaternion_multiply(a, identity), {T{1}, T{2}, T{3}, T{4}}, "right identity");
  if constexpr (std::is_same_v<T, double>) {
    const double fine = 1.0 + std::ldexp(1.0, -40);
    exact<T>(simd::quaternion_multiply(value<T>({fine, 0., 0., 1.}), identity), {fine, 0., 0., 1.},
             "double product must not narrow through float");
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
  close<T>(simd::quaternion_rotate(simd::quaternion_multiply(z_turn, x_turn), x),
           {T{0}, T{1}, T{0}, T{0}}, "reverse composition has distinct result");
  const auto original = value<T>({T{2}, T{-3}, T{4}, T{0}});
  close<T>(simd::quaternion_inverse_rotate(z_turn, simd::quaternion_rotate(z_turn, original)),
           {T{2}, T{-3}, T{4}, T{0}}, "inverse rotation round trip");
  close<T>(simd::quaternion_multiply(z_turn, simd::quaternion_inverse(z_turn)),
           {T{0}, T{0}, T{0}, T{1}}, "unit inverse product");
  const auto signed_identity = value<T>({T{0}, -T{0}, T{0}, T{1}});
  const auto inverse = lanes<T>(simd::quaternion_inverse(signed_identity));
  require(std::signbit(inverse[0]) && !std::signbit(inverse[1]) && std::signbit(inverse[2]) &&
              inverse[3] == T{1},
          "inverse numeric sign multiplication preserves signed-zero semantics");
}

template <typename T> void normalization() {
  const auto fallback = value<T>({T{7}, T{8}, T{9}, T{10}});
  exact<T>(simd::normalize_safe(simd::broadcast(T{0}), fallback), {T{7}, T{8}, T{9}, T{10}},
           "zero uses caller fallback without normalization");
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
  // Independent representable boundary: double 1e-4 squared rounds to 1e-8;
  // float 1e-4f squared rounds below 1e-8f. Do not conflate these literals.
  const auto boundary = value<T>({static_cast<T>(0.0001), T{0}, T{0}, T{0}});
  if constexpr (std::is_same_v<T, float>) {
    exact<T>(simd::normalize_safe(boundary, fallback), {T{7}, T{8}, T{9}, T{10}},
             "float root candidate is below threshold");
  } else {
    close<T>(simd::normalize_safe(boundary, fallback), {T{1}, T{0}, T{0}, T{0}},
             "double threshold equality takes normalized branch");
  }
  if constexpr (std::is_same_v<T, double>) {
    // Squared length ~9.99999998e-9 lies between promoted 1e-8f and 1e-8.
    // A float-narrowed threshold would wrongly normalize this double input.
    exact<T>(simd::normalize_safe(value<T>({9.99999999e-5, 0., 0., 0.}), fallback),
             {7., 8., 9., 10.}, "double threshold retains double literal precision");
  }
  const T nan = std::numeric_limits<T>::quiet_NaN();
  const auto invalid = value<T>({nan, T{0}, T{0}, T{1}});
  const std::array<T, 4> fallback_bits{-T{0}, nan, T{-7}, T{2}};
  const auto preserved = lanes<T>(simd::normalize_safe(invalid, value<T>(fallback_bits)));
  for (std::size_t index = 0; index < preserved.size(); ++index) {
    require(std::bit_cast<Bits<T>>(preserved[index]) ==
                std::bit_cast<Bits<T>>(fallback_bits[index]),
            "NaN comparison selects every fallback bit unchanged");
  }
  exact<T>(simd::normalize_quaternion(invalid), {T{0}, T{0}, T{0}, T{1}},
           "NaN quaternion squared length selects identity");
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
    std::cout << "Quaternion register contracts passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
