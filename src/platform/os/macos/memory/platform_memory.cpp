#include "bw/platform/os/macos/platform_memory.hpp"
#include "bw/memory/memory_failure.hpp"
#include "bw/platform/memory/memory_failure.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <mach/vm_page_size.h>
#include <mach/vm_statistics.h>
#include <sys/mman.h>

// E.26/E.28: mapping errors enter the source fatal path, not heap fallback.
// MEM.7: reserve/commit/decommit operations retain the selected Mac semantics.
// All source references use Unreal f45729c0b6f097038e72b52d366a62baeca3275b.
// Apple implementation = Engine/Source/Runtime/Core/Private/Apple/ApplePlatformMemory.cpp.
// Apple header = Engine/Source/Runtime/Core/Public/Apple/ApplePlatformMemory.h.
#ifndef NDEBUG
// Own spelling of source check(): same debug preconditions, distinct native fatal
// assertion handling. This is not std::assert's abort policy.
#define BW_MEMORY_CHECK(expression)                                                                \
  ((expression) ? static_cast<void>(0)                                                             \
                : bw::platform::memory_check_failed(#expression, __FILE__, __LINE__))
#else
#define BW_MEMORY_CHECK(expression) static_cast<void>(0)
#endif

namespace bw::platform::macos {
// Apple header:77-84 constructs the common descriptor without acquiring storage.
VirtualMemoryBlock::VirtualMemoryBlock() = default;
VirtualMemoryBlock::VirtualMemoryBlock(void* pointer, std::uint32_t size_in_pages)
    : BasicVirtualMemoryBlock(pointer, size_in_pages) {}

// GetVirtualSizeAlignment:733-737 / Mac GetConstants:249 use native vm_page_size.
std::size_t VirtualMemoryBlock::virtual_size_alignment() {
  static const auto page_size = static_cast<std::size_t>(vm_page_size);
  return page_size;
}
// GetCommitAlignment:739-743 has the same native value, a separate platform contract.
std::size_t VirtualMemoryBlock::commit_alignment() {
  static const auto page_size = static_cast<std::size_t>(vm_page_size);
  return page_size;
}
// Apple header GetActualSize:112-115 converts recorded pages back to byte extent.
std::size_t VirtualMemoryBlock::actual_size() const {
  return static_cast<std::size_t>(size_in_pages_) * virtual_size_alignment();
}

// AllocateVirtual:745-760 rounds/maps and returns a descriptor. No user heap call.
// Keep the inspected source size/alignment API; named arguments are documented.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
VirtualMemoryBlock VirtualMemoryBlock::allocate_virtual(std::size_t size, std::size_t alignment) {
  // AllocateVirtual:748-751: page rounding, 32-bit page count, page-bounded alignment.
  const auto page_size = virtual_size_alignment();
  const auto rounded_size = (size + page_size - 1) & ~(page_size - 1);
  VirtualMemoryBlock result(nullptr, static_cast<std::uint32_t>(rounded_size / page_size));
  [[maybe_unused]] const auto required_alignment = std::max(alignment, page_size);
  BW_MEMORY_CHECK(required_alignment <= page_size);

  // AllocateVirtual:753 and MmapWithTag:65-77: RW anonymous, no-reserve mapping;
  // application tag 241 identifies virtual-memory blocks in native instruments.
  // Darwin accepts VM_MAKE_TAG in this slot for anonymous mappings; the generic
  // POSIX analyzer assumes an fd >= -1 and does not model that native extension.
  // NOLINTNEXTLINE(clang-analyzer-unix.StdCLibraryFunctions)
  result.pointer_ = mmap(nullptr, result.actual_size(), PROT_READ | PROT_WRITE,
                         MAP_PRIVATE | MAP_ANON | MAP_NORESERVE,
                         VM_MAKE_TAG(VM_MEMORY_APPLICATION_SPECIFIC_1 + 1), 0);
  // AllocateVirtual:754-758: failed native mapping is fatal; then check alignment.
  if (result.pointer_ == MAP_FAILED) {
    memory::out_of_memory(result.actual_size(), static_cast<std::uint32_t>(alignment));
  }
  BW_MEMORY_CHECK(result.pointer_ != nullptr &&
                  reinterpret_cast<std::uintptr_t>(result.pointer_) % required_alignment == 0);
  return result;
}

// Commit:780-785 checks a valid aligned range, then issues reuse advice only.
void VirtualMemoryBlock::commit(std::size_t offset, std::size_t size) {
  BW_MEMORY_CHECK(offset % commit_alignment() == 0 && size % commit_alignment() == 0);
  BW_MEMORY_CHECK(offset + size <= actual_size() && pointer_ != nullptr);
  // Source deliberately ignores madvise's result on the reuse path.
  (void)madvise(static_cast<std::byte*>(pointer_) + offset, size, MADV_FREE_REUSE);
}

// Decommit:787-796 makes the range reclaimable without unmapping its address.
void VirtualMemoryBlock::decommit(std::size_t offset, std::size_t size) {
  BW_MEMORY_CHECK(offset % commit_alignment() == 0 && size % commit_alignment() == 0);
  BW_MEMORY_CHECK(offset + size <= actual_size() && pointer_ != nullptr);
  // Decommit:791-795: unlike reuse advice, reusable advice failure is fatal.
  if (madvise(static_cast<std::byte*>(pointer_) + offset, size, MADV_FREE_REUSABLE) != 0) {
    memory::out_of_memory(size, 0);
  }
}

// Apple header CommitByPtr/DecommitByPtr:92-100 forward an interior byte offset.
void VirtualMemoryBlock::commit_by_pointer(void* pointer, std::size_t size) {
  commit(static_cast<std::size_t>(static_cast<std::byte*>(pointer) -
                                  static_cast<std::byte*>(pointer_)),
         size);
}
void VirtualMemoryBlock::decommit_by_pointer(void* pointer, std::size_t size) {
  decommit(static_cast<std::size_t>(static_cast<std::byte*>(pointer) -
                                    static_cast<std::byte*>(pointer_)),
           size);
}
// Apple header whole-block overloads:102-110 use offset zero and recorded extent.
void VirtualMemoryBlock::commit() { commit(0, actual_size()); }
void VirtualMemoryBlock::decommit() { decommit(0, actual_size()); }

// FreeVirtual:764-778 releases one owned mapping, then clears this descriptor only.
void VirtualMemoryBlock::free_virtual() {
  if (pointer_ == nullptr) {
    return;
  }
  BW_MEMORY_CHECK(actual_size() > 0);
  // FreeVirtual:769-774: release the full mapping; native failure cannot continue.
  if (munmap(pointer_, actual_size()) != 0) {
    memory::out_of_memory(actual_size(), 0);
  }
  pointer_ = nullptr;
  size_in_pages_ = 0;
}
} // namespace bw::platform::macos
#undef BW_MEMORY_CHECK
