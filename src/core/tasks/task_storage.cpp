#include "core/tasks/task_storage.hpp"

#include "core/memory/allocator.hpp"
#include "platform/allocator_sync.hpp"
#include "platform/memory_failure.hpp"

#include <array>
#include <atomic>
#include <cstdint>
#include <limits>
#include <new>
#include <utility>

// R.15 / MEM.2: task objects pair construction/destruction with this storage;
// CP.2 / CONC.2: producer count is private, shared final release uses acq_rel.
namespace bw::tasks {
namespace {
// Unreal f45729c0b6f097038e72b52d366a62baeca3275b:
// TaskGraphInterfaces.h:451-465; ConcurrentLinearAllocator.h:198-287.
// Mac inherits guaranteed alignment4096, selecting allocation-header path.
constexpr std::size_t block_bytes = std::size_t{64} * 1024;
constexpr unsigned initial_count = std::numeric_limits<unsigned>::max();
struct Block final {
  std::atomic<unsigned> live{initial_count};
  std::array<std::byte, platform::allocator_cache_line_size - sizeof(std::atomic<unsigned>)>
      padding{};
  std::uintptr_t next{};
  unsigned allocated{};
};
struct Allocation final {
  std::uint32_t block_offset;
  std::uint32_t size;
};

// TBlockAllocationCache::SwapBlock/Malloc/Free, ConcurrentLinearAllocator.h:61-115:
// one completed block per thread; evict the previous block through FMemory.
struct CachedBlock final {
  void* pointer{};
  ~CachedBlock() { memory::release(pointer); }
};
CachedBlock& cached_block() {
  thread_local CachedBlock cached;
  return cached;
}
void cache_block(Block* block) {
  block->~Block();
  memory::release(std::exchange(cached_block().pointer, block));
}

// FBlockHeader constructor, ConcurrentLinearAllocator.h:238-245 reserves both
// block and allocation header. Mac non-fast block alignment is header alignment.
Block* construct_block(void* storage) {
  auto* result = new (storage) Block;
  result->next = reinterpret_cast<std::uintptr_t>(result) + sizeof(Block) + sizeof(Allocation);
  return result;
}

// FTLSCleanup, ConcurrentLinearAllocator.h:288-312: producer closes its current
// block before exit, subtracting unused sentinel count once. Consumers may still
// own objects; only the final release can then cache/free the block.
struct Producer final {
  Block* block{};
  Producer() { (void)cached_block(); }
  ~Producer() {
    if (block != nullptr) {
      block->next = reinterpret_cast<std::uintptr_t>(block) + block_bytes;
      const auto excess = initial_count - block->allocated;
      if (block->live.fetch_sub(excess, std::memory_order_acq_rel) == excess) {
        cache_block(block);
      }
    }
  }
};
Producer& producer() {
  thread_local Producer current;
  return current;
}

// Inline AllocateNewBlock, ConcurrentLinearAllocator.h:369-393, plus source
// one-block cache. No oversized path: TaskGraph tag explicitly disables it.
Block* fresh_block() {
  void* storage = std::exchange(cached_block().pointer, nullptr);
  if (storage == nullptr) {
    storage = memory::allocate(block_bytes, static_cast<std::uint32_t>(alignof(Block)));
  }
  return construct_block(storage);
}
} // namespace

// TLinearAllocatorBase::Malloc non-fast path, ConcurrentLinearAllocator.h:360-492:
// align allocation, write preceding offset header, bump private count; on a full
// block reconcile shared count and reuse only when no outstanding objects remain.
void* allocate_task_storage(std::size_t bytes, std::uint32_t alignment) {
  if (alignment == 0 || (alignment & (alignment - 1)) != 0) {
    platform::memory_check_failed("valid task storage alignment", __FILE__, __LINE__);
  }
  if (alignment < alignof(Allocation))
    alignment = alignof(Allocation);
  auto& current = producer();
  if (current.block == nullptr)
    current.block = fresh_block();
  for (;;) {
    auto* block = current.block;
    const auto alignment_mask = std::uintptr_t{alignment - 1};
    const auto address = (block->next + alignment_mask) & ~alignment_mask;
    const auto start = reinterpret_cast<std::uintptr_t>(block);
    // Equivalent fit comparison without overflowing a caller-sized addition.
    if (address <= start + block_bytes && bytes <= start + block_bytes - address) {
      block->next = address + bytes + sizeof(Allocation);
      ++block->allocated;
      auto* storage = reinterpret_cast<std::byte*>(block) + (address - start);
      auto* header = reinterpret_cast<Allocation*>(storage - sizeof(Allocation));
      new (header)
          Allocation{static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(header) - start),
                     static_cast<std::uint32_t>(bytes)};
      return storage;
    }
    // Source no-oversized-block check on the cold path (line478). Subtractions
    // express the same bound without allowing unsigned size addition to wrap.
    constexpr auto header_bytes = sizeof(Block) + sizeof(Allocation);
    if (bytes > block_bytes - header_bytes || alignment > block_bytes - header_bytes - bytes) {
      platform::memory_check_failed("task fits source fixed block", __FILE__, __LINE__);
    }
    // Source closes at exhaustion once; outstanding consumers keep the old
    // block alive while producer switches to new storage.
    block->next = start + block_bytes;
    const auto excess = initial_count - block->allocated;
    if (block->live.fetch_sub(excess, std::memory_order_acq_rel) == excess) {
      block->~Block();
      current.block = construct_block(block);
    } else {
      current.block = fresh_block();
    }
  }
}

// TLinearAllocatorBase::Free non-fast path, ConcurrentLinearAllocator.h:530-546:
// recover owning block from header offset; cross-thread last release reclaims
// only closed blocks (an active producer retains the sentinel reservation).
void release_task_storage(void* pointer) {
  if (pointer == nullptr)
    return;
  auto* header =
      reinterpret_cast<Allocation*>(static_cast<std::byte*>(pointer) - sizeof(Allocation));
  auto* block =
      reinterpret_cast<Block*>(reinterpret_cast<std::byte*>(header) - header->block_offset);
  if (block->live.fetch_sub(1, std::memory_order_acq_rel) == 1)
    cache_block(block);
}
} // namespace bw::tasks
