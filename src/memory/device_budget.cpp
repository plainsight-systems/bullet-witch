// Device charge implementation; contract and threading in device_budget.hpp.
// Admit before native acquisition, reconcile without mutating on rejection,
// retain owner through final release. O(1) counters and one lock per operation;
// token moves allocate nothing. E.6 (#re-raii), SL.con.3 (#rsl-bounds), GPU.1
// (guidelines/gpu/GPU.1-keep-data-on-device.md), COPY.5
// (guidelines/copy-move/COPY.5-resource-owning-types-are-move-only.md) applied
// in both MCP phases to rollback, checked totals and unique token ownership.
#include "bw/memory/device_budget.hpp"

#include <algorithm>
#include <new>
#include <stdexcept>
#include <utility>

#include "bw/memory/allocator.hpp"

namespace bw::memory {
namespace {
void rejected(DeviceSnapshot& stats) noexcept {
  if (stats.rejected_requests != std::numeric_limits<std::size_t>::max()) {
    ++stats.rejected_requests;
  }
}
}  // namespace
DeviceCharge::DeviceCharge(std::shared_ptr<DeviceBudget> owner,
                           Extent bytes) noexcept
    : owner_(std::move(owner)),
      logical_(bytes.logical),
      native_(bytes.native) {}
DeviceCharge::~DeviceCharge() { reset(); }
DeviceCharge::DeviceCharge(DeviceCharge&& other) noexcept
    : owner_(std::move(other.owner_)),
      logical_(std::exchange(other.logical_, 0)),
      native_(std::exchange(other.native_, 0)),
      committed_(std::exchange(other.committed_, false)) {}
DeviceCharge& DeviceCharge::operator=(DeviceCharge&& other) noexcept {
  if (this != &other) {
    reset();
    owner_ = std::move(other.owner_);
    logical_ = std::exchange(other.logical_, 0);
    native_ = std::exchange(other.native_, 0);
    committed_ = std::exchange(other.committed_, false);
  }
  return *this;
}
void DeviceCharge::commit(std::size_t reported_native_bytes) {
  if (!owner_ || committed_) {
    throw std::logic_error("Device charge is not pending");
  }
  owner_->commit(*this, reported_native_bytes);
}
void DeviceCharge::reset() noexcept {
  if (owner_) {
    owner_->release(*this);
    owner_.reset();
  }
  logical_ = native_ = 0;
  committed_ = false;
}
std::shared_ptr<DeviceBudget> DeviceBudget::create(
    const std::shared_ptr<Domain>& cpu_domain, DeviceLimits limits) {
  return std::allocate_shared<DeviceBudget>(Allocator<DeviceBudget>(cpu_domain),
                                            ConstructionKey{}, limits);
}
DeviceBudget::DeviceBudget(ConstructionKey, DeviceLimits limits)
    : limits_(limits) {}
void DeviceBudget::peak() noexcept {
  stats_.peak_admitted_logical_bytes =
      std::max(stats_.peak_admitted_logical_bytes,
               stats_.pending_logical_bytes + stats_.live_logical_bytes);
  stats_.peak_admitted_native_bytes =
      std::max(stats_.peak_admitted_native_bytes,
               stats_.pending_native_bytes + stats_.live_native_bytes);
}
DeviceCharge DeviceBudget::reserve(std::size_t logical_bytes,
                                   std::size_t estimated_native_bytes) {
  auto owner = shared_from_this();
  const std::lock_guard lock(mutex_);
  const auto count = stats_.pending_resources + stats_.live_resources;
  if (logical_bytes > limits_.max_logical_bytes - stats_.pending_logical_bytes -
                          stats_.live_logical_bytes ||
      estimated_native_bytes > limits_.max_native_bytes -
                                   stats_.pending_native_bytes -
                                   stats_.live_native_bytes ||
      count == std::numeric_limits<std::size_t>::max()) {
    rejected(stats_);
    throw std::bad_alloc();
  }
  stats_.pending_logical_bytes += logical_bytes;
  stats_.pending_native_bytes += estimated_native_bytes;
  ++stats_.pending_resources;
  peak();
  return DeviceCharge(std::move(owner),
                      {logical_bytes, estimated_native_bytes});
}
void DeviceBudget::commit(DeviceCharge& charge, std::size_t reported) {
  const std::lock_guard lock(mutex_);
  const auto other_pending = stats_.pending_native_bytes - charge.native_;
  if (reported >
      limits_.max_native_bytes - stats_.live_native_bytes - other_pending) {
    rejected(stats_);
    throw std::bad_alloc();
  }
  stats_.pending_logical_bytes -= charge.logical_;
  stats_.pending_native_bytes -= charge.native_;
  --stats_.pending_resources;
  stats_.live_logical_bytes += charge.logical_;
  stats_.live_native_bytes += reported;
  ++stats_.live_resources;
  charge.native_ = reported;
  charge.committed_ = true;
  peak();
}
void DeviceBudget::release(const DeviceCharge& charge) noexcept {
  const std::lock_guard lock(mutex_);
  if (charge.committed_) {
    stats_.live_logical_bytes -= charge.logical_;
    stats_.live_native_bytes -= charge.native_;
    --stats_.live_resources;
  } else {
    stats_.pending_logical_bytes -= charge.logical_;
    stats_.pending_native_bytes -= charge.native_;
    --stats_.pending_resources;
  }
}
DeviceSnapshot DeviceBudget::snapshot() const {
  const std::lock_guard lock(mutex_);
  return stats_;
}
}  // namespace bw::memory
