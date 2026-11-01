// Unit test: public contracts of bw/math/rotation/quaternion.hpp.
#include "bw/math/rotation/quaternion.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <limits>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace {
using bw::math::Quaternion4;
using bw::math::Quaternion4d;
using bw::math::Quaternion4f;
namespace simd = bw::math::simd;

static_assert(std::is_same_v<bw::math::Quaternion, Quaternion4d>);
static_assert(sizeof(Quaternion4f) == 16 && sizeof(Quaternion4d) == 32);
static_assert(alignof(Quaternion4f) == 16 && alignof(Quaternion4d) == 16);
static_assert(std::is_standard_layout_v<Quaternion4f> && std::is_standard_layout_v<Quaternion4d>);
static_assert(std::is_trivially_copyable_v<Quaternion4f> &&
              std::is_trivially_copyable_v<Quaternion4d>);
static_assert(std::is_trivially_default_constructible_v<Quaternion4f> &&
              std::is_trivially_default_constructible_v<Quaternion4d>);
static_assert(offsetof(Quaternion4f, w) == 3 * sizeof(float));
static_assert(offsetof(Quaternion4d, w) == 3 * sizeof(double));
static_assert(!std::is_convertible_v<Quaternion4d, Quaternion4f>);
static_assert(
    std::is_same_v<decltype(std::declval<Quaternion4f&>() *= std::declval<const Quaternion4f&>()),
                   Quaternion4f>);

void require(bool condition, const char* message) {
  if (!condition) {
    throw std::runtime_error{message};
  }
}

template <typename T>
void exact(const Quaternion4<T>& value, std::array<T, 4> expected, const char* message) {
  require(value.x == expected[0] && value.y == expected[1] && value.z == expected[2] &&
              value.w == expected[3],
          message);
}

template <typename T>
void close(const Quaternion4<T>& value, std::array<T, 4> expected, const char* message) {
  constexpr double tolerance = std::is_same_v<T, float> ? 2.e-6 : 2.e-9;
  const std::array<T, 4> actual{value.x, value.y, value.z, value.w};
  for (std::size_t i = 0; i < actual.size(); ++i) {
    require(std::abs(static_cast<double>(actual[i] - expected[i])) <= tolerance, message);
  }
}

template <typename T> void value_contracts() {
  using Q = Quaternion4<T>;
  exact(Q{bw::math::zero_init}, {T{0}, T{0}, T{0}, T{0}}, "explicit zero");
  exact(Q{bw::math::identity_init}, {T{0}, T{0}, T{0}, T{1}}, "explicit identity");
  exact(Q::identity, {T{0}, T{0}, T{0}, T{1}}, "static identity value");
  exact(Q{T{3}}, {T{3}, T{3}, T{3}, T{3}}, "splat constructor");

  const std::array<T, 4> components{T{1}, T{2}, T{3}, T{4}};
  const Q from_register = Q::from_register(simd::load(components.data()));
  exact(from_register, components, "register factory preserves XYZW");

  const Q a{T{1}, T{2}, T{3}, T{4}};
  const Q b{T{5}, T{6}, T{7}, T{8}};
  exact(a * b, {T{24}, T{48}, T{48}, T{-6}}, "Hamilton product");
  Q compound = a;
  compound *= b;
  exact(compound, {T{24}, T{48}, T{48}, T{-6}}, "compound updates receiver");
  Q self{T{1}, T{0}, T{0}, T{0}};
  self *= self;
  exact(self, {T{0}, T{0}, T{0}, T{-1}}, "compound self product");

  const Q magnitude{T{1}, T{2}, T{2}, T{4}};
  require(magnitude.length_squared() == T{25} && magnitude.length() == T{5},
          "length and squared length");

  const Q cycle{T{0.5}, T{0.5}, T{0.5}, T{0.5}};
  const auto rotated = cycle.rotate({T{1}, T{0}, T{0}});
  require(rotated.x == T{0} && rotated.y == T{1} && rotated.z == T{0},
          "cyclic unit rotation maps X to Y");
  const auto restored = cycle.unrotate(rotated);
  require(restored.x == T{1} && restored.y == T{0} && restored.z == T{0}, "unrotate restores X");

  Q at_threshold{T{0.5}, T{0}, T{0}, T{0}};
  at_threshold.normalize(T{0.25});
  close(at_threshold, {T{1}, T{0}, T{0}, T{0}}, "squared length at tolerance normalizes");
  const Q original{T{0}, T{0}, T{3}, T{4}};
  close(original.normalized(), {T{0}, T{0}, static_cast<T>(0.6), static_cast<T>(0.8)},
        "normalized copy");
  Q zero{bw::math::zero_init};
  zero.normalize();
  exact(zero, {T{0}, T{0}, T{0}, T{1}}, "zero normalizes to identity");

  Q invalid{bw::math::identity_init};
  invalid.x = std::numeric_limits<T>::quiet_NaN();
  require(invalid.contains_non_finite(), "contains_non_finite detects NaN");
  invalid = Q{bw::math::identity_init};
  require(invalid.is_normalized(), "identity is normalized");
  invalid.w = T{2};
  require(!invalid.is_normalized(), "large length error is not normalized");
  invalid.w = std::numeric_limits<T>::infinity();
  require(invalid.contains_non_finite() && !invalid.is_normalized(), "infinite length predicate");
}

void precision_contracts() {
  const Quaternion4d precise{16777217., 2., 3., 4.};
  const Quaternion4f narrow{precise};
  const Quaternion4d wide{narrow};
  require(wide.y == precise.y && wide.w == precise.w,
          "explicit precision conversion preserves representable components");
  Quaternion4d between{9.99999999e-5, 0., 0., 0.};
  between.normalize();
  close(between, {1., 0., 0., 0.}, "default tolerance is the header's float constant");
  Quaternion4d explicit_double{9.99999999e-5, 0., 0., 0.};
  explicit_double.normalize(1.e-8);
  exact(explicit_double, {0., 0., 0., 1.}, "explicit double tolerance selects identity");
}
} // namespace

int main() {
  try {
    value_contracts<float>();
    value_contracts<double>();
    precision_contracts();
    std::puts("quaternion contracts passed");
    return 0;
  } catch (const std::exception& error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
  }
}
