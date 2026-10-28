// One actor's original perception, Lua evaluation and native action owners.
// Construct from mission-retained named definitions/action library, shared
// schedules/random stream, the world's issuing clock and separate observation/
// action capabilities. All borrowed dependencies outlive this nonmoving owner.
// Mission library prepares shared wrapper VMs once; actor construction never
// allocates a VM or selects actions. Explicit start follows adapter binding.
// Shared scoring is construction-thread-only and rejects reentry; its scoped
// actor observation borrow is cleared on every exit. Script failure is terminal
// for that wrapper, matching Context; no recovery substitutes a score. Actors
// update perception then source action cadence on authoritative world frames;
// pause advances none. Internal observations expose owned knowledge,
// completion, elapsed time, level-random and stack labels. Remaining
// observations require the external query-only source and never acquire guessed
// defaults. Replacement selects only a prepared tree; retn resolves the initial
// behavior name. Commands may queue replacement but cannot synchronously
// reenter this owner. stop retires actions before VM/resource borrows end.
// Exceptions propagate; no fake action succeeds. Domain storage and Lua heaps
// are prepared once; no archive reads or owner allocation in frames. Core I.1
// (#ri-explicit), E.4 make capabilities and lifecycle explicit; MEM.9
// (guidelines/memory/MEM.9-embedded-allocate-at- init.md) prepares bounded VM
// owners. Both corpora read in both phases.
#pragma once
#include "behavior_lua.hpp"
#include "behavior_observations.hpp"
#include "native_actions.hpp"
#include "perception.hpp"
namespace bw::game {
struct NpcBehaviorDefinition {
  std::string_view name;
  const formats::BehaviorTreeDefinition* definition{};
  std::size_t lua_heap_bytes{};
};
struct NpcBehaviorId {
  std::size_t value{};
};
// Definitions/names remain mission-owned and outlive this library and brains.
// Only audited read-only score wrappers may be retained across actor calls.
class NpcBehaviorLibrary {
 public:
  NpcBehaviorLibrary(std::span<const NpcBehaviorDefinition>,
                     const std::shared_ptr<memory::Domain>&);
  NpcBehaviorLibrary(const NpcBehaviorLibrary&) = delete;
  NpcBehaviorLibrary& operator=(const NpcBehaviorLibrary&) = delete;
  NpcBehaviorLibrary(NpcBehaviorLibrary&&) = delete;
  NpcBehaviorLibrary& operator=(NpcBehaviorLibrary&&) = delete;
  ~NpcBehaviorLibrary();
  NpcBehaviorId resolve(std::string_view) const;
  std::string_view name(NpcBehaviorId) const;
  const formats::BehaviorTreeDefinition& definition(NpcBehaviorId) const;
  float evaluate(NpcBehaviorId, const formats::BehaviorNode&,
                 std::uint32_t actor_identity,
                 const BehaviorObservationSource&);

 private:
  struct Impl;
  std::shared_ptr<Impl> impl_;
};
class NpcBrain {
 public:
  NpcBrain(const world::WorldClock&, BehaviorSchedule&, PerceptionSchedule&,
           SourceRandom&, NpcBehaviorLibrary&, const NativeActionLibrary&,
           PerceptionSettings, std::uint32_t actor_identity,
           const BehaviorObservationSource&, const NpcActionQueries&,
           NpcActionReceiver&, const std::shared_ptr<memory::Domain>&);
  NpcBrain(const NpcBrain&) = delete;
  NpcBrain& operator=(const NpcBrain&) = delete;
  NpcBrain(NpcBrain&&) = delete;
  NpcBrain& operator=(NpcBrain&&) = delete;
  ~NpcBrain();
  void start(std::string_view name, std::size_t entry = 1);
  void replace(std::string_view name, std::size_t entry = 1);
  std::optional<PerceptionTransition> advance(const world::WorldFrame&,
                                              const PerceptionInput&,
                                              const PerceptionSight&);
  void stop();
  void receive_target(std::optional<std::uint64_t>, TargetKnowledge,
                      std::span<const PerceptionActor>);
  PerceptionState perception() const noexcept;
  PerceptionState secondary_perception() const noexcept;
  // Queue source events without exposing mutable behavior/perception owners.
  bool request_event(std::size_t entry);
  std::string_view behavior() const;
  std::string_view return_behavior() const;
  std::optional<std::size_t> current_node() const noexcept;

 private:
  struct Impl;
  std::shared_ptr<Impl> impl_;
};
}  // namespace bw::game
