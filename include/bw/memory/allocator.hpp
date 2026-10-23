// Owning STL adapter for domain-backed containers and allocate_shared controls.
// Explicit domain construction rejects null. Default construction is the named
// migration exception for legacy parser/tool callers: std::allocator storage,
// visible through domain()==nullptr. A domain failure NEVER falls back to it.
// Copy/rebind and copy/move/swap propagation retain the same capability, even
// for empty containers and weak shared-control owners. Nested containers
// require explicit propagation by their owning type (or
// scoped_allocator_adaptor). Algorithm: check element multiplication -> domain
// zeroed allocation or legacy allocator -> construct externally -> destroy
// externally -> matched release. Cost: shared-owner reference traffic per
// allocator copy, O(bytes) domain fill; no payload copies or metadata
// allocations by this adapter. No reset semantics. Design: cpp-guidelines R.20
// (#rr-owner), E.6 (#re-raii) keep capabilities alive; cpp-performance MEM.1
// (guidelines/memory/MEM.1-arena-allocator-for-frame-scope.md) excludes arenas
// for independent lifetimes. Implementation: SL.con.3
// (#rsl-bounds) checks multiplication; MEM.6 (guidelines/memory/
// MEM.6-inject-allocators-behind-one-interface.md) injects at owner boundaries.
#pragma once

#include <limits>
#include <memory>
#include <new>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "bw/memory/domain.hpp"

namespace bw::memory {
template <typename T>
class Allocator {
 public:
  using value_type = T;
  using propagate_on_container_copy_assignment = std::true_type;
  using propagate_on_container_move_assignment = std::true_type;
  using propagate_on_container_swap = std::true_type;
  using is_always_equal = std::false_type;

  Allocator() noexcept = default;
  explicit Allocator(std::shared_ptr<Domain> domain)
      : domain_(std::move(domain)) {
    if (!domain_) {
      throw std::invalid_argument("Null memory domain");
    }
  }
  template <typename U>
  // STL allocator rebinding requires this implicit owner-preserving conversion.
  // NOLINTNEXTLINE(google-explicit-constructor)
  Allocator(const Allocator<U>& other) noexcept : domain_(other.domain()) {}

  [[nodiscard]] T* allocate(std::size_t count) {
    if (count > std::numeric_limits<std::size_t>::max() / sizeof(T)) {
      throw std::bad_array_new_length();
    }
    if (domain_) {
      return static_cast<T*>(
          domain_->allocate_zeroed(count * sizeof(T), alignof(T)));
    }
    return std::allocator<T>{}.allocate(count);
  }
  void deallocate(T* pointer, std::size_t count) noexcept {
    if (domain_) {
      Domain::release(pointer);
    } else {
      std::allocator<T>{}.deallocate(pointer, count);
    }
  }
  [[nodiscard]] const std::shared_ptr<Domain>& domain() const noexcept {
    return domain_;
  }
  template <typename U>
  [[nodiscard]] bool operator==(const Allocator<U>& other) const noexcept {
    return domain_ == other.domain();
  }

 private:
  std::shared_ptr<Domain> domain_;
};
template <typename T>
using Vector = std::vector<T, Allocator<T>>;
using String = std::basic_string<char, std::char_traits<char>, Allocator<char>>;
}  // namespace bw::memory
