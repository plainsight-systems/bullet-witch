// Unit test: the compositions bw/math/simd/register.hpp builds on top of the
// native lane operations. Single-intrinsic wrappers are not asserted here; the
// guard page is the only OS use and proves the three-element loads read exactly
// three elements, which is part of their contract.
#include "bw/math/simd/register.hpp"

#include <sys/mman.h>
#include <unistd.h>

#include <array>
#include <cmath>
#include <cstddef>
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

void require(bool condition, const char* message) {
  if (!condition) {
    throw std::runtime_error{message};
  }
}

template <typename T> using Register = decltype(simd::broadcast(T{}));

template <typename T> std::array<T, 4> lanes(Register<T> value) {
  std::array<T, 4> result{};
  simd::store(value, result.data());
  return result;
}

template <typename T>
void exact(Register<T> value, std::array<T, 4> expected, const char* message) {
  require(lanes<T>(value) == expected, message);
}

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

// The refinement steps after the native estimates are ours.
template <typename T> void refinement() {
  const std::array<T, 4> input{T{3}, T{7}, T{11}, T{19}};
  const auto value = simd::load(input.data());
  const auto reciprocal = lanes<T>(simd::reciprocal(value));
  const auto root = lanes<T>(simd::reciprocal_square_root(value));
  for (std::size_t i = 0; i < input.size(); ++i) {
    require(std::abs(reciprocal[i] * input[i] - T{1}) <= static_cast<T>(1.e-7),
            "reciprocal residual");
    require(std::abs(root[i] * root[i] * input[i] - T{1}) <= static_cast<T>(1.e-6),
            "reciprocal square root residual");
  }
}

// Reductions and the cross product are compositions over several lanes.
template <typename T> void reductions() {
  const auto one = simd::broadcast(T{1});
  const std::array<T, 4> input{T{2}, T{3}, T{4}, T{5}};
  exact<T>(simd::dot4(simd::load(input.data()), one), {T{14}, T{14}, T{14}, T{14}}, "dot4");
  exact<T>(simd::dot3(simd::load(input.data()), one), {T{9}, T{9}, T{9}, T{9}}, "dot3");
  const std::array<T, 4> exceptional{T{2}, T{3}, T{4}, std::numeric_limits<T>::infinity()};
  exact<T>(simd::dot3(simd::load(exceptional.data()), one), {T{9}, T{9}, T{9}, T{9}},
           "dot3 ignores W");
  const auto x = simd::load_xyz_zero(std::array<T, 3>{T{1}, T{0}, T{0}}.data());
  const auto y = simd::load_xyz_zero(std::array<T, 3>{T{0}, T{1}, T{0}}.data());
  exact<T>(simd::cross(x, y), {T{0}, T{0}, T{1}, T{0}}, "basis cross orientation");
  exact<T>(simd::cross(y, x), {T{0}, T{0}, T{-1}, T{0}}, "reverse cross orientation");
}

// Three-element loads and stores must touch exactly three elements.
template <typename T> void three_element_access() {
  GuardedInput guarded;
  auto* xyz = reinterpret_cast<T*>(guarded.end() - sizeof(T) * 3);
  xyz[0] = T{2};
  xyz[1] = T{3};
  xyz[2] = T{5};
  const auto zero = simd::load_xyz_zero(xyz);
  exact<T>(zero, {T{2}, T{3}, T{5}, T{0}}, "XYZ zero load reads three elements");
  exact<T>(simd::load_xyz_one(xyz), {T{2}, T{3}, T{5}, T{1}}, "XYZ one load reads three elements");
  std::array<T, 5> destination{T{91}, T{0}, T{0}, T{0}, T{93}};
  simd::store_xyz(simd::set_w_one(zero), destination.data() + 1);
  require(destination == std::array<T, 5>{T{91}, T{2}, T{3}, T{5}, T{93}}, "XYZ store extent");
}

// Double registers are two pairs; lane routing across the pairs is ours.
void double_pair_routing() {
  const std::array<double, 4> first{2., 3., 5., 7.};
  const std::array<double, 4> second{11., 13., 17., 19.};
  const auto a = simd::load(first.data());
  const auto b = simd::load(second.data());
  exact<double>(simd::swizzle<1, 0, 3, 2>(a), {3., 2., 7., 5.}, "same-pair swizzle");
  exact<double>(simd::swizzle<0, 3, 2, 1>(a), {2., 7., 5., 3.}, "cross-pair swizzle");
  exact<double>(simd::shuffle<3, 0, 1, 2>(a, b), {7., 2., 13., 17.}, "two-source shuffle");
  exact<double>(simd::replicate<2>(a), {5., 5., 5., 5.}, "replicate from the high pair");
  const auto zero = simd::broadcast(0.);
  for (std::size_t lane = 0; lane < 4; ++lane) {
    std::array<double, 4> values{};
    values[lane] = 1.;
    require(simd::any_greater(simd::load(values.data()), zero), "every lane participates");
  }
}
} // namespace

int main() {
  try {
    refinement<float>();
    refinement<double>();
    reductions<float>();
    reductions<double>();
    three_element_access<float>();
    three_element_access<double>();
    double_pair_routing();
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
