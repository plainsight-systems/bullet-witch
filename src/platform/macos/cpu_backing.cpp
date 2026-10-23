// Native macOS CPU storage. Positive bytes and power-of-two alignment at least
// max_align_t produce aligned uninitialized storage or null; invalid requests
// return null. Free accepts null or one live original pointer, exactly once.
// Domain owns zeroing, metadata and budget/accounting. Native libmalloc handles
// concurrent allocation/release; callers still synchronize object access.
// Algorithm: validate -> native zone memalign; release -> originating zone
// free. No extra per-allocation metadata or lock, no bounded-time claim or
// immediate page return guarantee. Factory bootstrap is an explicit external
// allocation. Design MCP R.1 (#rr-raii); implementation E.6 (#re-raii): the
// shared provider survives domain owners. Both phases apply performance MEM.6
// (guidelines/memory/MEM.6-inject-allocators-behind-one-interface.md): native
// allocation stays behind the injected CPU interface. See
// memory-macos-backing.md for pinned Apple/Unreal source and the independently
// reclaimed Bullet Witch contract.
#include "platform/macos/cpu_backing.hpp"

#include <malloc/malloc.h>

#include <bit>

namespace bw::platform::macos {
namespace {
class MacCpuBacking final : public memory::CpuBacking {
 public:
  void* allocate(std::size_t bytes, std::size_t alignment) noexcept override {
    if (bytes == 0 || alignment < alignof(std::max_align_t) ||
        !std::has_single_bit(alignment)) {
      return nullptr;
    }
    return malloc_zone_memalign(malloc_default_zone(), alignment, bytes);
  }

  void deallocate(void* pointer, std::size_t /*bytes*/,
                  std::size_t /*alignment*/) noexcept override {
    if (pointer != nullptr) {
      malloc_zone_free(malloc_zone_from_ptr(pointer), pointer);
    }
  }
};
}  // namespace

std::shared_ptr<memory::CpuBacking> make_cpu_backing() {
  return std::make_shared<MacCpuBacking>();
}
}  // namespace bw::platform::macos
