// Prepared motion-parameter channels: validate native tags, strides and ranges
// once, retain source record order and explicit signed start/end frames. The
// metadata uses an injected domain; payload spans borrow immutable input bytes
// for the entire prepared value/consumer lifetime. No effect semantics are
// inferred from compatibility tags. Malformed tables/counts reject before
// publication; absent channels are empty. One immutable value can be shared by
// independent playback instances. Algorithm: check the directory, measure all
// present channels, reserve one record vector, decode timing and borrow each
// bounded record. O(records), metadata O(records), no bulk payload copy.
// Design: Core I.1 (#ri-explicit) exposes ownership and timing; Performance
// MEM.9 (guidelines/memory/MEM.9-embedded-allocate-at-init.md) prepares fixed
// runtime inputs. Source/layout:
// research/engine/subsystems/motion-event-timeline.md.
#pragma once

#include "bw/formats/common.hpp"
#include "bw/memory/allocator.hpp"

namespace bw::formats {
inline constexpr std::size_t kMotionEventChannels = 10;
inline constexpr std::size_t kMotionEventsPerChannel = 128;
// Exact numeric compatibility tags, without speculative mnemonic expansions.
inline constexpr std::uint32_t kCancellationEventTag = 0x43414e30;
struct MotionEvent {
  std::int32_t start_frame{}, end_frame{-1};
  std::span<const std::uint8_t> payload;
};
struct MotionEventChannel {
  std::uint32_t tag{};
  std::span<const MotionEvent> records;
};
using MotionEventChannels =
    std::array<MotionEventChannel, kMotionEventChannels>;
class MotionEvents {
 public:
  static MotionEvents parse(std::span<const std::uint8_t> bytes,
                            const std::shared_ptr<memory::Domain>& domain);
  // Returned spans also borrow this object's immutable metadata. Moving or
  // destroying it while a timeline is bound is forbidden by the caller.
  MotionEventChannels channels() const;

 private:
  explicit MotionEvents(const std::shared_ptr<memory::Domain>& domain)
      : records_(memory::Allocator<MotionEvent>(domain)) {}
  memory::Vector<MotionEvent> records_;
  std::array<std::size_t, kMotionEventChannels> offsets_{}, counts_{};
};
}  // namespace bw::formats
