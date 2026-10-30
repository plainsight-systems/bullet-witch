#include "binned/binned_thread_cache.hpp"
#include "allocator_checks.hpp"
#include "binned/binned_allocator.hpp"

#include <new>

// Core CP.2/C.32: cache owner holds its WordMutex, CAS transfers whole bundles.
// Performance MEM.2/MEM.5: source intrusive slots, per-class caching; no heap
// fallback. All references use Unreal f45729c0b6f097038e72b52d366a62baeca3275b.
namespace bw::memory {
// MallocBinnedCommon.h:186-202: pack a 48-bit link beside bundle count in8bytes.
void BinnedBundleNode::set_next(BinnedBundleNode* node) {
  next_address = reinterpret_cast<std::uintptr_t>(node);
}
BinnedBundleNode* BinnedBundleNode::next() const {
  // Pointer restoration is the inspected source packed-node representation.
  // NOLINTNEXTLINE(performance-no-int-to-ptr)
  return reinterpret_cast<BinnedBundleNode*>(static_cast<std::uintptr_t>(next_address));
}

// MallocBinnedCommonUtils.h:45-95 TGlobalRecycler: initially empty atomic slots.
BinnedRecycler::BinnedRecycler() = default;
// PushBundle: scan eight slots and transfer ownership with null-to-bundle CAS.
bool BinnedRecycler::push(std::uint32_t pool, BinnedBundleNode* bundle) {
  for (auto& slot : pools_[pool].bundles) {
    BinnedBundleNode* empty = nullptr;
    if (slot.compare_exchange_strong(empty, bundle, std::memory_order_seq_cst)) {
      return true;
    }
  }
  return false;
}
// PopBundle: only the successful exchange owns and may follow the bundle links.
BinnedBundleNode* BinnedRecycler::pop(std::uint32_t pool) {
  for (auto& slot : pools_[pool].bundles) {
    auto* bundle = slot.load(std::memory_order_relaxed);
    if (bundle != nullptr &&
        slot.compare_exchange_strong(bundle, nullptr, std::memory_order_seq_cst)) {
      return bundle;
    }
  }
  return nullptr;
}

// MallocBinnedCommon.h:265-305 FBundle/FFreeBlockList: both local bundles empty.
BinnedFreeList::BinnedFreeList() = default;
// .h:306-322 PushToFront: roll full partial bundle once, then reject overflow.
bool BinnedFreeList::push(void* slot, std::uint32_t bin_size) {
  if (partial_.count >= 64U || partial_.count * bin_size >= 65536U) {
    if (full_.head != nullptr) {
      return false;
    }
    full_ = partial_;
    partial_ = {};
  }
  auto* node = ::new (slot) BinnedBundleNode{};
  node->set_next(partial_.head);
  partial_.head = node;
  ++partial_.count;
  return true;
}
// .h:327-334 PopFromFront: roll full into empty partial, then pop local head.
void* BinnedFreeList::pop() {
  if (partial_.head == nullptr && full_.head != nullptr) {
    partial_ = full_;
    full_ = {};
  }
  auto* node = partial_.head;
  if (node != nullptr) {
    partial_.head = node->next();
    --partial_.count;
  }
  return node;
}
// .h:335-350 RecyleFull: encode count for receiving thread; failed transfer is
// returned to the class pool by caller, never retained as a hidden third bundle.
BinnedBundleNode* BinnedFreeList::recycle_full(std::uint32_t pool, BinnedRecycler& recycler) {
  auto* result = full_.head;
  if (result != nullptr) {
    result->count = full_.count;
    if (recycler.push(pool, result)) {
      result = nullptr;
    }
    full_ = {};
  }
  return result;
}
// .h:352-367 ObtainPartial: only empty partial takes a global recycled bundle.
bool BinnedFreeList::obtain_partial(std::uint32_t pool, BinnedRecycler& recycler) {
  if (partial_.head == nullptr) {
    partial_.head = recycler.pop(pool);
    partial_.count =
        partial_.head == nullptr ? 0U : static_cast<std::uint32_t>(partial_.head->count);
  }
  return partial_.head != nullptr;
}
// .h:369-401 PopBundles: concatenate detached partial/full chains for pool return.
BinnedBundleNode* BinnedFreeList::detach_bundles() {
  auto* result = partial_.head;
  if (result == nullptr) {
    result = full_.head;
  } else {
    auto* tail = result;
    while (tail->next() != nullptr) {
      tail = tail->next();
    }
    tail->set_next(full_.head);
  }
  partial_ = {};
  full_ = {};
  return result;
}

thread_local BinnedThreadCache* BinnedThreadCache::current_ = nullptr;
// .h:500-643 FPerThreadFreeBlockLists: explicit thread lifetime, not TLS destructor.
BinnedThreadCache::BinnedThreadCache() = default;
// Get: owner must hold the cache lock; waits can allocate after relinquishing it.
BinnedThreadCache* BinnedThreadCache::current_if_owned() {
  return current_ != nullptr && current_->locked_by_owner_ ? current_ : nullptr;
}
// SetTLS: VM metadata construction then publish/lock before registry can allocate.
void BinnedThreadCache::setup(BinnedAllocator& allocator) {
  if (current_ == nullptr) {
    current_ =
        ::new (BinnedAllocator::allocate_metadata(sizeof(BinnedThreadCache))) BinnedThreadCache;
    current_->locked_by_owner_ = true;
    current_->mutex_.lock();
    allocator.register_cache(*current_);
  }
}
// .h:786-795 and ClearTLS: flush, unregister while alive, unlock/destroy/unmap.
void BinnedThreadCache::clear(BinnedAllocator& allocator) {
  if (current_ != nullptr) {
    allocator.flush_current_thread_cache();
    allocator.unregister_cache(*current_);
    current_->locked_by_owner_ = false;
    current_->mutex_.unlock();
    current_->~BinnedThreadCache();
    BinnedAllocator::release_metadata(current_, sizeof(BinnedThreadCache));
    current_ = nullptr;
  }
}
// LockTLS: reacquire before marking local ownership available to allocation.
void BinnedThreadCache::mark_used() {
  if (current_ != nullptr) {
    current_->mutex_.lock();
    current_->locked_by_owner_ = true;
  }
}
// .h:809-821: satisfy pending trim epoch before relinquishing the cache lock.
void BinnedThreadCache::mark_unused(BinnedAllocator& allocator) {
  if (current_ != nullptr) {
    allocator.flush_current_thread_cache(true);
    current_->locked_by_owner_ = false;
    current_->mutex_.unlock();
  }
}
// .h:575-582 Malloc/Free forward to exclusively owned per-class list.
void* BinnedThreadCache::allocate(std::uint32_t pool) { return lists_[pool].pop(); }
bool BinnedThreadCache::release(void* slot, std::uint32_t pool, std::uint32_t size) {
  return lists_[pool].push(slot, size);
}
// .h:590-609: forwarding retains bundle ownership transfer in the list/recycler.
BinnedBundleNode* BinnedThreadCache::recycle_full(std::uint32_t pool, BinnedRecycler& recycler) {
  return lists_[pool].recycle_full(pool, recycler);
}
bool BinnedThreadCache::obtain_partial(std::uint32_t pool, BinnedRecycler& recycler) {
  return lists_[pool].obtain_partial(pool, recycler);
}
BinnedBundleNode* BinnedThreadCache::detach_bundles(std::uint32_t pool) {
  return lists_[pool].detach_bundles();
}
// .h:626-639 UpdateEpoch: called with cache owned, suppress already handled epoch.
bool BinnedThreadCache::update_epoch(std::uint64_t epoch) {
  if (trim_epoch_ >= epoch) {
    return false;
  }
  trim_epoch_ = epoch;
  return true;
}
} // namespace bw::memory
