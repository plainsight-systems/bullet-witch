#ifndef BW_CORE_MEMORY_GLOBAL_ALLOCATION_HPP
#define BW_CORE_MEMORY_GLOBAL_ALLOCATION_HPP
#include <cstddef>
#include <cstdint>
#include <new>

// Core R.15/R.1: matched storage families, process-lifetime allocator ownership.
// Performance MEM.6: every executable allocation enters the existing facade.
// Compile definitions once per executable, never implicitly into every consumer.
// Scalar and array forms use identical byte/alignment policy. Zero bytes become
// one; default alignment is eight for requested bytes<=8, compiler default
// otherwise. Explicit aligned forms preserve the requested alignment. Nothrow
// does not replace the selected allocator's fatal OOM behavior with recovery.
namespace bw::memory {
// The request every global new forwards to the allocator facade.
struct NewRequest final {
  std::size_t bytes;
  std::uint32_t alignment;
};
// Zero bytes become one. Without an explicit alignment, requests of at most
// eight bytes are eight-aligned and larger ones use the compiler default.
[[nodiscard]] constexpr NewRequest new_request(std::size_t bytes) noexcept {
  return {bytes == 0 ? 1 : bytes,
          bytes <= 8 ? 8U : static_cast<std::uint32_t>(__STDCPP_DEFAULT_NEW_ALIGNMENT__)};
}
// An explicit alignment is forwarded unchanged.
[[nodiscard]] constexpr NewRequest new_request(std::size_t bytes,
                                               std::align_val_t alignment) noexcept {
  return {bytes == 0 ? 1 : bytes, static_cast<std::uint32_t>(alignment)};
}
} // namespace bw::memory

[[nodiscard]] void* operator new(std::size_t bytes);
[[nodiscard]] void* operator new(std::size_t bytes, const std::nothrow_t&) noexcept;
[[nodiscard]] void* operator new(std::size_t bytes, std::align_val_t alignment);
[[nodiscard]] void* operator new(std::size_t bytes, std::align_val_t alignment,
                                 const std::nothrow_t&) noexcept;
[[nodiscard]] void* operator new[](std::size_t bytes);
[[nodiscard]] void* operator new[](std::size_t bytes, const std::nothrow_t&) noexcept;
[[nodiscard]] void* operator new[](std::size_t bytes, std::align_val_t alignment);
[[nodiscard]] void* operator new[](std::size_t bytes, std::align_val_t alignment,
                                   const std::nothrow_t&) noexcept;

// Every delete family forwards only the pointer to free;
// redundant size/alignment and nothrow tags never select another allocator.
void operator delete(void* pointer) noexcept;
void operator delete(void* pointer, const std::nothrow_t&) noexcept;
void operator delete(void* pointer, std::size_t bytes) noexcept;
void operator delete(void* pointer, std::size_t bytes, const std::nothrow_t&) noexcept;
void operator delete(void* pointer, std::align_val_t alignment) noexcept;
void operator delete(void* pointer, std::align_val_t alignment, const std::nothrow_t&) noexcept;
void operator delete(void* pointer, std::size_t bytes, std::align_val_t alignment) noexcept;
void operator delete(void* pointer, std::size_t bytes, std::align_val_t alignment,
                     const std::nothrow_t&) noexcept;
void operator delete[](void* pointer) noexcept;
void operator delete[](void* pointer, const std::nothrow_t&) noexcept;
void operator delete[](void* pointer, std::size_t bytes) noexcept;
void operator delete[](void* pointer, std::size_t bytes, const std::nothrow_t&) noexcept;
void operator delete[](void* pointer, std::align_val_t alignment) noexcept;
void operator delete[](void* pointer, std::align_val_t alignment, const std::nothrow_t&) noexcept;
void operator delete[](void* pointer, std::size_t bytes, std::align_val_t alignment) noexcept;
void operator delete[](void* pointer, std::size_t bytes, std::align_val_t alignment,
                       const std::nothrow_t&) noexcept;
#endif
