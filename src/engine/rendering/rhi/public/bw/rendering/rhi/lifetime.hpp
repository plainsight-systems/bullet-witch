#ifndef BW_RENDERING_RHI_LIFETIME_HPP
#define BW_RENDERING_RHI_LIFETIME_HPP

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <type_traits>
#include <utility>
#include <vector>

// Responsibility: resource lifetime (docs/architecture/rhi.md, invariant 4
// and "Resource lifetime"): intrusive reference counting on every RHI
// object, a deferred delete queue that destroys an object only after the GPU
// has completed the frame that last referenced it, and the frames-in-flight
// arithmetic of invariant 8. The device (device.hpp) owns one queue and one
// frame counter; backends construct their objects against that queue.
//
// Contract.
//   Resource: add_ref and release are thread-safe and never block. The
//   release that takes the count to zero enqueues the object on its queue,
//   tagged with the queue's current frame; the object is destroyed by the
//   queue's destroy_completed, on the thread that calls it, through the
//   virtual destructor. A resource is never deleted directly. A resource
//   outlives every Ref to it and every view that holds it (views hold a Ref).
//   DeferredDeleteQueue: destroy_completed(frame) destroys every entry tagged
//   at or below frame, in enqueue order, and returns how many; entries tagged
//   above frame are untouched. Destruction order between entries of one frame
//   is enqueue order, so it does not depend on which Ref was dropped last.
//   (Departure from the pin's queue, which is a LIFO stack drained with
//   ConsumeAllLifo, RHIResources.cpp:107, 113: this order is the one a test
//   can state.)
//   drain(): destroys everything, for device shutdown after the GPU is idle.
//   The queue is destroyed empty; a non-empty queue at destruction is a
//   programming error and the remaining entries are destroyed then.
//   required_completion: the frame the CPU must wait for before frame next
//   may be submitted, or no value. With N frames in flight and C the last
//   completed frame, frames C+1..next-1 are queued; the wait is required
//   exactly when next - C - 1 >= N, and then for frame next - N. Frames are
//   numbered from 1; completed starts at 0.
//
// Algorithm: the queue is one vector of (frame, Resource*) pairs under a
// mutex; enqueue appends, destroy_completed removes the prefix at or below
// the frame (entries are appended with non-decreasing frames because the
// current frame only advances). Work is O(entries) per call.
//
// Design and cost: a release is one atomic decrement and, at zero, one
// mutex-guarded append; no release blocks on the GPU. The queue is the only
// place a resource is deleted, which is what lets a view, a command list in
// flight and a pipeline share one lifetime rule.
//
// Core guidelines read: C.35 (Resource's destructor is protected and
// virtual: deletion only through the queue, through the base), C.67 (a
// polymorphic class suppresses copy), I.11 and C.32 (Ref is the owning
// pointer; a raw Resource* is an observer; the queue's pointers are owning
// and documented as such), CP.3 and CP.100 (one mutex around the queue;
// the only lock-free operation is the standard atomic count). Performance
// guidelines read: GPU.7 (bounded frames in flight with a fence per frame,
// not a device wait), MEM.4 (per-frame lifetimes released at one flip
// point), CONC.2 (the count uses acq_rel on the decrement so the destructor
// sees every prior write), LIFE.7 (a destroyed object is never touched
// again: the queue holds the last pointer).
namespace bw::rhi {

class DeferredDeleteQueue;

// Unreal f45729c0b6f097038e72b52d366a62baeca3275b
// Engine/Source/Runtime/RHI/Public/RHIResources.h:53-109: FRHIResource with a
// protected virtual destructor ("should only be destructed via the deletion
// queue"), AddRef, Release (at zero, MarkForDelete), GetRefCount;
// Engine/Source/Runtime/RHI/Private/RHIResources.cpp:46-59: MarkForDelete
// pushes the resource into the pending-delete queue. Followed. Omitted: the
// packed FAtomicFlags with MarkedForDelete and Deleting bits
// (RHIResources.h:141-220) and the resurrection check in Deleting()
// (:184-203, "caches can bring dead objects back to life"): no cache here
// holds a raw pointer to a resource, every holder owns a Ref, so a count at
// zero is final; the lifetime-extension queue (RHIResources.cpp:12-13,
// 81-100), which serves render-thread scopes the game does not have.
class Resource {
public:
  Resource(const Resource&) = delete;
  Resource& operator=(const Resource&) = delete;

  void add_ref() const noexcept;
  // Never blocks. At zero, enqueues *this on its queue; see the contract.
  void release() const noexcept;
  [[nodiscard]] std::uint32_t ref_count() const noexcept;
  [[nodiscard]] DeferredDeleteQueue& delete_queue() const noexcept { return *queue_; }

protected:
  // Starts with a count of zero; the first Ref takes it to one.
  explicit Resource(DeferredDeleteQueue& queue) noexcept : queue_(&queue) {}
  virtual ~Resource() = default;

private:
  friend class DeferredDeleteQueue;
  mutable std::atomic<std::uint32_t> refs_{0};
  DeferredDeleteQueue* queue_;
};

// Unreal f45729c0b6f097038e72b52d366a62baeca3275b
// Engine/Source/Runtime/Core/Public/Templates/RefCounting.h:289:
// TRefCountPtr, the owning pointer of every RHI resource
// (Engine/Source/Runtime/RHI/Public/RHIFwd.h:103-133 names the typed refs).
// Followed: copy adds a reference, destruction releases one, a null Ref owns
// nothing.
template <class T> class Ref {
public:
  constexpr Ref() noexcept = default;
  constexpr Ref(std::nullptr_t) noexcept {} // NOLINT(google-explicit-constructor)
  // Takes a reference on p; p may be null.
  explicit Ref(T* p) noexcept : ptr_(p) {
    if (ptr_ != nullptr) {
      ptr_->add_ref();
    }
  }
  Ref(const Ref& other) noexcept : Ref(other.ptr_) {}
  Ref(Ref&& other) noexcept : ptr_(std::exchange(other.ptr_, nullptr)) {}
  template <class U>
    requires std::is_convertible_v<U*, T*>
  Ref(const Ref<U>& other) noexcept : Ref(other.get()) {} // NOLINT(google-explicit-constructor)
  ~Ref() { reset(); }

  // Copy-and-swap by value serves copy and move assignment; self-assignment
  // takes a reference before it drops one.
  Ref& operator=(Ref other) noexcept {
    swap(other);
    return *this;
  }

  void reset() noexcept {
    if (ptr_ != nullptr) {
      ptr_->release();
      ptr_ = nullptr;
    }
  }
  void swap(Ref& other) noexcept { std::swap(ptr_, other.ptr_); }

  [[nodiscard]] T* get() const noexcept { return ptr_; }
  [[nodiscard]] T* operator->() const noexcept { return ptr_; }
  [[nodiscard]] T& operator*() const noexcept { return *ptr_; }
  [[nodiscard]] explicit operator bool() const noexcept { return ptr_ != nullptr; }
  [[nodiscard]] friend bool operator==(const Ref& a, const Ref& b) noexcept {
    return a.ptr_ == b.ptr_;
  }

private:
  T* ptr_{nullptr};
};

// Unreal f45729c0b6f097038e72b52d366a62baeca3275b
// Engine/Source/Runtime/RHI/Private/RHIResources.cpp:61-77 and 102-117:
// GatherResourcesToDelete consumes the pending queue and DeleteResources runs
// the destructors; Engine/Source/Runtime/RHI/Private/RHICommandList.cpp:1414-1464:
// Submit_Close runs the gather and delete before the frame-end call, so a
// deletion happens at the frame boundary after the GPU work that could
// reference it. Followed, with the frame tag made explicit: the pin's Metal
// backend ties deletion to GPU completion with an end-of-pipe task
// (Engine/Source/Runtime/Apple/MetalRHI/Private/MetalRHI.cpp:1545-1629,
// 1631-1664); here the tag plus the completed frame value is that gate on
// every backend. Omitted: the lock-free MPMC queue
// (Core/Public/Containers/ConsumeAllMpmcQueue.h:25) in favor of one mutex
// (CP.100), and the extended-lifetime queue (see Resource).
class DeferredDeleteQueue {
public:
  DeferredDeleteQueue() = default;
  DeferredDeleteQueue(const DeferredDeleteQueue&) = delete;
  DeferredDeleteQueue& operator=(const DeferredDeleteQueue&) = delete;
  ~DeferredDeleteQueue();

  // The frame new entries are tagged with. Set by the device at each frame
  // boundary; never decreases.
  void set_current_frame(std::uint64_t frame) noexcept;
  [[nodiscard]] std::uint64_t current_frame() const noexcept;

  // Called by Resource::release at zero. Takes ownership of the object.
  void enqueue(const Resource* resource) noexcept;

  // Destroys every entry tagged at or below completed_frame, in enqueue
  // order; returns the number destroyed.
  std::size_t destroy_completed(std::uint64_t completed_frame) noexcept;
  // Destroys every entry; for shutdown after the GPU is idle.
  std::size_t drain() noexcept;
  [[nodiscard]] std::size_t pending() const noexcept;

private:
  struct Entry {
    const Resource* resource{};
    std::uint64_t frame{};
  };
  mutable std::mutex mutex_{};
  std::vector<Entry> entries_{};
  std::uint64_t current_frame_{1};
};

// Frames in flight.
// Unreal f45729c0b6f097038e72b52d366a62baeca3275b
// Engine/Source/Runtime/Apple/MetalRHI/Private/MetalDevice.cpp:290: the
// frame semaphore is created with a count of 3 (1 with -gpulockstep);
// Engine/Source/Runtime/Apple/MetalRHI/Private/MetalViewport.cpp:1106:
// dispatch_semaphore_wait blocks the CPU when that many frames are queued.
// Followed as arithmetic over frame numbers so the bound is the same on
// every backend and testable without a GPU.
inline constexpr std::uint32_t kDefaultFramesInFlight = 3;
inline constexpr std::uint32_t kMaxFramesInFlight = 3;

// Where the frame counters stand: the frame about to be submitted and the
// last one the GPU completed.
struct FrameProgress {
  std::uint64_t next{1};
  std::uint64_t completed{};
};

// The frame the CPU must wait for before submitting progress.next, or no
// value when fewer than frames_in_flight frames are queued. Preconditions:
// next >= 1, completed < next, frames_in_flight >= 1.
[[nodiscard]] constexpr std::optional<std::uint64_t>
required_completion(FrameProgress progress, std::uint32_t frames_in_flight) noexcept {
  const std::uint64_t queued = progress.next - progress.completed - 1U;
  if (queued >= frames_in_flight) {
    return progress.next - frames_in_flight;
  }
  return std::nullopt;
}

} // namespace bw::rhi
#endif
