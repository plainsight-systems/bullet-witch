// Move-only native immutable buffer and corresponding device-budget charge.
// create borrows bytes for the call, validates size, admits estimated storage,
// copies through Metal shared storage, and reconciles allocatedSize before
// publication. Failure releases both native resource and admission. The caller
// retires GPU consumers before reset/destruction. get() is borrowed, never an
// ownership transfer. Single-owner operations; budget synchronization is
// separate. Algorithm: validate -> reserve -> acquire/copy -> commit -> retire
// -> release. One setup allocation/copy, no per-frame upload or readback.
// Native driver metadata remains external. See world-renderer-foundation.md.
// Both MCP phases: E.6 (#re-raii) rollback; GPU.1
// (guidelines/gpu/GPU.1-keep-data-on-device.md) retains device data. Preserve
// source ranges/base vertex in the draw owner.
#pragma once

#import <Metal/Metal.h>

#include <cstdint>
#include <memory>
#include <span>

#include "bw/memory/device_budget.hpp"

namespace bw::platform::macos {
class BufferAllocation {
 public:
  BufferAllocation() = default;
  ~BufferAllocation();
  BufferAllocation(const BufferAllocation&) = delete;
  BufferAllocation& operator=(const BufferAllocation&) = delete;
  BufferAllocation(BufferAllocation&& other) noexcept;
  BufferAllocation& operator=(BufferAllocation&& other) noexcept;
  static BufferAllocation create(
      id<MTLDevice> device, const std::shared_ptr<memory::DeviceBudget>& budget,
      std::span<const std::uint8_t> bytes);
  // Native release precedes return of its accounting charge.
  void reset() noexcept;
  id<MTLBuffer> get() const noexcept { return buffer_; }

 private:
  BufferAllocation(memory::DeviceCharge charge, id<MTLBuffer> buffer) noexcept;
  memory::DeviceCharge charge_;
  id<MTLBuffer> buffer_{};
};
}  // namespace bw::platform::macos
