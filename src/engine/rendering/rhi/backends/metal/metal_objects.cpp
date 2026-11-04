#include "metal_objects.hpp"

#include <atomic>
#include <cstdint>
#include <cstring>
#include <limits>

namespace bw::rhi::metal {

// Destruction runs inside the delete queue after the GPU completed every
// frame that named the object (lifetime.hpp), so the Metal object is released
// and its heap range returned at once.
MetalBuffer::~MetalBuffer() {
  buffer_->release();
  memory_->free(placement_);
}

// A back buffer owns neither a texture nor a heap range.
MetalTexture::~MetalTexture() {
  if (memory_ == nullptr) {
    return;
  }
  texture_->release();
  memory_->free(placement_);
}

MetalShaderResourceView::~MetalShaderResourceView() {
  if (texture_view_ != nullptr) {
    texture_view_->release();
  }
}

MetalUnorderedAccessView::~MetalUnorderedAccessView() {
  writable_->remove(*written_);
  if (texture_view_ != nullptr) {
    texture_view_->release();
  }
}

MetalSampler::~MetalSampler() { state_->release(); }

MetalDepthStencilState::~MetalDepthStencilState() { state_->release(); }

MetalGraphicsPipeline::~MetalGraphicsPipeline() { state_->release(); }

MetalComputePipeline::~MetalComputePipeline() { state_->release(); }

MetalStagingBuffer::~MetalStagingBuffer() {
  buffer_->release();
  memory_->free(placement_);
}

MetalQueryPool::~MetalQueryPool() {
  results_->release();
  memory_->free(placement_);
}

MetalGpuFence::~MetalGpuFence() { event_->release(); }

// synchronization.hpp, FenceState: Unwritten until a write is replayed;
// Pending from the replay; Submitted from the end_frame that commits it;
// Signaled once the event has reached the write's value. The event's value
// only grows (each write takes the next value), so a signal from a write
// before reset() never satisfies a later one.
FenceState MetalGpuFence::state() const noexcept {
  const FenceState phase = phase_.load(std::memory_order_acquire);
  if (phase == FenceState::Unwritten) {
    return FenceState::Unwritten;
  }
  if (event_->signaledValue() >= value_.load(std::memory_order_acquire)) {
    return FenceState::Signaled;
  }
  return phase;
}

// Waits with waitUntilSignaledValue and a timeout of 1000000 ms; the wait
// repeats until the value is reached rather than falling back to a sync point,
// so it never returns early and never spins (each call blocks in the event).
void MetalGpuFence::wait() noexcept {
  const std::uint64_t target = value_.load(std::memory_order_acquire);
  constexpr std::uint64_t kWaitMilliseconds = 1000000;
  while (event_->signaledValue() < target) {
    static_cast<void>(event_->waitUntilSignaledValue(target, kWaitMilliseconds));
  }
}

void MetalGpuFence::reset() noexcept {
  phase_.store(FenceState::Unwritten, std::memory_order_release);
}

std::uint64_t MetalGpuFence::begin_write() noexcept {
  const std::uint64_t next = value_.load(std::memory_order_relaxed) + 1U;
  value_.store(next, std::memory_order_release);
  phase_.store(FenceState::Pending, std::memory_order_release);
  return next;
}

void MetalGpuFence::mark_submitted() noexcept {
  FenceState expected = FenceState::Pending;
  static_cast<void>(
      phase_.compare_exchange_strong(expected, FenceState::Submitted, std::memory_order_acq_rel));
}

void MetalQueryPool::mark_ended(std::uint32_t index, std::uint64_t frame) noexcept {
  ended_frame_[index].store(frame, std::memory_order_release);
}

std::uint64_t MetalQueryPool::ended_frame(std::uint32_t index) const noexcept {
  return ended_frame_[index].load(std::memory_order_acquire);
}

// The visibility result buffer holds one 64-bit count per index at index *
// 8 (metal_context.cpp, begin_query). Shared storage: after the frame has
// completed the GPU's writes are visible to the CPU.
std::uint64_t MetalQueryPool::result(std::uint32_t index) const noexcept {
  std::uint64_t value = 0;
  const auto* bytes = static_cast<const unsigned char*>(results_->contents());
  std::memcpy(&value, bytes + static_cast<std::size_t>(index) * sizeof(std::uint64_t),
              sizeof(value));
  return value;
}

} // namespace bw::rhi::metal
