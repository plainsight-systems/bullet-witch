#include "bw/math/simd/register.hpp"

#include <sys/mman.h>
#include <unistd.h>

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
template <typename T> std::array<T, 4> lanes(Register<T> value) {
  std::array<T, 4> result{};
  simd::store(value, result.data());
  return result;
}
template <typename T>
void exact(Register<T> value, std::array<T, 4> expected, const char* message) {
  require(lanes<T>(value) == expected, message);
}

// Real guard-page fixture. Three scalar inputs end at an inaccessible boundary;
// a four-lane overread faults instead of being hidden by extra array storage.
class GuardedInput {
public:
  GuardedInput() {
    const long native_page = ::sysconf(_SC_PAGESIZE);
    require(native_page > 0, "native page size");
    page_ = static_cast<std::size_t>(native_page);
    memory_ = ::mmap(nullptr, page_ * 2, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    require(memory_ != MAP_FAILED, "guard mapping");
    if (::mprotect(static_cast<std::byte*>(memory_) + page_, page_, PROT_NONE) != 0) {
      ::munmap(memory_, page_ * 2);
      throw std::runtime_error{"guard protection"};
    }
  }
  ~GuardedInput() { ::munmap(memory_, page_ * 2); }
  GuardedInput(const GuardedInput&) = delete;
  GuardedInput& operator=(const GuardedInput&) = delete;
  std::byte* end() { return static_cast<std::byte*>(memory_) + page_; }

private:
  void* memory_{};
  std::size_t page_{};
};

template <typename T> void routing() {
  const std::array<T, 4> first{T{2}, T{3}, T{5}, T{7}};
  const std::array<T, 4> second{T{11}, T{13}, T{17}, T{19}};
  const auto a = simd::load(first.data());
  const auto b = simd::load(second.data());
  exact<T>(simd::replicate<0>(a), {T{2}, T{2}, T{2}, T{2}}, "replicate X");
  exact<T>(simd::replicate<1>(a), {T{3}, T{3}, T{3}, T{3}}, "replicate Y");
  exact<T>(simd::replicate<2>(a), {T{5}, T{5}, T{5}, T{5}}, "replicate Z");
  exact<T>(simd::replicate<3>(a), {T{7}, T{7}, T{7}, T{7}}, "replicate W");
  exact<T>(simd::swizzle<1, 0, 3, 2>(a), {T{3}, T{2}, T{7}, T{5}}, "same-pair swizzle");
  exact<T>(simd::swizzle<0, 3, 2, 1>(a), {T{2}, T{7}, T{5}, T{3}}, "cross-pair swizzle");
  exact<T>(simd::shuffle<3, 0, 1, 2>(a, b), {T{7}, T{2}, T{13}, T{17}}, "two-source shuffle");
  exact<T>(simd::shuffle<0, 1, 3, 2>(a, b), {T{2}, T{3}, T{19}, T{17}}, "paired shuffle");
}

template <typename T> void reductions() {
  const T large = std::ldexp(T{1}, std::numeric_limits<T>::digits + 1);
  const std::array<T, 4> input{large, T{1}, -large, T{1}};
  const auto a = simd::load(input.data());
  const auto one = simd::broadcast(T{1});
  const T expected4 = std::is_same_v<T, float> ? T{0} : T{2};
  const T expected3 = std::is_same_v<T, float> ? T{0} : T{1};
  exact<T>(simd::dot4(a, one), {expected4, expected4, expected4, expected4},
           "source dot4 grouping");
  exact<T>(simd::dot3(a, one), {expected3, expected3, expected3, expected3},
           "source dot3 grouping");
  const std::array<T, 4> exceptional{T{2}, T{3}, T{4}, std::numeric_limits<T>::infinity()};
  exact<T>(simd::dot3(simd::load(exceptional.data()), one), {T{9}, T{9}, T{9}, T{9}},
           "dot3 result ignores W infinity");
  const auto x = simd::load_xyz_zero(std::array<T, 3>{T{1}, T{0}, T{0}}.data());
  const auto y = simd::load_xyz_zero(std::array<T, 3>{T{0}, T{1}, T{0}}.data());
  exact<T>(simd::cross(x, y), {T{0}, T{0}, T{1}, T{0}}, "basis cross orientation");
  exact<T>(simd::cross(y, x), {T{0}, T{0}, T{-1}, T{0}}, "reverse cross orientation");
  // Actual quaternion rotation caller permits finite quaternion W with vector W0.
  exact<T>(simd::cross(simd::set_w_one(x), y), {T{0}, T{0}, T{1}, T{0}},
           "quaternion W cross convention");
}

void double_fusion() {
  const double delta = std::ldexp(1.0, -27);
  const std::array<double, 4> a{-1.0, 0.0, 1.0 + delta, 0.0};
  const std::array<double, 4> b{1.0, 1.0, 1.0 - delta, 0.0};
  const double residual = -std::ldexp(1.0, -54);
  exact<double>(simd::dot3(simd::load(a.data()), simd::load(b.data())),
                {residual, residual, residual, residual}, "double dot3 fused residual");
  exact<double>(simd::dot4(simd::load(a.data()), simd::load(b.data())),
                {residual, residual, residual, residual}, "double dot4 fused residual");
}

template <typename T> void memory_and_bits() {
  GuardedInput guarded;
  auto* xyz = reinterpret_cast<T*>(guarded.end() - sizeof(T) * 3);
  xyz[0] = T{2};
  xyz[1] = T{3};
  xyz[2] = T{5};
  const auto zero = simd::load_xyz_zero(xyz);
  exact<T>(zero, {T{2}, T{3}, T{5}, T{0}}, "exact XYZ zero load extent");
  exact<T>(simd::load_xyz_one(xyz), {T{2}, T{3}, T{5}, T{1}}, "exact XYZ one load extent");
  std::array<T, 5> destination{T{91}, T{0}, T{0}, T{0}, T{93}};
  simd::store_xyz(simd::set_w_one(zero), destination.data() + 1);
  require(destination == std::array<T, 5>{T{91}, T{2}, T{3}, T{5}, T{93}}, "XYZ store extent");
  const auto payload = std::bit_cast<Bits<T>>(std::numeric_limits<T>::quiet_NaN()) | Bits<T>{0x123};
  const T nan = std::bit_cast<T>(payload);
  const std::array<T, 4> values{-T{0}, T{-2}, nan, T{-3}};
  const auto v = simd::load(values.data());
  for (const auto changed : {simd::set_w_zero(v), simd::set_w_one(v)}) {
    const auto stored = lanes<T>(changed);
    for (std::size_t i = 0; i < 3; ++i)
      require(std::bit_cast<Bits<T>>(stored[i]) == std::bit_cast<Bits<T>>(values[i]),
              "W setter preserves XYZ bits");
  }
  const auto w_zero = lanes<T>(simd::set_w_zero(v));
  const auto w_one = lanes<T>(simd::set_w_one(v));
  require(w_zero[3] == T{0} && !std::signbit(w_zero[3]) && w_one[3] == T{1},
          "W setters replace negative input with positive zero or one");
  const auto permuted = lanes<T>(simd::swizzle<2, 0, 3, 1>(v));
  require(std::bit_cast<Bits<T>>(permuted[0]) == payload &&
              std::bit_cast<Bits<T>>(permuted[1]) == std::bit_cast<Bits<T>>(-T{0}),
          "swizzle preserves NaN payload and signed zero");
  const auto absolute = lanes<T>(simd::absolute(v));
  const auto negative = lanes<T>(simd::negate(v));
  require(!std::signbit(absolute[0]) && absolute[1] == T{2} && std::isnan(absolute[2]),
          "native abs sign behavior");
  require(!std::signbit(negative[0]) && negative[1] == T{2} && std::isnan(negative[2]),
          "native negate sign behavior");
  const auto mask = lanes<T>(simd::compare_greater_equal(v, simd::broadcast(T{0})));
  require(std::bit_cast<Bits<T>>(mask[0]) == std::numeric_limits<Bits<T>>::max() &&
              std::bit_cast<Bits<T>>(mask[1]) == 0 && std::bit_cast<Bits<T>>(mask[2]) == 0,
          "GE equality negative and NaN mask");
  const auto positive_mask =
      lanes<T>(simd::compare_greater_equal(simd::broadcast(T{2}), simd::broadcast(T{1})));
  require(std::bit_cast<Bits<T>>(positive_mask[0]) == std::numeric_limits<Bits<T>>::max(),
          "GE strictly greater comparison");
}
} // namespace

int main() {
  try {
    routing<float>();
    routing<double>();
    reductions<float>();
    reductions<double>();
    double_fusion();
    memory_and_bits<float>();
    memory_and_bits<double>();
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
