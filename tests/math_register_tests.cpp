#include "core/math/register.hpp"

#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <type_traits>

namespace {
namespace simd = bw::math::simd;
static_assert(sizeof(simd::Register4f) == 16 && alignof(simd::Register4f) == 16);
static_assert(sizeof(simd::Register4d) == 32 && alignof(simd::Register4d) == 16);
static_assert(std::is_same_v<simd::Register, simd::Register4d>);
static_assert(std::is_trivially_copyable_v<simd::Register4f>);
static_assert(std::is_trivially_copyable_v<simd::Register4d>);

void require(bool value, const char* message) {
  if (!value) {
    throw std::runtime_error{message};
  }
}

template <typename T> using Register = decltype(simd::broadcast(T{}));
template <typename T>
using Bits = std::conditional_t<std::is_same_v<T, float>, std::uint32_t, std::uint64_t>;

template <typename T> std::array<T, 4> lanes(Register<T> value) {
  std::array<T, 4> result{};
  simd::store(value, result.data());
  return result;
}

template <typename T>
void exact(Register<T> value, std::array<T, 4> expected, const char* message) {
  require(lanes<T>(value) == expected, message);
}

template <typename T> void arithmetic_and_storage() {
  alignas(16) std::array<T, 4> a{T{1}, T{2}, T{4}, T{8}};
  alignas(16) std::array<T, 4> b{T{2}, T{4}, T{8}, T{16}};
  const auto av = simd::load_aligned(a.data());
  const auto bv = simd::load(b.data());
  alignas(16) std::array<T, 4> stored{};
  simd::store_aligned(av, stored.data());
  require(stored == a, "aligned XYZW lane order");
  // Offset is element-aligned, deliberately not register-aligned. Guards detect
  // stores outside the four-lane contract, without undefined misaligned T access.
  alignas(16) std::array<T, 6> offset{T{99}, T{3}, T{5}, T{7}, T{11}, T{101}};
  exact<T>(simd::load(offset.data() + 1), {T{3}, T{5}, T{7}, T{11}}, "unaligned load");
  simd::store(av, offset.data() + 1);
  require(offset == std::array<T, 6>{T{99}, T{1}, T{2}, T{4}, T{8}, T{101}}, "store extent");
  exact<T>(simd::broadcast(T{7}), {T{7}, T{7}, T{7}, T{7}}, "broadcast");
  exact<T>(simd::add(av, bv), {T{3}, T{6}, T{12}, T{24}}, "add");
  exact<T>(simd::subtract(bv, av), a, "subtract");
  exact<T>(simd::multiply(av, bv), {T{2}, T{8}, T{32}, T{128}}, "multiply");
  exact<T>(simd::divide(bv, av), {T{2}, T{2}, T{2}, T{2}}, "divide");
  // Exact cancellation distinguishes hardware fused operations from rounded
  // multiplication followed by addition; no tolerance can conceal a zero result.
  const int exponent = std::is_same_v<T, float> ? -13 : -27;
  const T delta = std::ldexp(T{1}, exponent);
  const auto high = simd::broadcast(T{1} + delta);
  const auto low = simd::broadcast(T{1} - delta);
  const T residual = -(delta * delta);
  const auto fused = lanes<T>(simd::multiply_add(high, low, simd::broadcast(T{-1})));
  const auto negated = lanes<T>(simd::negate_multiply_add(high, low, simd::broadcast(T{1})));
  for (std::size_t i = 0; i < 4; ++i) {
    require(fused[i] == residual && negated[i] == -residual, "fused rounding");
  }
}

template <typename T> void masks_and_domains() {
  const T nan = std::numeric_limits<T>::quiet_NaN();
  const T inf = std::numeric_limits<T>::infinity();
  const std::array<T, 4> values{T{0}, -T{0}, nan, inf};
  const auto a = simd::load(values.data());
  const auto equal = lanes<T>(simd::compare_equal(a, a));
  const auto greater = lanes<T>(simd::compare_greater(a, simd::broadcast(T{0})));
  const Bits<T> all = std::numeric_limits<Bits<T>>::max();
  require(std::bit_cast<Bits<T>>(equal[0]) == all && std::bit_cast<Bits<T>>(equal[1]) == all &&
              std::bit_cast<Bits<T>>(equal[2]) == 0 && std::bit_cast<Bits<T>>(equal[3]) == all,
          "equality mask bits, signed zero and unordered NaN");
  require(std::bit_cast<Bits<T>>(greater[0]) == 0 && std::bit_cast<Bits<T>>(greater[1]) == 0 &&
              std::bit_cast<Bits<T>>(greater[2]) == 0 && std::bit_cast<Bits<T>>(greater[3]) == all,
          "ordered greater mask bits");
  exact<T>(simd::select(simd::compare_equal(a, a), simd::broadcast(T{3}), simd::broadcast(T{5})),
           {T{3}, T{3}, T{5}, T{3}}, "full-mask select");
  // A sign-bit-only mask must select individual bits, not an entire truthy lane.
  const auto partial = simd::broadcast(-T{0});
  exact<T>(simd::select(partial, simd::broadcast(T{-2}), simd::broadcast(T{3})),
           {T{-3}, T{-3}, T{-3}, T{-3}}, "partial-bit select");
  const auto root = lanes<T>(simd::square_root(a));
  require(root[0] == T{0} && !std::signbit(root[0]) && root[1] == T{0} && std::signbit(root[1]) &&
              std::isnan(root[2]) && root[3] == inf,
          "sqrt special values");
  require(std::isnan(lanes<T>(simd::square_root(simd::broadcast(T{-1})))[0]), "negative sqrt");
  const auto reciprocal_zero = lanes<T>(simd::reciprocal(simd::broadcast(T{0})));
  if constexpr (std::is_same_v<T, float>) {
    require(std::isnan(reciprocal_zero[0]), "float polynomial reciprocal zero is invalid");
  } else {
    require(reciprocal_zero[0] == inf, "double direct reciprocal zero is infinity");
  }
  const auto inverse_root = lanes<T>(simd::reciprocal_square_root(a));
  require(inverse_root[0] == inf && inverse_root[1] == -inf && std::isnan(inverse_root[2]) &&
              inverse_root[3] == T{0},
          "rsqrt instruction special values");
}

template <typename T> void approximation() {
  const std::array<T, 4> input{T{3}, T{7}, T{11}, T{19}};
  const auto value = simd::load(input.data());
  const auto reciprocal_estimate = lanes<T>(simd::reciprocal_estimate(value));
  const auto reciprocal = lanes<T>(simd::reciprocal(value));
  const auto root_estimate = lanes<T>(simd::reciprocal_square_root_estimate(value));
  const auto root = lanes<T>(simd::reciprocal_square_root(value));
  // Inspected UnrealMathTest.cpp:2029–2046,2877–2894 gives these residual
  // thresholds. These are test acceptance bounds, not new runtime tolerances.
  // Fixtures/oracles are independent scalar identities, not the NEON algorithm.
  for (std::size_t i = 0; i < input.size(); ++i) {
    const T expected = T{1} / input[i];
    require(std::abs(reciprocal_estimate[i] * input[i] - T{1}) <= static_cast<T>(0.008),
            "reciprocal estimate residual");
    require(std::abs(reciprocal[i] * input[i] - T{1}) <= static_cast<T>(1.e-7),
            "reciprocal residual");
    require(std::abs(root_estimate[i] * root_estimate[i] * input[i] - T{1}) <=
                static_cast<T>(0.007),
            "rsqrt estimate residual");
    require(std::abs(root[i] * root[i] * input[i] - T{1}) <= static_cast<T>(1.e-6),
            "rsqrt refined residual");
    require(std::abs(reciprocal[i] - expected) <= std::abs(reciprocal_estimate[i] - expected),
            "reciprocal refinement improves estimate");
    if constexpr (std::is_same_v<T, double>) {
      require(reciprocal[i] == expected, "double reciprocal uses direct divide");
    }
  }
}

void conversion() {
  const std::array<double, 4> input{16777217.0, -16777217.0, 0.5, -0.0};
  const auto d = simd::load(input.data());
  exact<double>(d, input, "double retains precision");
  const auto f = simd::narrow(d);
  exact<float>(f, {16777216.0F, -16777216.0F, 0.5F, -0.0F}, "explicit narrowing");
  const auto widened = lanes<double>(simd::widen(f));
  require(widened[0] == 16777216.0 && widened[1] == -16777216.0 && widened[2] == 0.5 &&
              std::signbit(widened[3]),
          "paired widening lane order and sign");
}
} // namespace

int main() {
  try {
    arithmetic_and_storage<float>();
    arithmetic_and_storage<double>();
    masks_and_domains<float>();
    masks_and_domains<double>();
    approximation<float>();
    approximation<double>();
    conversion();
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
