// In-process callback contracts exercise source modes, stable cause order,
// bounded capacity and world-interval timers without original files or devices.
// Core I.1 (#ri-explicit) tests public observations; performance MEM.9
// (guidelines/memory/MEM.9-embedded-allocate-at-init.md) exercises fixed
// storage reuse. No assertion duplicates private algorithm steps.
#include "game/mission/mission_callbacks.hpp"

#include <limits>
#include <stdexcept>
#include <type_traits>
namespace {
using bw::game::MissionCallbacks;
void require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}
void expect(MissionCallbacks& callbacks, std::string_view expected) {
  const auto result = callbacks.take();
  require(result && std::string_view(result->data()) == expected,
          "Callback dispatch order/name mismatch");
}
void ordering_and_modes() {
  MissionCallbacks callbacks;
  bool inside = true;
  const bw::game::MissionTrapContains contains =
      [&inside](std::int32_t event, const std::array<float, 3>& position) {
        return inside && event == 610 && position[0] == 4;
      };
  constexpr std::array<float, 3> kPosition{4, 0, 0};
  callbacks.add_timer("timer", {0, .5F});
  callbacks.add_trap("checkpoint", {0, 610});
  callbacks.add_trap("entry", {2, 610});
  callbacks.add_trap("continuous", {1, 610});
  callbacks.tick(.5F);
  callbacks.scan(kPosition, contains);
  expect(callbacks, "checkpoint");
  expect(callbacks, "entry");
  expect(callbacks, "continuous");
  expect(callbacks, "timer");
  require(!callbacks.take(), "Consumption must clear ready flags");
  callbacks.scan(kPosition, contains);
  expect(callbacks, "continuous");
  require(!callbacks.take(), "Entry mode must not repeat while inside");
  inside = false;
  callbacks.scan(kPosition, contains);
  require(!callbacks.take(), "Outside trap cannot dispatch");
  inside = true;
  callbacks.scan(kPosition, contains);
  expect(callbacks, "entry");
  expect(callbacks, "continuous");
  require(callbacks.trap_count() == 2,
          "One-shot trap retires before execution");
}
void timers_and_capacity() {
  MissionCallbacks callbacks;
  const bw::game::MissionTrapContains outside = [](auto, const auto&) {
    return false;
  };
  callbacks.add_timer("repeat", {1, 1});
  callbacks.tick(.75F);
  callbacks.tick(0);
  callbacks.scan({}, outside);
  require(!callbacks.take(), "Paused world time must not expire timer");
  callbacks.tick(.25F);
  callbacks.scan({}, outside);
  callbacks.tick(20);
  expect(callbacks, "repeat");
  callbacks.scan({}, outside);
  require(!callbacks.take(), "Triggered timer must restart without overshoot");
  callbacks.tick(1);
  callbacks.scan({}, outside);
  expect(callbacks, "repeat");
  for (std::size_t i = 1; i < bw::game::kMissionCallbackCapacity; ++i) {
    callbacks.add_timer("one", {0, 0});
  }
  bool rejected = false;
  try {
    callbacks.add_timer("overflow", {0, 0});
  } catch (const std::length_error&) {
    rejected = true;
  }
  require(rejected, "Registry capacity must reject before publication");
  callbacks.scan({}, outside);
  for (std::size_t i = 1; i < bw::game::kMissionCallbackCapacity; ++i) {
    expect(callbacks, "one");
  }
  callbacks.add_timer("reused", {0, 0});
  callbacks.scan({}, outside);
  expect(callbacks, "reused");
  require(!callbacks.take(), "Retired slot cannot retain an old ready flag");
}
void rejection() {
  MissionCallbacks callbacks;
  unsigned rejected = 0;
  try {
    callbacks.add_trap("", {0, 1});
  } catch (const std::invalid_argument&) {
    ++rejected;
  }
  try {
    callbacks.add_trap("badmode", {3, 1});
  } catch (const std::invalid_argument&) {
    ++rejected;
  }
  try {
    callbacks.add_timer("badtime", {0, std::numeric_limits<float>::infinity()});
  } catch (const std::invalid_argument&) {
    ++rejected;
  }
  try {
    callbacks.tick(-1);
  } catch (const std::invalid_argument&) {
    ++rejected;
  }
  require(rejected == 4 && callbacks.trap_count() == 0 && !callbacks.take(),
          "Rejected inputs must preserve empty registry");
}
}  // namespace
void mission_callback_tests() {
  static_assert(!std::is_copy_constructible_v<MissionCallbacks>);
  static_assert(!std::is_move_constructible_v<MissionCallbacks>);
  ordering_and_modes();
  timers_and_capacity();
  rejection();
}
