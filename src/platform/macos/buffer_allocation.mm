// Implement the native immutable-buffer contract in buffer_allocation.hpp.
// Admission precedes native copying; reported-size reconciliation precedes
// publication. ARC/RAII unwind allocation or budget failure. Device consumers
// must retire before destruction; the world submission owner provides that proof.
// O(bytes) setup copy and constant owner/counter work, no per-frame allocation.
// Both MCP phases: E.6 (#re-raii) owns rollback; GPU.1
// (guidelines/gpu/GPU.1-keep-data-on-device.md) keeps immutable geometry resident.
#include "platform/macos/buffer_allocation.hpp"

#include <new>
#include <stdexcept>
#include <utility>

namespace bw::platform::macos {
BufferAllocation::BufferAllocation(memory::DeviceCharge charge, id<MTLBuffer> buffer) noexcept
    : charge_(std::move(charge)), buffer_(buffer) {}
BufferAllocation::~BufferAllocation() { reset(); }
BufferAllocation::BufferAllocation(BufferAllocation&& other) noexcept
    : charge_(std::move(other.charge_)), buffer_(other.buffer_) {
  other.buffer_ = nil;
}
BufferAllocation& BufferAllocation::operator=(BufferAllocation&& other) noexcept {
  if (this != &other) {
    reset();
    charge_ = std::move(other.charge_);
    buffer_ = other.buffer_;
    other.buffer_ = nil;
  }
  return *this;
}
void BufferAllocation::reset() noexcept {
  buffer_ = nil;
  charge_.reset();
}
BufferAllocation BufferAllocation::create(id<MTLDevice> device,
                                          const std::shared_ptr<memory::DeviceBudget>& budget,
                                          std::span<const std::uint8_t> bytes) {
  if (device == nil || !budget || bytes.empty() || bytes.size() > device.maxBufferLength) {
    throw std::invalid_argument("Invalid native geometry buffer request");
  }
  @autoreleasepool {
    // Admission includes the temporary overlap when replacing retained geometry.
    const auto estimate = [device heapBufferSizeAndAlignWithLength:bytes.size()
                                                           options:MTLResourceStorageModeShared];
    auto charge = budget->reserve(bytes.size(), estimate.size);
    id<MTLBuffer> buffer = [device newBufferWithBytes:bytes.data()
                                               length:bytes.size()
                                              options:MTLResourceStorageModeShared];
    if (buffer == nil) throw std::bad_alloc();
    // Only a fully charged native owner can escape to the renderer.
    charge.commit(buffer.allocatedSize);
    return BufferAllocation(std::move(charge), buffer);
  }
}
}  // namespace bw::platform::macos
