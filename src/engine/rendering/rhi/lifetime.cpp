#include "bw/rendering/rhi/lifetime.hpp"

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <utility>
#include <vector>

namespace bw::rhi {

void Resource::add_ref() const noexcept { refs_.fetch_add(1U, std::memory_order_relaxed); }

// The release that takes the count to zero pushes the resource into the
// queue. The decrement is acq_rel (CONC.2) so the thread that destroys the
// object sees every write made before any release.
void Resource::release() const noexcept {
  if (refs_.fetch_sub(1U, std::memory_order_acq_rel) == 1U) {
    queue_->enqueue(this);
  }
}

std::uint32_t Resource::ref_count() const noexcept { return refs_.load(std::memory_order_acquire); }

DeferredDeleteQueue::~DeferredDeleteQueue() {
  // A non-empty queue here is a programming error (lifetime.hpp); the
  // remaining entries are destroyed now so nothing leaks.
  static_cast<void>(drain());
}

void DeferredDeleteQueue::set_current_frame(std::uint64_t frame) noexcept {
  const std::scoped_lock lock(mutex_);
  if (frame > current_frame_) {
    current_frame_ = frame;
  }
}

std::uint64_t DeferredDeleteQueue::current_frame() const noexcept {
  const std::scoped_lock lock(mutex_);
  return current_frame_;
}

// The append may allocate; an allocation failure on the release path cannot
// be recovered from, and inside this noexcept function it terminates.
// NOLINTNEXTLINE(bugprone-exception-escape)
void DeferredDeleteQueue::enqueue(const Resource* resource) noexcept {
  const std::scoped_lock lock(mutex_);
  entries_.push_back(Entry{resource, current_frame_});
}

// The pending entries are taken out of the queue, then destroyed outside it:
// the prefix is spliced out under the mutex and destroyed after it is
// released, because a destructor may release the last Ref to another resource
// and enqueue it (a view holds its resource). Entries enqueued by those
// destructors are tagged with the current frame and are destroyed by this
// call too when that tag is at or below completed_frame, so the loop runs
// until no entry at or below it remains. Copying the prefix out may allocate;
// as for enqueue, an allocation failure here terminates. Destruction is in
// enqueue order (lifetime.hpp).
// NOLINTNEXTLINE(bugprone-exception-escape)
std::size_t DeferredDeleteQueue::destroy_completed(std::uint64_t completed_frame) noexcept {
  std::size_t destroyed = 0;
  std::vector<Entry> batch;
  for (;;) {
    {
      const std::scoped_lock lock(mutex_);
      std::size_t count = 0;
      while (count < entries_.size() && entries_[count].frame <= completed_frame) {
        ++count;
      }
      if (count == 0) {
        break;
      }
      const auto end = entries_.begin() + static_cast<std::ptrdiff_t>(count);
      batch.assign(entries_.begin(), end);
      entries_.erase(entries_.begin(), end);
    }
    for (const Entry& entry : batch) {
      delete entry.resource;
      ++destroyed;
    }
    batch.clear();
  }
  return destroyed;
}

std::size_t DeferredDeleteQueue::drain() noexcept {
  std::size_t destroyed = 0;
  std::vector<Entry> batch;
  for (;;) {
    {
      const std::scoped_lock lock(mutex_);
      if (entries_.empty()) {
        break;
      }
      batch.swap(entries_);
    }
    for (const Entry& entry : batch) {
      delete entry.resource;
      ++destroyed;
    }
    batch.clear();
  }
  return destroyed;
}

std::size_t DeferredDeleteQueue::pending() const noexcept {
  const std::scoped_lock lock(mutex_);
  return entries_.size();
}

} // namespace bw::rhi
