#include "core/tasks/task_queue.hpp"

#include "core/memory/allocator.hpp"
#include "platform/memory_failure.hpp"
#include "platform/task_dispatch.hpp"

#include <new>

// Implementation guidance read: CP.2/R.15 and CONC.2/MEM.2. Publication uses
// source acquire/CAS edges; page storage survives recycled links and ABA readers.
namespace bw::tasks {
namespace {
// Unreal f45729c0b6f097038e72b52d366a62baeca3275b LockFreeList.h:55-261:
// 26-bit indices, 16,384 entries/page; zero is null, upper bits are ABA counter.
constexpr std::uint32_t index_bits = 26;
constexpr std::uint32_t index_limit = 1U << index_bits;
constexpr std::uint32_t index_mask = index_limit - 1;
constexpr std::uint32_t page_items = 16384;
constexpr std::uint32_t bundle_items = 64;
struct Link final {
  std::atomic<std::uint64_t> next{0};
  std::atomic<void*> payload{nullptr};
  std::atomic<std::uint32_t> single_next{0};
};
struct LinkPages final {
  alignas(platform::allocator_cache_line_size) std::atomic<std::uint32_t> next{1};
  alignas(platform::allocator_cache_line_size)
      std::array<std::atomic<Link*>, index_limit / page_items> pages{};
};
LinkPages pages;
std::atomic<std::uint64_t> shared_bundles{0};

// Extracted FIndexedPointer methods, LockFreeList.h:136-221: retain the pointer
// bits separately from a monotonically advancing counter; wrap gets source1ms
// precaution (LockFreeList.cpp:42-46), not a bespoke reclamation policy.
std::uint32_t index_of(std::uint64_t value) {
  return static_cast<std::uint32_t>(value & index_mask);
}
// Source tagged counter and index are deliberately distinct integer widths.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
std::uint64_t advanced(std::uint64_t previous, std::uint32_t index) {
  const std::uint64_t next = (previous & ~std::uint64_t{index_mask}) + index_limit;
  if (next == 0) {
    platform::pause_after_task_tag_overflow();
  }
  return next | index;
}
bool replace(std::atomic<std::uint64_t>& target, std::uint64_t previous, std::uint64_t next) {
  return target.compare_exchange_strong(previous, next, std::memory_order_acq_rel,
                                        std::memory_order_relaxed);
}

// TLockFreeAllocOnceIndexedAllocator::GetItem, LockFreeList.h:88-104. No link
// backing page is reclaimed; an outstanding consumer can inspect recycled tags.
Link& link(std::uint32_t index) {
  return pages.pages[index / page_items].load(std::memory_order_acquire)[index % page_items];
}

// Alloc/GetRawItem, LockFreeList.h:73-128. Reserve indices atomically, publish
// backing page by CAS, free a losing page, construct only the reserved links.
std::uint32_t reserve_bundle() {
  const auto first = pages.next.fetch_add(bundle_items, std::memory_order_relaxed);
  if (first > index_limit - bundle_items) {
    platform::memory_check_failed("lock-free link index capacity", __FILE__, __LINE__);
  }
  for (std::uint32_t index = first; index < first + bundle_items; ++index) {
    auto& page = pages.pages[index / page_items];
    if (page.load(std::memory_order_acquire) == nullptr) {
      auto* fresh = static_cast<Link*>(memory::allocate(page_items * sizeof(Link)));
      Link* empty = nullptr;
      if (!page.compare_exchange_strong(empty, fresh, std::memory_order_acq_rel,
                                        std::memory_order_acquire)) {
        memory::release(fresh);
      }
    }
    new (&link(index)) Link;
  }
  return first;
}

// FLockFreePointerListLIFORoot::Push/Pop, LockFreeList.h:286-357: bundle heads
// have a separate single-next chain; a successful tagged CAS transfers ownership.
void return_bundle(std::uint32_t item) {
  for (;;) {
    const auto head = shared_bundles.load(std::memory_order_acquire);
    link(item).single_next.store(index_of(head), std::memory_order_relaxed);
    if (replace(shared_bundles, head, advanced(head, item)))
      return;
  }
}
std::uint32_t take_bundle() {
  for (;;) {
    const auto head = shared_bundles.load(std::memory_order_acquire);
    const auto item = index_of(head);
    if (item == 0)
      return 0;
    const auto next = link(item).single_next.load(std::memory_order_relaxed);
    if (replace(shared_bundles, head, advanced(head, next))) {
      link(item).single_next.store(0, std::memory_order_relaxed);
      return item;
    }
  }
}
struct LinkCache final {
  std::uint32_t partial{};
  std::uint32_t full{};
  std::uint32_t count{};
};

// LockFreeLinkAllocator_TLSCache::GetTLS, LockFreeList.cpp:228-239. The source
// allocates this record through FMemory and intentionally retains it at thread
// exit; cached link pages/bundles are process-lifetime storage, not task payloads.
LinkCache& cache() {
  thread_local LinkCache* value = nullptr;
  if (value == nullptr)
    value = new (memory::allocate(sizeof(LinkCache))) LinkCache;
  return *value;
}

// LockFreeLinkAllocator_TLSCache::Pop, LockFreeList.cpp:117-176: partial -> full
// -> shared bundle -> allocate64 indices; payload carries the private free chain.
std::uint32_t take_link() {
  auto& local = cache();
  if (local.partial == 0) {
    local.partial = local.full;
    local.full = 0;
    if (local.partial == 0)
      local.partial = take_bundle();
    if (local.partial == 0) {
      const auto first = reserve_bundle();
      for (std::uint32_t index = first; index < first + bundle_items; ++index) {
        // Source free-list payload holds an encoded index, never dereferenced.
        // NOLINTNEXTLINE(performance-no-int-to-ptr)
        link(index).payload.store(reinterpret_cast<void*>(std::uintptr_t{local.partial}),
                                  std::memory_order_relaxed);
        local.partial = index;
      }
    }
    local.count = bundle_items;
  }
  const auto result = local.partial;
  auto& entry = link(result);
  local.partial = static_cast<std::uint32_t>(
      reinterpret_cast<std::uintptr_t>(entry.payload.load(std::memory_order_relaxed)));
  --local.count;
  entry.payload.store(nullptr, std::memory_order_relaxed);
  return result;
}

// LockFreeLinkAllocator_TLSCache::Push, LockFreeList.cpp:184-216: spill the old
// full bundle globally; recycle link without resetting its ABA counter.
void return_link(std::uint32_t item) {
  auto& local = cache();
  if (local.count >= bundle_items) {
    if (local.full != 0)
      return_bundle(local.full);
    local.full = local.partial;
    local.partial = 0;
    local.count = 0;
  }
  auto& entry = link(item);
  entry.next.store(entry.next.load(std::memory_order_relaxed) & ~std::uint64_t{index_mask},
                   std::memory_order_relaxed);
  entry.single_next.store(0, std::memory_order_relaxed);
  // Source free-list payload holds an encoded index, never dereferenced.
  // NOLINTNEXTLINE(performance-no-int-to-ptr)
  entry.payload.store(reinterpret_cast<void*>(std::uintptr_t{local.partial}),
                      std::memory_order_relaxed);
  local.partial = item;
  ++local.count;
}
} // namespace

// FLockFreePointerFIFOBase constructor/destructor, LockFreeList.h:554-572:
// queue starts with a dummy; after draining, return that final dummy to TLS.
TaskQueue::TaskQueue() {
  const auto stub = take_link();
  head_.store(stub, std::memory_order_relaxed);
  tail_.store(stub, std::memory_order_relaxed);
}
TaskQueue::~TaskQueue() {
  while (pop() != nullptr) {
  }
  return_link(index_of(head_.load(std::memory_order_relaxed)));
}

// FLockFreePointerFIFOBase::Push, LockFreeList.h:574-617: initialize private
// link, recheck tail, help a lagging tail or publish next, then advance tail.
void TaskQueue::push(void* payload) {
  if (payload == nullptr) {
    platform::memory_check_failed("non-null task queue payload", __FILE__, __LINE__);
  }
  const auto item = take_link();
  link(item).payload.store(payload, std::memory_order_relaxed);
  std::uint64_t tail = 0;
  for (;;) {
    tail = tail_.load(std::memory_order_acquire);
    auto& entry = link(index_of(tail));
    const auto next = entry.next.load(std::memory_order_acquire);
    if (tail_.load(std::memory_order_acquire) != tail)
      continue;
    if (index_of(next) != 0) {
      replace(tail_, tail, advanced(tail, index_of(next)));
    } else if (replace(entry.next, next, advanced(next, item))) {
      break;
    }
  }
  replace(tail_, tail, advanced(tail, item));
}

// FLockFreePointerFIFOBase::Pop, LockFreeList.h:619-663: inspect consistent
// head/tail/next, help tail when behind, acquire payload by winning head CAS,
// then recycle the old dummy (never the new head still reachable by producers).
void* TaskQueue::pop() {
  for (;;) {
    const auto head = head_.load(std::memory_order_acquire);
    const auto tail = tail_.load(std::memory_order_acquire);
    const auto next = link(index_of(head)).next.load(std::memory_order_acquire);
    if (head_.load(std::memory_order_acquire) != head)
      continue;
    if (index_of(head) == index_of(tail)) {
      if (index_of(next) == 0)
        return nullptr;
      replace(tail_, tail, advanced(tail, index_of(next)));
    } else {
      void* payload = link(index_of(next)).payload.load(std::memory_order_relaxed);
      if (replace(head_, head, advanced(head, index_of(next)))) {
        return_link(index_of(head));
        return payload;
      }
    }
  }
}

// FStallingTaskQueue::Push, LockFreeList.h:702-752. Current owner-pump extraction
// never marks a sleeping bit; preserve tagged publication after FIFO insertion.
void NamedTaskQueue::push(void* payload, bool high_priority) {
  auto state = state_.load(std::memory_order_acquire);
  priorities_[high_priority ? 0 : 1].push(payload);
  while (!replace(state_, state, advanced(state, index_of(state)))) {
    state = state_.load(std::memory_order_acquire);
  }
}

// FStallingTaskQueue::Pop(false), LockFreeList.h:755-802: inspect priorities in
// order, publish successful pop through master-state CAS, return on empty.
void* NamedTaskQueue::pop() {
  auto state = state_.load(std::memory_order_acquire);
  for (auto& queue : priorities_) {
    if (void* payload = queue.pop()) {
      while (!replace(state_, state, advanced(state, index_of(state)))) {
        state = state_.load(std::memory_order_acquire);
      }
      return payload;
    }
  }
  return nullptr;
}
} // namespace bw::tasks
