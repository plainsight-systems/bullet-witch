#include "bw/memory/global_allocation.hpp"
#include "bw/memory/allocator.hpp"

// Core R.15/R.1: all allocation families have matching release paths; the heap
// survives process teardown. Performance MEM.6: one selected allocator facade.
// Unreal f45729c0b6f097038e72b52d366a62baeca3275b:
// Core/Public/Modules/Boilerplate/ModuleBoilerplate.h:42-70.
// This unit is linked once into each opted-in monolithic executable. Retain the
// source analyzer exclusion; it does not disable ordinary compiler/type checks.
#if !defined(__clang_analyzer__)
static_assert(__STDCPP_DEFAULT_NEW_ALIGNMENT__ <= 16);

// ModuleBoilerplate.h:46: Choose alignment from original size; zero-byte requests become one.
[[gnu::noinline]] void* operator new(std::size_t bytes) {
  const auto request = bw::memory::new_request(bytes);
  return bw::memory::allocate(request.bytes, request.alignment);
}

// ModuleBoilerplate.h:47: Choose alignment from original size; zero-byte requests become one.
[[gnu::noinline]] void* operator new[](std::size_t bytes) {
  const auto request = bw::memory::new_request(bytes);
  return bw::memory::allocate(request.bytes, request.alignment);
}

// ModuleBoilerplate.h:48: Choose alignment from original size; zero-byte requests become one.
// Nothrow retains the same source fatal allocation policy.
[[gnu::noinline]] void* operator new(std::size_t bytes, const std::nothrow_t&) noexcept {
  const auto request = bw::memory::new_request(bytes);
  return bw::memory::allocate(request.bytes, request.alignment);
}

// ModuleBoilerplate.h:49: Choose alignment from original size; zero-byte requests become one.
// Nothrow retains the same source fatal allocation policy.
[[gnu::noinline]] void* operator new[](std::size_t bytes, const std::nothrow_t&) noexcept {
  const auto request = bw::memory::new_request(bytes);
  return bw::memory::allocate(request.bytes, request.alignment);
}

// ModuleBoilerplate.h:50: Preserve explicit alignment; zero-byte requests become one.
[[gnu::noinline]] void* operator new(std::size_t bytes, std::align_val_t alignment) {
  const auto request = bw::memory::new_request(bytes, alignment);
  return bw::memory::allocate(request.bytes, request.alignment);
}

// ModuleBoilerplate.h:51: Preserve explicit alignment; zero-byte requests become one.
[[gnu::noinline]] void* operator new[](std::size_t bytes, std::align_val_t alignment) {
  const auto request = bw::memory::new_request(bytes, alignment);
  return bw::memory::allocate(request.bytes, request.alignment);
}

// ModuleBoilerplate.h:52: Preserve explicit alignment; zero-byte requests become one.
// Nothrow retains the same source fatal allocation policy.
[[gnu::noinline]] void* operator new(std::size_t bytes, std::align_val_t alignment,
                                     const std::nothrow_t&) noexcept {
  const auto request = bw::memory::new_request(bytes, alignment);
  return bw::memory::allocate(request.bytes, request.alignment);
}

// ModuleBoilerplate.h:53: Preserve explicit alignment; zero-byte requests become one.
// Nothrow retains the same source fatal allocation policy.
[[gnu::noinline]] void* operator new[](std::size_t bytes, std::align_val_t alignment,
                                       const std::nothrow_t&) noexcept {
  const auto request = bw::memory::new_request(bytes, alignment);
  return bw::memory::allocate(request.bytes, request.alignment);
}

// ModuleBoilerplate.h:54: release through the same facade; metadata tags
// do not select another heap or change the release operation.
[[gnu::noinline]] void operator delete(void* pointer) noexcept { bw::memory::release(pointer); }

// ModuleBoilerplate.h:55: release through the same facade; metadata tags
// do not select another heap or change the release operation.
[[gnu::noinline]] void operator delete[](void* pointer) noexcept { bw::memory::release(pointer); }

// ModuleBoilerplate.h:56: release through the same facade; metadata tags
// do not select another heap or change the release operation.
[[gnu::noinline]] void operator delete(void* pointer, const std::nothrow_t&) noexcept {
  bw::memory::release(pointer);
}

// ModuleBoilerplate.h:57: release through the same facade; metadata tags
// do not select another heap or change the release operation.
[[gnu::noinline]] void operator delete[](void* pointer, const std::nothrow_t&) noexcept {
  bw::memory::release(pointer);
}

// ModuleBoilerplate.h:58: release through the same facade; metadata tags
// do not select another heap or change the release operation.
[[gnu::noinline]] void operator delete(void* pointer, std::size_t) noexcept {
  bw::memory::release(pointer);
}

// ModuleBoilerplate.h:59: release through the same facade; metadata tags
// do not select another heap or change the release operation.
[[gnu::noinline]] void operator delete[](void* pointer, std::size_t) noexcept {
  bw::memory::release(pointer);
}

// ModuleBoilerplate.h:60: release through the same facade; metadata tags
// do not select another heap or change the release operation.
[[gnu::noinline]] void operator delete(void* pointer, std::size_t, const std::nothrow_t&) noexcept {
  bw::memory::release(pointer);
}

// ModuleBoilerplate.h:61: release through the same facade; metadata tags
// do not select another heap or change the release operation.
[[gnu::noinline]] void operator delete[](void* pointer, std::size_t,
                                         const std::nothrow_t&) noexcept {
  bw::memory::release(pointer);
}

// ModuleBoilerplate.h:62: release through the same facade; metadata tags
// do not select another heap or change the release operation.
[[gnu::noinline]] void operator delete(void* pointer, std::align_val_t) noexcept {
  bw::memory::release(pointer);
}

// ModuleBoilerplate.h:63: release through the same facade; metadata tags
// do not select another heap or change the release operation.
[[gnu::noinline]] void operator delete[](void* pointer, std::align_val_t) noexcept {
  bw::memory::release(pointer);
}

// ModuleBoilerplate.h:64: release through the same facade; metadata tags
// do not select another heap or change the release operation.
[[gnu::noinline]] void operator delete(void* pointer, std::align_val_t,
                                       const std::nothrow_t&) noexcept {
  bw::memory::release(pointer);
}

// ModuleBoilerplate.h:65: release through the same facade; metadata tags
// do not select another heap or change the release operation.
[[gnu::noinline]] void operator delete[](void* pointer, std::align_val_t,
                                         const std::nothrow_t&) noexcept {
  bw::memory::release(pointer);
}

// ModuleBoilerplate.h:66: release through the same facade; metadata tags
// do not select another heap or change the release operation.
[[gnu::noinline]] void operator delete(void* pointer, std::size_t, std::align_val_t) noexcept {
  bw::memory::release(pointer);
}

// ModuleBoilerplate.h:67: release through the same facade; metadata tags
// do not select another heap or change the release operation.
[[gnu::noinline]] void operator delete[](void* pointer, std::size_t, std::align_val_t) noexcept {
  bw::memory::release(pointer);
}

// ModuleBoilerplate.h:68: release through the same facade; metadata tags
// do not select another heap or change the release operation.
[[gnu::noinline]] void operator delete(void* pointer, std::size_t, std::align_val_t,
                                       const std::nothrow_t&) noexcept {
  bw::memory::release(pointer);
}

// ModuleBoilerplate.h:69: release through the same facade; metadata tags
// do not select another heap or change the release operation.
[[gnu::noinline]] void operator delete[](void* pointer, std::size_t, std::align_val_t,
                                         const std::nothrow_t&) noexcept {
  bw::memory::release(pointer);
}

#endif
