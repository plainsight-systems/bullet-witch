// Source group expansion prepares placement requests, never live actors.
// Borrow a validated immutable definition and explicit mission RNG during each
// call; returned candidates own numeric values and refer to definition indexes.
// Caller retains definitions while consuming candidates. Caller also owns
// trigger-fired state, deferred admission, ground-query capability and
// lifetime. Algorithm: select matching rows -> stable lexical shape ordering ->
// distribute shared regions using original cell shuffle -> publish authored
// actor facing. Ground projection is an explicit subsequent world operation: 2D
// Y2500 to -5000; 3D authored Y to Y-2500. Source leaves authored Y on a
// real query miss. Missing query
// capability must not masquerade as a miss. No actor/AI/renderer callback
// exists here. Fixed scratch covers native 256 pending-record capacity and
// <=1024 cells; larger groups fail explicitly. O(groups squared + cells) work,
// zero dynamic allocation. RNG failures throw and never publish partial
// candidates; RNG consumption itself is not rolled back. Keep successful
// candidates on a later admission retry, rather than drawing again. Single
// world-owner thread only. Design Core I.1 (#ri-explicit) exposes RNG and
// ground contract; C.9
// (#rc-private) protects storage. Performance MEM.9
// (guidelines/memory/MEM.9-embedded-allocate-at-init.md) bounds scratch.
// Source: research/engine/subsystems/mission-startup-source-trace.md.
#pragma once
#include "game/content/original/mission_placement.hpp"
namespace bw::game {
class PlacementRandom {
 public:
  PlacementRandom() = default;
  virtual ~PlacementRandom() = default;
  PlacementRandom(const PlacementRandom&) = delete;
  PlacementRandom& operator=(const PlacementRandom&) = delete;
  PlacementRandom(PlacementRandom&&) = delete;
  PlacementRandom& operator=(PlacementRandom&&) = delete;
  virtual std::uint16_t next_random15() = 0;
};
// Original Windows CRT stream shared by title selection and placement. The
// session supplies the platform-millisecond seed at title activation; replay
// supplies an explicit seed. This is not SourceRandom's separate game RNG.
// Fixed unsigned wrap and high15 output match the existing title selector.
class PlacementRandomStream final : public PlacementRandom {
 public:
  explicit PlacementRandomStream(std::uint32_t seed) : state_(seed) {}
  void seed(std::uint32_t value) { state_ = value; }
  std::uint16_t next_random15() override;
  std::uint32_t state() const { return state_; }

 private:
  std::uint32_t state_;
};
struct SpawnCandidate {
  std::size_t archetype{}, group_row{};
  math::Vector3<float> position{};
  float yaw_degrees{}, admission_distance{}, retirement_distance{};
  bool ground_projection_required{true};
  bool ground_3d{};
};
class MissionPopulationExpansion {
 public:
  static constexpr std::size_t kCapacity = 256;
  // The span remains valid until the next prepare call or destruction. No
  // mutation capability to published candidates is given to readers.
  std::span<const SpawnCandidate> prepare(
      const content::MissionPlacementDefinition& definition,
      std::int32_t group_id, PlacementRandom& random);
  std::span<const SpawnCandidate> candidates() const {
    return std::span(candidates_).first(count_);
  }

 private:
  std::array<SpawnCandidate, kCapacity> candidates_{};
  std::size_t count_{};
};

// Mission-owned placement lifetime. A stable numeric identity survives deferred
// residency; geometry/animation assets stay with the load owner. All receivers
// are call-only borrows, never retained mutation authority. Construction sizes
// fired state from immutable definitions. Each admitted world step first
// retires at most one eligible distant actor, admits at most one stored actor,
// then scans source trigger rows. A newly triggered group cannot admit in that
// same step. Staging/grounding completes before a group is published; a failed
// actor admission leaves its stored request for retry. Fixed native 256-record
// pending capacity, O(records + trigger rows squared) scans, no scan
// allocations. Single world thread; pause is enforced by the caller's
// WorldFrame authority. Core I.11 (#ri-raw), implementation E.6 (#re-raii), and
// both-phase MEM.9 (guidelines/memory/MEM.9-embedded-allocate-at-init.md) bound
// borrow/cleanup and storage. Native admission details:
// mission-startup-source-trace.md.
struct PopulationIdentity {
  std::uint32_t value{};
  friend bool operator==(PopulationIdentity, PopulationIdentity) = default;
};
struct PopulationActorState {
  SpawnCandidate placement;
  float life{}, maximum_life{};
  bool retirement_eligible{};
};
struct PopulationObservation {
  math::Vector3<float> player{}, camera{};
  // Original narrow-field camera admission includes visible distant actors.
  // Ordinary follow-camera admission uses distance only.
  bool narrow_camera{};
};
class PopulationReceivers {
 public:
  PopulationReceivers() = default;
  virtual ~PopulationReceivers() = default;
  PopulationReceivers(const PopulationReceivers&) = delete;
  PopulationReceivers& operator=(const PopulationReceivers&) = delete;
  PopulationReceivers(PopulationReceivers&&) = delete;
  PopulationReceivers& operator=(PopulationReceivers&&) = delete;
  virtual math::Vector3<float> project_ground(const SpawnCandidate&) const = 0;
  virtual bool visible(const math::Vector3<float>& position) const = 0;
  // Return false only for actual temporary admission unavailability. An
  // unsupported actor/consumer throws; never report a bind-pose fake success.
  virtual bool admit(PopulationIdentity, const PopulationActorState&) = 0;
  virtual PopulationActorState observe(PopulationIdentity) const = 0;
  virtual void retire(PopulationIdentity) = 0;
};
// Typed mission-lifetime checkpoint of the implemented population producers.
// Original enemy save writes living active actors then all deferred records;
// its separate trigger block has256 fired bytes. Identity/residency below are
// reconstruction metadata, not claims about that binary format. No owner or
// mutable view escapes. Fixed values own their contents; capture is O(active
// + pending + trigger rows), allocation-free. R.1/R.2 and MEM.9/CDSA.32.
struct PopulationCheckpoint {
  static constexpr std::size_t kRecords = 50 + 256, kTriggers = 256;
  struct Record {
    PopulationIdentity identity;
    PopulationActorState state;
    bool deferred{};
  };
  std::array<Record, kRecords> records{};
  std::array<std::uint8_t, kTriggers> fired{};
  std::size_t count{}, trigger_count{};
};
class MissionPopulation {
 public:
  explicit MissionPopulation(const content::MissionPlacementDefinition&,
                             const std::shared_ptr<memory::Domain>&);
  MissionPopulation(const MissionPopulation&) = delete;
  MissionPopulation& operator=(const MissionPopulation&) = delete;
  MissionPopulation(MissionPopulation&&) = delete;
  MissionPopulation& operator=(MissionPopulation&&) = delete;
  ~MissionPopulation() = default;
  void advance(const PopulationObservation&, PlacementRandom&,
               PopulationReceivers&);
  std::size_t active_count() const { return active_count_; }
  std::size_t pending_count() const;
  std::size_t fired_count() const;
  PopulationCheckpoint checkpoint(const PopulationReceivers&) const;

 private:
  static constexpr std::size_t kCapacity =
      MissionPopulationExpansion::kCapacity;
  struct Pending {
    PopulationIdentity identity;
    PopulationActorState state;
    unsigned delay{};
    bool occupied{};
  };
  const content::MissionPlacementDefinition& definition_;
  memory::Vector<std::uint8_t> fired_;
  MissionPopulationExpansion expansion_;
  std::array<Pending, kCapacity> pending_{};
  std::array<PopulationIdentity, kCapacity> active_{};
  std::size_t active_count_{};
  std::uint32_t next_identity_{1};
  void retire_distant(const PopulationObservation&, PopulationReceivers&);
  void admit_pending(const PopulationObservation&, PopulationReceivers&,
                     std::size_t live_count_before_retirement);
  void scan_triggers(const PopulationObservation&, PlacementRandom&,
                     PopulationReceivers&);
};
}  // namespace bw::game
