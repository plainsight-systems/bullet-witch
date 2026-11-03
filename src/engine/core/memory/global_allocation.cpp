#include "bw/memory/global_allocation.hpp"
#include "bw/memory/allocator.hpp"

// Core R.15/R.1: all allocation families have matching release paths; the heap
// survives process teardown. Performance MEM.6: one selected allocator facade.
// This unit is linked once into each opted-in monolithic executable. Retain the
// analyzer exclusion; it does not disable ordinary compiler/type checks.
#if !defined(__clang_analyzer__)
static_assert(__STDCPP_DEFAULT_NEW_ALIGNMENT__ <= 16);

// Choose alignment from original size; zero-byte requests become one.
[[gnu::noinline]] void* operator new(std::size_t bytes) {
  const auto request = bw::memory::new_request(bytes);
  return bw::memory::allocate(request.bytes, request.alignment);
}

// Choose alignment from original size; zero-byte requests become one.
[[gnu::noinline]] void* operator new[](std::size_t bytes) {
  const auto request = bw::memory::new_request(bytes);
  return bw::memory::allocate(request.bytes, request.alignment);
}

// Choose alignment from original size; zero-byte requests become one.
// Nothrow retains the same fatal allocation policy.
[[gnu::noinline]] void* operator new(std::size_t bytes, const std::nothrow_t&) noexcept {
  const auto request = bw::memory::new_request(bytes);
  return bw::memory::allocate(request.bytes, request.alignment);
}

// Choose alignment from original size; zero-byte requests become one.
// Nothrow retains the same fatal allocation policy.
[[gnu::noinline]] void* operator new[](std::size_t bytes, const std::nothrow_t&) noexcept {
  const auto request = bw::memory::new_request(bytes);
  return bw::memory::allocate(request.bytes, request.alignment);
}

// Preserve explicit alignment; zero-byte requests become one.
[[gnu::noinline]] void* operator new(std::size_t bytes, std::align_val_t alignment) {
  const auto request = bw::memory::new_request(bytes, alignment);
  return bw::memory::allocate(request.bytes, request.alignment);
}

// Preserve explicit alignment; zero-byte requests become one.
[[gnu::noinline]] void* operator new[](std::size_t bytes, std::align_val_t alignment) {
  const auto request = bw::memory::new_request(bytes, alignment);
  return bw::memory::allocate(request.bytes, request.alignment);
}

// Preserve explicit alignment; zero-byte requests become one.
// Nothrow retains the same fatal allocation policy.
[[gnu::noinline]] void* operator new(std::size_t bytes, std::align_val_t alignment,
                                     const std::nothrow_t&) noexcept {
  const auto request = bw::memory::new_request(bytes, alignment);
  return bw::memory::allocate(request.bytes, request.alignment);
}

// Preserve explicit alignment; zero-byte requests become one.
// Nothrow retains the same fatal allocation policy.
[[gnu::noinline]] void* operator new[](std::size_t bytes, std::align_val_t alignment,
                                       const std::nothrow_t&) noexcept {
  const auto request = bw::memory::new_request(bytes, alignment);
  return bw::memory::allocate(request.bytes, request.alignment);
}

// Release through the same facade; metadata tags
// do not select another heap or change the release operation.
[[gnu::noinline]] void operator delete(void* pointer) noexcept { bw::memory::release(pointer); }

// Release through the same facade; metadata tags
// do not select another heap or change the release operation.
[[gnu::noinline]] void operator delete[](void* pointer) noexcept { bw::memory::release(pointer); }

// Release through the same facade; metadata tags
// do not select another heap or change the release operation.
[[gnu::noinline]] void operator delete(void* pointer, const std::nothrow_t&) noexcept {
  bw::memory::release(pointer);
}

// Release through the same facade; metadata tags
// do not select another heap or change the release operation.
[[gnu::noinline]] void operator delete[](void* pointer, const std::nothrow_t&) noexcept {
  bw::memory::release(pointer);
}

// Release through the same facade; metadata tags
// do not select another heap or change the release operation.
[[gnu::noinline]] void operator delete(void* pointer, std::size_t) noexcept {
  bw::memory::release(pointer);
}

// Release through the same facade; metadata tags
// do not select another heap or change the release operation.
[[gnu::noinline]] void operator delete[](void* pointer, std::size_t) noexcept {
  bw::memory::release(pointer);
}

// Release through the same facade; metadata tags
// do not select another heap or change the release operation.
[[gnu::noinline]] void operator delete(void* pointer, std::size_t, const std::nothrow_t&) noexcept {
  bw::memory::release(pointer);
}

// Release through the same facade; metadata tags
// do not select another heap or change the release operation.
[[gnu::noinline]] void operator delete[](void* pointer, std::size_t,
                                         const std::nothrow_t&) noexcept {
  bw::memory::release(pointer);
}

// Release through the same facade; metadata tags
// do not select another heap or change the release operation.
[[gnu::noinline]] void operator delete(void* pointer, std::align_val_t) noexcept {
  bw::memory::release(pointer);
}

// Release through the same facade; metadata tags
// do not select another heap or change the release operation.
[[gnu::noinline]] void operator delete[](void* pointer, std::align_val_t) noexcept {
  bw::memory::release(pointer);
}

// Release through the same facade; metadata tags
// do not select another heap or change the release operation.
[[gnu::noinline]] void operator delete(void* pointer, std::align_val_t,
                                       const std::nothrow_t&) noexcept {
  bw::memory::release(pointer);
}

// Release through the same facade; metadata tags
// do not select another heap or change the release operation.
[[gnu::noinline]] void operator delete[](void* pointer, std::align_val_t,
                                         const std::nothrow_t&) noexcept {
  bw::memory::release(pointer);
}

// Release through the same facade; metadata tags
// do not select another heap or change the release operation.
[[gnu::noinline]] void operator delete(void* pointer, std::size_t, std::align_val_t) noexcept {
  bw::memory::release(pointer);
}

// Release through the same facade; metadata tags
// do not select another heap or change the release operation.
[[gnu::noinline]] void operator delete[](void* pointer, std::size_t, std::align_val_t) noexcept {
  bw::memory::release(pointer);
}

// Release through the same facade; metadata tags
// do not select another heap or change the release operation.
[[gnu::noinline]] void operator delete(void* pointer, std::size_t, std::align_val_t,
                                       const std::nothrow_t&) noexcept {
  bw::memory::release(pointer);
}

// Release through the same facade; metadata tags
// do not select another heap or change the release operation.
[[gnu::noinline]] void operator delete[](void* pointer, std::size_t, std::align_val_t,
                                         const std::nothrow_t&) noexcept {
  bw::memory::release(pointer);
}

#endif
