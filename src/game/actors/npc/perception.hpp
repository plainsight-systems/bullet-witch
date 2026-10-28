// Original NPC target ranking, visibility and retained knowledge. World owns
// actors, the issuing clock and the shared scheduling bank. This owner borrows
// the clock/bank, copies observations and returns copied target/transition
// facts; it never moves an actor, fires a weapon or mutates physics. Caller
// supplies original eye position/forward, authored attention fields and active
// actors in original traversal order (player first). Sight is synchronous
// query-only. Keep top three ranked candidates, test visibility in rank order,
// preserve the best occluded candidate, then apply source knowledge/lost-target
// transitions. The original ten-slot scheduler staggers expensive scans;
// admitted world delta advances timers even on skipped scans. Pause does
// neither. No wall clock or frame allocation. Invalid/foreign/repeated frames
// and malformed copied input throw. All instances and their borrowed
// dependencies use one world thread. I.11 (#ri-raw) makes borrows explicit;
// MEM.9 (guidelines/memory/ MEM.9-embedded-allocate-at-init.md) fixes
// candidate/scheduling storage. Both design corpora read. Source details:
// first-combat-ai.md, 5aad90/5aa490.
#pragma once
#include <array>
#include <optional>
#include <span>

#include "bw/math/vector.hpp"
#include "bw/timing/world_clock.hpp"

namespace bw::game {
struct PerceptionActor {
  std::uint64_t identity{};
  math::Vector3<float> position{};
  std::uint32_t affiliation{};
  float target_eligibility{};
  bool player{}, dead{};
};
struct PerceptionSettings {
  float view_angle_radians{}, close_radius{}, far_radius{};
  float target_change_delay{}, lost_target_seconds{}, nearby_radius{};
};
struct PerceptionInput {
  math::Vector3<float> eye{}, forward{}, target_offset{};
  std::uint32_t affiliation{};
  std::span<const PerceptionActor> actors;
};
class PerceptionSight {
 public:
  PerceptionSight() = default;
  virtual ~PerceptionSight() = default;
  PerceptionSight(const PerceptionSight&) = delete;
  PerceptionSight& operator=(const PerceptionSight&) = delete;
  PerceptionSight(PerceptionSight&&) = delete;
  PerceptionSight& operator=(PerceptionSight&&) = delete;
  virtual bool clear(const math::Vector3<float>& origin,
                     const math::Vector3<float>& target,
                     std::uint32_t excluded_source_flags) const = 0;
};
class PerceptionSchedule {
 public:
  PerceptionSchedule() = default;
  PerceptionSchedule(const PerceptionSchedule&) = delete;
  PerceptionSchedule& operator=(const PerceptionSchedule&) = delete;
  PerceptionSchedule(PerceptionSchedule&&) = delete;
  PerceptionSchedule& operator=(PerceptionSchedule&&) = delete;
  ~PerceptionSchedule() = default;
  // Source5aa100/5a9190: counts start zero; descending strict-min selection
  // preserves the higher slot on equal load. Slot zero is never assigned.
  std::uint8_t next();
  // World phase rotates scheduled future loads before either actor owner scans.
  void advance();

 private:
  static constexpr std::size_t kSchedulingSlots = 10;
  std::array<std::uint32_t, kSchedulingSlots> counts_{};
};
enum class TargetKnowledge : std::uint8_t {
  unknown = 0,
  searching = 1,
  known = 3
};
struct PerceptionState {
  TargetKnowledge knowledge{TargetKnowledge::unknown};
  std::optional<std::uint64_t> target;
  math::Vector3<float> last_seen_position{};
  bool visible{};
};
struct PerceptionTransition {
  TargetKnowledge before{}, after{};
  // Original message identities: acquired7532, lost7534, forgotten7535.
  std::uint32_t source_message{};
};
class NpcPerception {
 public:
  NpcPerception(const world::WorldClock&, PerceptionSchedule&,
                PerceptionSettings, unsigned original_mode = 0);
  NpcPerception(const NpcPerception&) = delete;
  NpcPerception& operator=(const NpcPerception&) = delete;
  NpcPerception(NpcPerception&&) = delete;
  NpcPerception& operator=(NpcPerception&&) = delete;
  ~NpcPerception() = default;
  std::optional<PerceptionTransition> advance(const world::WorldFrame&,
                                              const PerceptionInput&,
                                              const PerceptionSight&);
  // Original target-request receiver resets knowledge timers and refreshes
  // remembered position from the live identity. An absent replacement retains
  // the current target; it does not fabricate an actor or advance time.
  void receive_target(std::optional<std::uint64_t> replacement, TargetKnowledge,
                      std::span<const PerceptionActor> actors);
  PerceptionState state() const noexcept { return state_; }

 private:
  const world::WorldClock& clock_;
  PerceptionSchedule& schedule_;
  PerceptionSettings settings_;
  PerceptionState state_;
  std::uint64_t sequence_{};
  float change_delay_{}, lost_seconds_{};
  std::uint8_t skip_{};
  unsigned mode_{};
};
}  // namespace bw::game
