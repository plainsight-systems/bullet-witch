#include "core/math/vector3.hpp"
#include "platform/math.hpp"

#include <array>
#include <cerrno>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <sys/wait.h>
#include <type_traits>
#include <unistd.h>

// Real numeric consumers, no reference implementation or injected reporter.
// F.20/ES.23: test value results and explicit initialization; SIMD.1/3: these
// tests establish the selected scalar semantics, not later NEON performance.
namespace {
using bw::math::Vector3;
using bw::math::Vector3d;
using bw::math::Vector3f;

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

void require(bool condition, const char* message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

template <typename T> void near(T actual, T expected, const char* message) {
  const T allowed = T{32} * std::numeric_limits<T>::epsilon() * std::fmax(T{1}, std::abs(expected));
  require(std::isfinite(actual) && std::abs(actual - expected) <= allowed, message);
}

template <typename T> void numeric_contracts() {
  const Vector3<T> zero{bw::math::zero_init};
  const Vector3<T> value_initialized{};
  require(zero == Vector3<T>{T{0}, T{0}, T{0}}, "explicit zero initialization");
  require(value_initialized == zero, "C++ value initialization zeroes storage");
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
  require(a.dot(b) == T{21} && a.dot(b) == b.dot(a), "dot value and symmetry");
  const auto cross = a.cross(b);
  require(cross == Vector3<T>{T{-14}, T{-8}, T{13}}, "independent cross result");
  require(cross.dot(a) == T{0} && cross.dot(b) == T{0}, "cross orthogonality");
  require(a != b, "exact equality rejects distinct values");

  Vector3<T> length_case{T{3}, T{4}, T{12}};
  require(length_case.length_squared() == T{169}, "known squared length");
  near(length_case.length(), T{13}, "known length");
  const auto normalized_copy = length_case.safe_normal();
  near(normalized_copy.length(), T{1}, "safe result has unit length");
  near(normalized_copy.x, T{3} / T{13}, "safe result direction");
  require(length_case.normalize(), "ordinary in-place normalization succeeds");
  near(length_case.length(), T{1}, "in-place result has unit length");

  const Vector3<T> fallback{T{7}, T{8}, T{9}};
  require(x.safe_normal(T{2}, fallback) == x, "unit branch precedes tolerance fallback");
  Vector3<T> at_threshold{T{0.5}, T{0}, T{0}};
  const auto original = at_threshold;
  require(!at_threshold.normalize(T{0.25}), "in-place rejects exact equality");
  require(at_threshold == original, "failed normalization preserves receiver");
  require(original.safe_normal(T{0.25}, fallback) == x, "safe-normal accepts equality");
  require(original.safe_normal(T{0.5}, fallback) == fallback, "custom fallback unchanged");
  require(fallback == Vector3<T>{T{7}, T{8}, T{9}}, "fallback source remains unchanged");
  require(zero.safe_normal() == zero, "zero safe-normal returns default fallback");
  Vector3<T> signed_zero{T{-0.0}, T{0}, T{0}};
  require(!signed_zero.normalize() && std::signbit(signed_zero.x),
          "failed normalization preserves negative zero");
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
  near(bw::platform::math::square_root(T{81}), T{9}, "native scalar square root");
  near(bw::platform::math::inverse_square_root(T{16}), T{0.25}, "native reciprocal sqrt");
  require(std::isnan(bw::platform::math::square_root(T{-1})), "negative sqrt is not clamped");
}

void precision_contracts() {
  const Vector3d wide{16777217.0, 2.0, 3.0};
  require(wide.x - 16777216.0 == 1.0, "double vector retains precision beyond float");
  const Vector3f narrow{wide};
  const Vector3d widened{narrow};
  require(widened.x == 16777216.0, "explicit narrowing reflects float precision");
  require(widened.y == wide.y && widened.z == wide.z, "conversion preserves exact components");
  Vector3d default_threshold{std::sqrt((1.e-8 + static_cast<double>(1.e-8F)) / 2.0), 0.0, 0.0};
  near(default_threshold.safe_normal().x, 1.0,
       "double safe-normal default is the source float threshold converted to double");
  require(default_threshold.normalize(),
          "double in-place default is the source float threshold converted to double");
  // Mixed division promotes before narrowing. Prematurely converting this
  // denominator to float would produce infinity and incorrectly return zero.
  const auto promoted_division = Vector3f{1.0F, 0.0F, 0.0F} / 1.e40;
  require(promoted_division.x > 0.0F && promoted_division.x < std::numeric_limits<float>::min(),
          "mixed division retains representable reciprocal subnormal");
  const Vector3d large{3.e100, 4.e100, 0.0};
  near(large.length(), 5.e100, "double length does not use float intermediary");
}

int diagnostic_child() {
  const Vector3f finite{1.0F, 2.0F, 3.0F};
  require(!finite.contains_non_finite(), "finite diagnostic input");
  for (int repetition = 0; repetition != 3; ++repetition) {
    const Vector3f invalid_float{std::numeric_limits<float>::infinity(), 1.0F, 2.0F};
    const Vector3d invalid_double{std::numeric_limits<double>::quiet_NaN(), 4.0, 5.0};
    require(std::isinf(invalid_float.x) && invalid_float.y == 1.0F,
            "diagnostic preserves infinity and other components");
    require(std::isnan(invalid_double.x) && invalid_double.z == 5.0,
            "diagnostic preserves NaN and other components");
  }
  return 0;
}

void diagnostic_contracts(const char* executable) {
  std::array<int, 2> output{};
  require(pipe(output.data()) == 0, "create real stdout pipe");
  const pid_t child = fork();
  require(child >= 0, "fork diagnostic child");
  if (child == 0) {
    close(output[0]);
    if (dup2(output[1], STDOUT_FILENO) < 0) {
      _exit(126);
    }
    close(output[1]);
    execl(executable, executable, "diagnostic-child", static_cast<char*>(nullptr));
    _exit(127);
  }
  close(output[1]);
  std::string captured;
  std::array<char, 1024> buffer{};
  for (;;) {
    const auto count = read(output[0], buffer.data(), buffer.size());
    if (count < 0 && errno == EINTR) {
      continue;
    }
    require(count >= 0, "read diagnostic stdout");
    if (count == 0) {
      break;
    }
    captured.append(buffer.data(), static_cast<std::size_t>(count));
  }
  close(output[0]);
  int status = 0;
  pid_t waited;
  do {
    waited = waitpid(child, &status, 0);
  } while (waited < 0 && errno == EINTR);
  require(waited == child && WIFEXITED(status) && WEXITSTATUS(status) == 0,
          "diagnostic reports continue without abort or throw");
#if BW_ENABLE_MATH_DIAGNOSTICS
  std::size_t reports = 0;
  std::size_t position = 0;
  while ((position = captured.find("LogCore: Error:", position)) != std::string::npos) {
    ++reports;
    ++position;
  }
  require(reports == 2, "one actual Debug Error per vector precision/callsite");
  require(captured.find("Y=1.000 Z=2.000") != std::string::npos &&
              captured.find("Y=4.000 Z=5.000") != std::string::npos,
          "real diagnostic includes preserved component data");
#else
  require(captured.empty(), "source non-Debug configuration emits no vector diagnostic");
#endif
}
} // namespace

int main(int argc, char** argv) {
  try {
    if (argc == 2 && std::string_view{argv[1]} == "diagnostic-child") {
      return diagnostic_child();
    }
    numeric_contracts<float>();
    numeric_contracts<double>();
    precision_contracts();
    diagnostic_contracts(argv[0]);
    std::puts("Math vector and native diagnostic contracts passed.");
    return 0;
  } catch (const std::exception& error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
  }
}
