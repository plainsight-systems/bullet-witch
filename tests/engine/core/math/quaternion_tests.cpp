#include "bw/math/rotation/quaternion.hpp"

#include <sys/wait.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
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
  // Fixture budgets chosen before implementation, allowing native two-step rsqrt.
  // They are not a claimed Unreal accuracy contract or double-epsilon guarantee.
  constexpr double tolerance = std::is_same_v<T, float> ? 2.e-6 : 2.e-9;
  const std::array<T, 4> actual{value.x, value.y, value.z, value.w};
  for (std::size_t i = 0; i < actual.size(); ++i) {
    require(std::abs(static_cast<double>(actual[i] - expected[i])) <= tolerance, message);
  }
}

template <typename T> void value_contracts() {
  using Q = Quaternion4<T>;
  const Q value_initialized{};
  exact(value_initialized, {T{0}, T{0}, T{0}, T{0}}, "braced value initialization zeros");
  // Ordinary uninitialized default is checked by type traits, never read.
  exact(Q{bw::math::zero_init}, {T{0}, T{0}, T{0}, T{0}}, "explicit force zero");
  exact(Q{bw::math::identity_init}, {T{0}, T{0}, T{0}, T{1}}, "explicit force identity");
  exact(Q::identity, {T{0}, T{0}, T{0}, T{1}}, "static identity value");
  exact(Q{T{3}}, {T{3}, T{3}, T{3}, T{3}}, "splat constructor");
  const std::array<T, 4> components{T{1}, T{2}, T{3}, T{4}};
  const Q from_register = Q::from_register(simd::load(components.data()));
  exact(from_register, components, "register factory preserves XYZW");
  const Q a{T{1}, T{2}, T{3}, T{4}};
  const Q b{T{5}, T{6}, T{7}, T{8}};
  exact(a * b, {T{24}, T{48}, T{48}, T{-6}}, "native product independent Hamilton result");
  Q compound = a;
  Q returned = (compound *= b);
  exact(compound, {T{24}, T{48}, T{48}, T{-6}}, "compound updates receiver");
  returned.x = T{99};
  require(compound.x == T{24}, "compound returns independent value");
  Q self{T{1}, T{0}, T{0}, T{0}};
  self *= self;
  exact(self, {T{0}, T{0}, T{0}, T{-1}}, "compound self alias loads before store");

  // Left-associated scalar product sum differs from register reduction grouping.
  const T large = std::ldexp(T{1}, std::numeric_limits<T>::digits + 1);
  const Q cancellation{large, T{1}, -large, T{1}};
  require(cancellation.dot(Q{T{1}}) == T{1}, "value dot retains scalar sum ordering");
  const Q magnitude{T{1}, T{2}, T{2}, T{4}};
  require(magnitude.length_squared() == T{25} && magnitude.length() == T{5},
          "scalar length and squared length");
  const Q cycle{T{0.5}, T{0.5}, T{0.5}, T{0.5}};
  const auto rotated = cycle.rotate({T{1}, T{0}, T{0}});
  require(rotated.x == T{0} && rotated.y == T{1} && rotated.z == T{0},
          "cyclic unit rotation maps X to Y");
  const auto restored = cycle.unrotate(rotated);
  require(restored.x == T{1} && restored.y == T{0} && restored.z == T{0},
          "scalar unrotate restores X");

  Q at_threshold{T{0.5}, T{0}, T{0}, T{0}};
  at_threshold.normalize(T{0.25});
  close(at_threshold, {T{1}, T{0}, T{0}, T{0}}, "custom tolerance equality normalizes");
  Q unit{T{1}, T{0}, T{0}, T{0}};
  unit.normalize(T{2});
  exact(unit, {T{0}, T{0}, T{0}, T{1}}, "no unit fast path bypasses caller tolerance");
  const Q original{T{0}, T{0}, T{3}, T{4}};
  close(original.normalized(), {T{0}, T{0}, static_cast<T>(0.6), static_cast<T>(0.8)},
        "normalized copy");
  exact(original, {T{0}, T{0}, T{3}, T{4}}, "normalized leaves receiver untouched");
  Q zero{bw::math::zero_init};
  zero.normalize();
  exact(zero, {T{0}, T{0}, T{0}, T{1}}, "zero normalizes to identity");
  zero = Q{bw::math::zero_init};
  zero.normalize(T{-1});
  require(std::isnan(zero.x) && std::isnan(zero.w), "negative tolerance not repaired");
  Q invalid{bw::math::identity_init};
  invalid.x = std::numeric_limits<T>::quiet_NaN();
  require(invalid.contains_non_finite(), "contains nonfinite separately detects NaN");
  require(invalid.is_normalized(), "source ordered normalized predicate accepts NaN error");
  invalid.normalize();
  exact(invalid, {T{0}, T{0}, T{0}, T{1}}, "NaN normalization selects identity");
  require(invalid.is_normalized(), "identity is normalized");
  invalid.w = T{2};
  require(!invalid.is_normalized(), "large length error is not normalized");
  invalid.w = std::numeric_limits<T>::infinity();
  require(invalid.contains_non_finite() && !invalid.is_normalized(), "infinite length predicate");
}

template <typename T> void predicate_contracts() {
  const auto zero = simd::broadcast(T{0});
  require(!simd::any_greater(zero, zero), "equal lanes are not greater");
  for (std::size_t lane = 0; lane < 4; ++lane) {
    std::array<T, 4> values{};
    values[lane] = T{1};
    require(simd::any_greater(simd::load(values.data()), zero), "every lane participates");
    values[lane] = T{-1};
    require(!simd::any_greater(simd::load(values.data()), zero), "negative lane not greater");
    values[lane] = std::numeric_limits<T>::quiet_NaN();
    require(!simd::any_greater(simd::load(values.data()), zero), "ordered NaN comparison false");
  }
}

void precision_contracts() {
  const Quaternion4d precise{16777217., 2., 3., 4.};
  const Quaternion4f narrow{precise};
  const Quaternion4d wide{narrow};
  require(wide.x == 16777216. && wide.y == precise.y && wide.w == precise.w,
          "explicit precision conversion rounds only unrepresentable component");
  Quaternion4d between{9.99999999e-5, 0., 0., 0.};
  between.normalize();
  close(between, {1., 0., 0., 0.}, "value default is promoted float threshold");
  Quaternion4d explicit_double{9.99999999e-5, 0., 0., 0.};
  explicit_double.normalize(1.e-8);
  exact(explicit_double, {0., 0., 0., 1.}, "explicit double threshold selects identity");
}

int diagnostic_child() {
  for (int repetition = 0; repetition < 3; ++repetition) {
    const Quaternion4f invalid_float{std::numeric_limits<float>::infinity(), 2.F, 3.F, 4.F};
    const std::array<double, 4> raw{1., 2., 3., std::numeric_limits<double>::quiet_NaN()};
    const auto invalid_double = Quaternion4d::from_register(simd::load(raw.data()));
    require(std::isinf(invalid_float.x) && invalid_float.w == 4.F && std::isnan(invalid_double.w) &&
                invalid_double.x == 1.,
            "constructor diagnostics preserve all components");
  }
  return 0;
}

void diagnostics(const char* executable) {
  std::array<int, 2> output{};
  require(pipe(output.data()) == 0, "stdout capture pipe");
  const pid_t child = fork();
  require(child >= 0, "diagnostic process fork");
  if (child == 0) {
    ::close(output[0]);
    if (dup2(output[1], STDOUT_FILENO) < 0) {
      _exit(126);
    }
    ::close(output[1]);
    execl(executable, executable, "diagnostic-child", static_cast<char*>(nullptr));
    _exit(127);
  }
  ::close(output[1]);
  std::string captured;
  std::array<char, 1024> buffer{};
  for (;;) {
    const auto count = read(output[0], buffer.data(), buffer.size());
    if (count < 0 && errno == EINTR) {
      continue;
    }
    require(count >= 0, "stdout read");
    if (count == 0) {
      break;
    }
    captured.append(buffer.data(), static_cast<std::size_t>(count));
  }
  ::close(output[0]);
  int status = 0;
  pid_t waited;
  do {
    waited = waitpid(child, &status, 0);
  } while (waited < 0 && errno == EINTR);
  require(waited == child && WIFEXITED(status) && WEXITSTATUS(status) == 0,
          "nonfatal diagnostic child completes");
#if BW_ENABLE_MATH_DIAGNOSTICS
  std::size_t reports = 0;
  std::size_t cursor = 0;
  while ((cursor = captured.find("LogCore: Error:", cursor)) != std::string::npos) {
    ++reports;
    ++cursor;
  }
  require(reports == 2, "one real diagnostic per quaternion precision/callsite");
  require(captured.find("Y=2.000000000 Z=3.000000000 W=4.000000000") != std::string::npos,
          "diagnostic includes XYZW with source fractional precision");
#else
  require(captured.empty(), "non-Debug constructor diagnostics omitted");
#endif
}
} // namespace

int main(int argc, char** argv) {
  try {
    if (argc == 2 && std::string_view{argv[1]} == "diagnostic-child") {
      return diagnostic_child();
    }
    value_contracts<float>();
    value_contracts<double>();
    predicate_contracts<float>();
    predicate_contracts<double>();
    precision_contracts();
    diagnostics(argv[0]);
    std::puts("Quaternion value contracts passed");
    return 0;
  } catch (const std::exception& error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
  }
}
