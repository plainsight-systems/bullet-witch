// Preserve original callback condition ordering and consumption independently
// of Lua continuation ownership. Validate complete registrations before slot
// publication; stable insertion sort uses inline records rather than allocator
// backed sorting. Triggered timers freeze until consumed. Both-phase Core I.1
// (#ri-explicit) and performance MEM.9 (guidelines/memory/
// MEM.9-embedded-allocate-at-init.md) keep queries explicit and work bounded.
#include "mission_callbacks.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace bw::game {
void MissionCallbacks::add(MissionCallbackRecord record,
                           std::string_view function) {
  if (function.empty() || function.size() >= record.function.size() ||
      function.find('\0') != std::string_view::npos || record.mode < 0 ||
      record.mode > 2) {
    throw std::invalid_argument("Invalid mission callback registration");
  }
  const auto slot =
      std::ranges::find(records_, MissionCallbackRecord::Cause::unused,
                        &MissionCallbackRecord::cause);
  if (slot == records_.end()) {
    throw std::length_error("Mission callback registry capacity exceeded");
  }
  std::ranges::copy(function, record.function.begin());
  *slot = record;
}
void MissionCallbacks::add_trap(std::string_view function,
                                MissionTrapEvent event) {
  MissionCallbackRecord record;
  record.cause = MissionCallbackRecord::Cause::trap;
  record.mode = event.kind;
  record.event = event.event;
  add(record, function);
}
void MissionCallbacks::add_timer(std::string_view function,
                                 MissionTimerEvent timer) {
  if (!std::isfinite(timer.seconds) || timer.seconds < 0) {
    throw std::invalid_argument("Invalid mission callback timer");
  }
  MissionCallbackRecord record;
  record.cause = MissionCallbackRecord::Cause::timer;
  record.mode = timer.mode;
  record.interval = record.remaining = timer.seconds;
  add(record, function);
}
void MissionCallbacks::tick(float seconds) {
  if (!std::isfinite(seconds) || seconds < 0) {
    throw std::invalid_argument("Invalid mission callback world interval");
  }
  for (auto& record : records_) {
    if (record.cause == MissionCallbackRecord::Cause::timer &&
        !record.triggered) {
      record.remaining -= seconds;
    }
  }
}
void MissionCallbacks::scan(const std::array<float, 3>& position,
                            const MissionTrapContains& contains) {
  if (!contains || !std::ranges::all_of(position, [](float value) {
        return std::isfinite(value);
      })) {
    throw std::invalid_argument("Invalid mission callback trap observation");
  }
  // Stable descending cause order is the original registry's dispatch policy.
  // Keep insertion sorting in bounded inline storage; equal causes retain
  // order.
  for (std::size_t i = 1; i < records_.size(); ++i) {
    const auto candidate = records_.at(i);
    auto destination = i;
    while (destination != 0 &&
           records_.at(destination - 1).cause < candidate.cause) {
      records_.at(destination) = records_.at(destination - 1);
      --destination;
    }
    records_.at(destination) = candidate;
  }
  for (auto& record : records_) {
    if (record.triggered) continue;
    if (record.cause == MissionCallbackRecord::Cause::timer) {
      record.triggered = record.remaining <= 0;
    } else if (record.cause == MissionCallbackRecord::Cause::trap) {
      const bool inside = contains(record.event, position);
      record.triggered = inside && (record.mode != 2 || !record.inside);
      record.inside = inside;
    }
  }
}
std::optional<MissionCallbackName> MissionCallbacks::take() {
  for (auto& record : records_) {
    if (record.cause == MissionCallbackRecord::Cause::unused ||
        !record.triggered) {
      continue;
    }
    const auto function = record.function;
    record.triggered = false;
    // Retire/rearm before entering Lua: the callback may register more work.
    if (record.mode == 0) {
      record.cause = MissionCallbackRecord::Cause::unused;
    } else if (record.mode == 1 &&
               record.cause == MissionCallbackRecord::Cause::timer) {
      record.remaining = record.interval;
    }
    return function;
  }
  return std::nullopt;
}
std::size_t MissionCallbacks::trap_count() const noexcept {
  return static_cast<std::size_t>(
      std::ranges::count(records_, MissionCallbackRecord::Cause::trap,
                         &MissionCallbackRecord::cause));
}
}  // namespace bw::game
