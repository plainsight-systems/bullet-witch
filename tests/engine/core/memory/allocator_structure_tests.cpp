#include "binned/binned_bit_tree.hpp"
#include "binned/binned_size_classes.hpp"
#include "binned/binned_thread_cache.hpp"
#include "bw/concurrency/word_mutex.hpp"
#include "bw/platform/memory/allocator_sync.hpp"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <limits>
#include <thread>
#include <vector>

namespace {
void require(bool condition, const char* message) {
  if (!condition) {
    std::fprintf(stderr, "%s\n", message);
    std::exit(1);
  }
}

void bit_tree_contract() {
  constexpr std::uint32_t capacity = 4097;
  const auto bytes = bw::memory::BinnedBitTree::memory_requirement(capacity);
  std::vector<std::uint64_t> storage((bytes + 7U) / 8U);
  bw::memory::BinnedBitTree tree;
  tree.initialize(capacity, storage.data(), bytes, false);
  for (std::uint32_t index = 0; index < capacity; ++index) {
    require(tree.allocate() == index, "tree must allocate lowest free valid index");
  }
  require(tree.allocate() == std::numeric_limits<std::uint32_t>::max(),
          "tree must never expose capacity padding");
  tree.release(63);
  tree.release(4096);
  require(tree.next_available() == 63, "release must update summaries");
  require(tree.next_available(64) == 4096, "bounded search must cross summary rows");
  require(tree.count_occupied(capacity) == capacity - 2U, "count excludes padding");
  tree.allocate(63);
  require(tree.is_allocated(63), "explicit allocation marks leaf");
  require(tree.allocate() == 4096, "last partial leaf remains reachable");
  tree.initialize(capacity, storage.data(), bytes, true);
  require(tree.next_available() == std::numeric_limits<std::uint32_t>::max(),
          "occupied initialization exposes no free block");
}

void size_class_contract() {
  for (const std::uint32_t page : {4096U, 16384U}) {
    const bw::memory::BinnedSizeClasses sizes(page);
    require(sizes.at(0).bytes == 8, "source includes eight-byte class");
    require(sizes.at(76).bytes == 131072, "Mac small-class maximum is 128 KiB");
    require(sizes.quantize(0, 0) == 8, "zero-sized request uses minimum class");
    require(sizes.quantize(17, 0) == 32, "small capacity follows listed class");
    require(sizes.quantize(4097, 0) == 4768, "classes are not powers of two");
    require(sizes.quantize(30000, 0) == 32768, "extended classes have 4 KiB spacing");
    require(sizes.quantize(17, 64) == 64, "alignment promotes to natural class");
    require(sizes.quantize(257, 256) == 512, "promotion respects size and alignment");
    require(sizes.quantize(1000, 512) == page, "above 256 alignment uses large path");
    require(sizes.at(sizes.index_for(736)).pages_per_block == (page == 4096 ? 2U : 1U),
            "preferred block multiple converts to native pages");
    require(sizes.at(sizes.index_for(1536)).pages_per_block == 3U,
            "three-base-page class retains divisible native block multiple");
    std::size_t request = 17;
    std::uint32_t alignment = 64;
    require(sizes.promote(request, alignment), "small aligned request promotes");
    require(request == 64 && alignment == 16, "promotion writes source request state");
  }
}

void bundle_contract() {
  std::array<bw::memory::BinnedBundleNode, 129> nodes{};
  bw::memory::BinnedFreeList list;
  for (std::size_t index = 0; index < 128; ++index) {
    require(list.push(&nodes[index], 8), "two bundles must retain 128 small slots");
  }
  require(!list.push(&nodes.back(), 8), "full partial/full pair rejects overflow");
  require(list.pop() == &nodes[127], "partial bundle is LIFO");
  bw::memory::BinnedRecycler recycler;
  require(list.recycle_full(0, recycler) == nullptr, "available recycler accepts full bundle");
  auto* bundle = recycler.pop(0);
  require(bundle != nullptr && bundle->count == 64, "recycler preserves whole bundle count");
  std::uint32_t recycled_count = 0;
  for (auto* node = bundle; node != nullptr; node = node->next()) {
    ++recycled_count;
  }
  require(recycled_count == 64, "recycler preserves all nodes");
  auto* remainder = list.detach_bundles();
  std::uint32_t remaining_count = 0;
  for (auto* node = remainder; node != nullptr; node = node->next()) {
    ++remaining_count;
  }
  require(remaining_count == 63 && list.pop() == nullptr, "detach transfers remaining ownership");

  std::array<bw::memory::BinnedBundleNode, 9> bundles{};
  for (std::size_t index = 0; index < 8; ++index) {
    require(recycler.push(1, &bundles[index]), "eight recycler slots are available");
  }
  require(!recycler.push(1, &bundles[8]), "ninth bundle must return to allocator");
  std::array<bool, 8> seen{};
  for (std::size_t index = 0; index < 8; ++index) {
    auto* node = recycler.pop(1);
    require(node != nullptr, "stored recycler bundle must be returned");
    const auto offset = static_cast<std::size_t>(node - bundles.data());
    require(offset < 8 && !seen[offset], "bundle ownership transfers exactly once");
    seen[offset] = true;
  }
  require(recycler.pop(1) == nullptr, "drained recycler is empty");
}

void synchronization_contract() {
  bw::platform::AllocatorRecursiveMutex recursive;
  recursive.lock();
  require(recursive.try_lock(), "same-thread recursion succeeds");
  recursive.unlock();
  bool other_acquired = true;
  std::thread competitor([&] {
    other_acquired = recursive.try_lock();
    if (other_acquired) {
      recursive.unlock();
    }
  });
  competitor.join();
  require(!other_acquired, "inner unlock must not release outer ownership");
  recursive.unlock();

  bw::platform::AllocatorManualResetEvent event;
  event.notify();
  event.wait(); // Notification before wait must remain visible.
  event.reset();
  std::atomic<bool> completed{false};
  std::thread waiter([&] {
    event.wait();
    completed.store(true);
  });
  event.notify();
  waiter.join();
  require(completed.load(), "reset event wakes on subsequent notification");

  bw::concurrency::WordMutex mutex;
  std::uint32_t counter = 0;
  std::array<std::thread, 4> threads;
  for (auto& thread : threads) {
    thread = std::thread([&] {
      for (std::uint32_t iteration = 0; iteration < 1000; ++iteration) {
        mutex.lock();
        ++counter;
        mutex.unlock();
      }
    });
  }
  for (auto& thread : threads) {
    thread.join();
  }
  require(counter == 4000 && !mutex.is_locked(), "word mutex protects shared updates");
}
} // namespace

int main() try {
  bit_tree_contract();
  size_class_contract();
  bundle_contract();
  synchronization_contract();
} catch (const std::exception& error) {
  std::fprintf(stderr, "Test infrastructure failure: %s\n", error.what());
  return 1;
}
