#ifndef BW_CORE_MEMORY_ALIGNMENT_HPP
#define BW_CORE_MEMORY_ALIGNMENT_HPP

#include <concepts>
#include <type_traits>

namespace bw::memory {
// Round value up to the next multiple of alignment. Alignment must be a
// nonzero power of two; a value already on the boundary is returned unchanged.
// Shared by the mapping layer, the binned allocator and the crash allocator.
template <std::unsigned_integral T>
[[nodiscard]] constexpr T align_up(T value, std::type_identity_t<T> alignment) noexcept {
  return (value + alignment - 1U) & ~(alignment - 1U);
}
} // namespace bw::memory
#endif
