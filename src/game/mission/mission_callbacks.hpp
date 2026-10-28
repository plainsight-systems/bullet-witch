// Game-owned original mission callback conditions, independent of Lua task
// execution. Own fixed source-sized records; borrow the const trap query only
// during scan. Inputs are validated before registration. Tick consumes only an
// admitted world interval; scan marks ready conditions in stable descending
// cause order; take retires/rearms one record before its script executes. Lua
// dispatch remains serial in MissionDriver. Single owner thread, O(64) tick,
// bounded insertion-sort scan, no heap allocation. Invalid mode/name/capacity
// throws without publication. Core I.1 (#ri-explicit) makes timing/query
// capabilities explicit; performance MEM.9 (guidelines/memory/
// MEM.9-embedded-allocate-at-init.md) keeps registry storage inline. Design and
// implementation consulted both corpora. Source: mission-script-callbacks.md.
#pragma once
#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string_view>
namespace bw::game {
inline constexpr std::size_t kMissionCallbackCapacity = 64;
inline constexpr std::size_t kMissionCallbackNameCapacity = 64;
using MissionCallbackName = std::array<char, kMissionCallbackNameCapacity>;
using MissionTrapContains =
    std::function<bool(std::int32_t, const std::array<float, 3>&)>;
struct MissionTrapEvent {
  std::int32_t kind{}, event{};
};
struct MissionTimerEvent {
  std::int32_t mode{};
  float seconds{};
};
struct MissionCallbackRecord {
  enum class Cause : std::int16_t {
    unused = -1,
    timer = 0,
    trap = 3,
    player_defeated = 1000
  };
  Cause cause{Cause::unused};
  MissionCallbackName function{};
  std::int32_t mode{}, event{};
  float interval{}, remaining{};
  bool triggered{}, inside{};
};
class MissionCallbacks {
 public:
  MissionCallbacks() = default;
  ~MissionCallbacks() = default;
  MissionCallbacks(const MissionCallbacks&) = delete;
  MissionCallbacks& operator=(const MissionCallbacks&) = delete;
  MissionCallbacks(MissionCallbacks&&) = delete;
  MissionCallbacks& operator=(MissionCallbacks&&) = delete;
  void add_trap(std::string_view function, MissionTrapEvent event);
  void add_timer(std::string_view function, MissionTimerEvent timer);
  // The original default death event shares ordering/storage with realtime
  // conditions. It is one-shot and becomes ready from copied player life.
  void add_player_defeated(std::string_view function);
  void cancel_all() noexcept;
  void tick(float seconds);
  void scan(const std::array<float, 3>& position,
            const MissionTrapContains& contains, bool player_defeated = false);
  std::optional<MissionCallbackName> take();
  std::span<const MissionCallbackRecord> records() const noexcept {
    return records_;
  }
  std::size_t trap_count() const noexcept;

 private:
  void add(MissionCallbackRecord record, std::string_view function);
  std::array<MissionCallbackRecord, kMissionCallbackCapacity> records_{};
};
}  // namespace bw::game
