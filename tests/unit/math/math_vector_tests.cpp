// Unit test: public contracts of bw/math/vector/vector3.hpp and
// bw/platform/math/math.hpp. Needs nothing beyond the code under test.
#include "bw/math/vector/vector3.hpp"
#include "bw/platform/math/math.hpp"

#include "bw/testing/harness.hpp"

#include <array>
#include <cmath>
#include <limits>
#include <type_traits>

namespace {
using bw::math::Vector3;
using bw::math::Vector3d;
using bw::math::Vector3f;

// Layout and type contracts stated by the header.
static_assert(std::is_same_v<bw::math::Vector, Vector3d>);
static_assert(sizeof(Vector3f) == 3 * sizeof(float));
static_assert(sizeof(Vector3d) == 3 * sizeof(double));
static_assert(std::is_trivially_copyable_v<Vector3f>);
static_assert(std::is_trivially_copyable_v<Vector3d>);
static_assert(std::is_trivially_default_constructible_v<Vector3f>);
static_assert(std::is_trivially_default_constructible_v<Vector3d>);
static_assert(!std::is_convertible_v<Vector3f, Vector3d>);
static_assert(!std::is_convertible_v<Vector3d, Vector3f>);
static_assert(std::is_constructible_v<Vector3d, Vector3f>);
static_assert(std::is_constructible_v<Vector3f, Vector3d>);

using bw::testing::require;

template <typename T> void near(T actual, T expected, const char* message) {
  const T allowed = T{32} * std::numeric_limits<T>::epsilon() * std::fmax(T{1}, std::abs(expected));
  require(std::isfinite(actual) && std::abs(actual - expected) <= allowed, message);
}

template <typename T> void numeric_contracts() {
  const Vector3<T> zero{bw::math::zero_init};
  require(zero == Vector3<T>{T{0}, T{0}, T{0}}, "explicit zero initialization");
  require(Vector3<T>::zero == zero, "per-precision zero constant");
  require(Vector3<T>{T{2}} == Vector3<T>{T{2}, T{2}, T{2}}, "scalar initialization");

  const Vector3<T> x{T{1}, T{0}, T{0}};
  const Vector3<T> y{T{0}, T{1}, T{0}};
  const Vector3<T> z{T{0}, T{0}, T{1}};
  require(x.cross(y) == z && y.cross(x) == -z, "cross basis orientation");

  const Vector3<T> a{T{2}, T{3}, T{4}};
  const Vector3<T> b{T{-1}, T{5}, T{2}};
  require(a + b == Vector3<T>{T{1}, T{8}, T{6}}, "component sum");
  require(a - b == Vector3<T>{T{3}, T{-2}, T{2}}, "component difference");
  require(a * b == Vector3<T>{T{-2}, T{15}, T{8}}, "component product");
  require(a * 2 == Vector3<T>{T{4}, T{6}, T{8}}, "integer scaling");
  require(a / 2 == Vector3<T>{T{1}, T{1.5}, T{2}}, "scalar reciprocal scaling");
  require(a.dot(b) == T{21}, "dot value");
  const auto cross = a.cross(b);
  require(cross == Vector3<T>{T{-14}, T{-8}, T{13}}, "cross result");
  require(a != b, "exact equality rejects distinct values");

  Vector3<T> length_case{T{3}, T{4}, T{12}};
  require(length_case.length_squared() == T{169}, "known squared length");
  const auto normalized_copy = length_case.safe_normal();
  near(normalized_copy.length(), T{1}, "safe result has unit length");
  near(normalized_copy.x, T{3} / T{13}, "safe result direction");
  require(length_case.normalize(), "in-place normalization succeeds");
  near(length_case.length(), T{1}, "in-place result has unit length");

  const Vector3<T> fallback{T{7}, T{8}, T{9}};
  Vector3<T> at_threshold{T{0.5}, T{0}, T{0}};
  const auto original = at_threshold;
  require(!at_threshold.normalize(T{0.25}), "in-place rejects squared length at tolerance");
  require(at_threshold == original, "failed normalization preserves receiver");
  require(original.safe_normal(T{0.25}, fallback) == x, "safe-normal accepts equality");
  require(original.safe_normal(T{0.5}, fallback) == fallback, "below tolerance uses fallback");
  require(zero.safe_normal() == zero, "zero safe-normal returns default fallback");
  const Vector3<T> small{static_cast<T>(1.e-5F), T{0}, T{0}};
  require(small.safe_normal() == zero, "default threshold uses squared magnitude");

  Vector3<T> classified{T{1}, T{2}, T{3}};
  require(!classified.contains_non_finite(), "finite vector classification");
  classified.y = std::numeric_limits<T>::infinity();
  require(classified.contains_non_finite(), "infinity vector classification");
  classified.y = T{2};
  classified.z = std::numeric_limits<T>::quiet_NaN();
  require(classified.contains_non_finite(), "NaN vector classification");

  require(bw::platform::math::is_finite(std::numeric_limits<T>::max()), "maximum finite");
  require(bw::platform::math::is_finite(std::numeric_limits<T>::denorm_min()), "subnormal finite");
  require(!bw::platform::math::is_finite(-std::numeric_limits<T>::infinity()),
          "negative infinity classification");
  require(!bw::platform::math::is_finite(std::numeric_limits<T>::quiet_NaN()),
          "scalar NaN classification");
}

void precision_contracts() {
  const Vector3d wide{16777217.0, 2.0, 3.0};
  const Vector3f narrow{wide};
  const Vector3d widened{narrow};
  require(widened.y == wide.y && widened.z == wide.z, "conversion preserves exact components");
  const auto promoted_division = Vector3f{1.0F, 0.0F, 0.0F} / 1.e40;
  require(promoted_division.x > 0.0F && promoted_division.x < std::numeric_limits<float>::min(),
          "mixed division retains representable reciprocal subnormal");
  const Vector3d large{3.e100, 4.e100, 0.0};
  near(large.length(), 5.e100, "double length does not use float intermediary");
}

// The executable's contracts, one CTest entry.
void contracts() {
  numeric_contracts<float>();
  numeric_contracts<double>();
  precision_contracts();
}

using Scenario = bw::testing::Scenario<void>;
constexpr std::array scenarios{Scenario{"math_vector", contracts}};
} // namespace

int main(int argc, char** argv) { return bw::testing::run_scenario(argc, argv, scenarios); }
