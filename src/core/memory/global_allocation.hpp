#ifndef BW_CORE_MEMORY_GLOBAL_ALLOCATION_HPP
#define BW_CORE_MEMORY_GLOBAL_ALLOCATION_HPP
#include <cstddef>
#include <new>

// Core R.15/R.1: matched storage families, process-lifetime allocator ownership.
// Performance MEM.6: every executable allocation enters the existing facade.
// Unreal f45729c0b6f097038e72b52d366a62baeca3275b:
// Core/Public/Modules/Boilerplate/ModuleBoilerplate.h:20-70.
// Compile definitions once per executable, never implicitly into every consumer.
// Scalar and array forms use identical byte/alignment policy. Zero bytes become
// one; default alignment is eight for requested bytes<=8, compiler default
// otherwise. Explicit aligned forms preserve the requested alignment. Nothrow
// does not replace the selected allocator's fatal OOM behavior with recovery.
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

// Same reference:54-69. Every delete family forwards only the pointer to free;
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
