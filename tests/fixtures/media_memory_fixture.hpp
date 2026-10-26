// In-process native-backing substitute for media lifetime/admission tests.
// Atomics record acquire/release and allow deterministic failure after priming.
// Native raw storage is paired by Domain; fixture does not inspect payloads.
// Design R.1 (#rr-raii), implementation E.6 (#re-raii) retain owners;
// MEM.6 (guidelines/memory/MEM.6-inject-allocators-behind-one-interface.md)
// injects this seam and MEM.9 (guidelines/memory/
// MEM.9-embedded-allocate-at-init.md) checks callback storage stays prepared.
#pragma once

#include <atomic>
#include <new>

#include "bw/memory/domain.hpp"

namespace bw::test {
class MediaBacking final : public memory::CpuBacking {
 public:
  void* allocate(std::size_t bytes, std::size_t alignment) noexcept override {
    ++attempts;
    if (reject.load()) {
      return nullptr;
    }
    // Test provider boundary transfers raw storage to the RAII domain owner.
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
    void* result =
        ::operator new(bytes, std::align_val_t(alignment), std::nothrow);
    if (result != nullptr) {
      ++acquired;
    }
    return result;
  }
  void deallocate(void* pointer, std::size_t /*bytes*/,
                  std::size_t alignment) noexcept override {
    ::operator delete(pointer, std::align_val_t(alignment));
    ++released;
  }
  std::atomic<bool> reject{false};
  std::atomic<std::size_t> attempts{0}, acquired{0}, released{0};
};
}  // namespace bw::test
