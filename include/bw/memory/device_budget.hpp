// Portable device admission/accounting; no device allocation or residency API.
// reserve admits logical payload and estimated native storage; commit replaces
// the estimate with reported native bytes before publication. Limits apply to
// pending plus live totals. Failure throws without discarding prior charges;
// RAII rollback/release returns exactly the token's charge. Charges retain the
// budget; budget CPU metadata/control use the injected CPU domain. Defaults
// are uncapped, not inferred hardware capacities. Snapshot separates estimates
// from reported bytes and is not process/physical memory. Budget operations
// synchronize; individual move-only tokens require exclusive caller access.
// Native owners must release resources after consumer completion, then reset
// the token. Algorithm: admit -> acquire native resource -> commit -> consume
// -> completion -> native release -> charge release. Constant counters/locking,
// no allocation per token. E.6 (#re-raii) provides rollback; SL.con.3
// (#rsl-bounds) checks subtraction before admission. GPU.1
// (guidelines/gpu/GPU.1-keep-data-on-device.md) separates device ownership;
// COPY.5 (guidelines/copy-move/COPY.5-resource-owning-types-are-move-only.md)
// keeps charges unique. Both MCP phases consulted. See
// device-memory-ownership.md.
#pragma once

#include <cstddef>
#include <limits>
#include <memory>
#include <mutex>

#include "bw/memory/domain.hpp"

namespace bw::memory {
struct DeviceLimits {
  std::size_t max_logical_bytes = std::numeric_limits<std::size_t>::max();
  std::size_t max_native_bytes = std::numeric_limits<std::size_t>::max();
};
struct DeviceSnapshot {
  std::size_t pending_logical_bytes{};
  std::size_t pending_native_bytes{};
  std::size_t live_logical_bytes{};
  std::size_t live_native_bytes{};
  std::size_t pending_resources{};
  std::size_t live_resources{};
  std::size_t peak_admitted_logical_bytes{};
  std::size_t peak_admitted_native_bytes{};
  std::size_t rejected_requests{};
};
class DeviceBudget;
class DeviceCharge {
 public:
  DeviceCharge() = default;
  ~DeviceCharge();
  DeviceCharge(const DeviceCharge&) = delete;
  DeviceCharge& operator=(const DeviceCharge&) = delete;
  DeviceCharge(DeviceCharge&& other) noexcept;
  DeviceCharge& operator=(DeviceCharge&& other) noexcept;
  void commit(std::size_t reported_native_bytes);
  void reset() noexcept;
  explicit operator bool() const noexcept { return owner_ != nullptr; }

 private:
  friend class DeviceBudget;
  struct Extent {
    std::size_t logical;
    std::size_t native;
  };
  DeviceCharge(std::shared_ptr<DeviceBudget> owner, Extent bytes) noexcept;
  std::shared_ptr<DeviceBudget> owner_;
  std::size_t logical_{};
  std::size_t native_{};
  bool committed_{};
};
class DeviceBudget final : public std::enable_shared_from_this<DeviceBudget> {
  class ConstructionKey {
    friend class DeviceBudget;
    ConstructionKey() = default;
  };

 public:
  static std::shared_ptr<DeviceBudget> create(
      const std::shared_ptr<Domain>& cpu_domain, DeviceLimits limits = {});
  DeviceBudget(ConstructionKey, DeviceLimits limits);
  [[nodiscard]] DeviceCharge reserve(std::size_t logical_bytes,
                                     std::size_t estimated_native_bytes);
  [[nodiscard]] DeviceSnapshot snapshot() const;

 private:
  friend class DeviceCharge;
  void commit(DeviceCharge& charge, std::size_t reported);
  void release(const DeviceCharge& charge) noexcept;
  void peak() noexcept;
  DeviceLimits limits_;
  mutable std::mutex mutex_;
  DeviceSnapshot stats_;
};
}  // namespace bw::memory
