// World-owned original placement lifetime and source-initialized NPCs.
// Definitions/resources and the simulation collision observer outlive this
// owner. Scene presentation outlives retirement. Prepared archetype models are
// shared by all their instances; actors own only playback/pose/state. The
// session lends its original CRT RNG explicitly. No AI/combat outcome, clock
// mutation or asset reload is implied by admission. Validate and prepare all
// authored archetypes at bind, then manager admission/retirement and separate
// instance animation consume only admitted world deltas. Source native capacity
// is50 active/256pending. Setup and actor admission allocate in the injected
// domain; ordinary scans/pose publication allocate nothing. Core I.11/E.6 and
// performance MEM.9, read in both phases, keep stable borrows, failure cleanup
// and setup storage explicit. See mission-startup-source-trace.md.
#pragma once
#include "game/actors/npc/npc_actor.hpp"
#include "mission_simulation.hpp"
namespace bw::game {
class StagePresentation;
class WorldPopulation final : private PopulationReceivers {
 public:
  WorldPopulation(const MissionLoading&, const MissionSimulation&,
                  StagePresentation&, PlacementRandom&,
                  const std::shared_ptr<memory::Domain>&);
  ~WorldPopulation() override;
  WorldPopulation(const WorldPopulation&) = delete;
  WorldPopulation& operator=(const WorldPopulation&) = delete;
  WorldPopulation(WorldPopulation&&) = delete;
  WorldPopulation& operator=(WorldPopulation&&) = delete;
  void advance(double seconds);
  PopulationCheckpoint checkpoint() const {
    return population_.checkpoint(*this);
  }
  std::size_t active_count() const { return population_.active_count(); }
  std::size_t pending_count() const { return population_.pending_count(); }

 private:
  math::Vector3<float> project_ground(const SpawnCandidate&) const override;
  bool visible(const math::Vector3<float>&) const override;
  bool admit(PopulationIdentity, const PopulationActorState&) override;
  PopulationActorState observe(PopulationIdentity) const override;
  void retire(PopulationIdentity) override;
  const content::MissionPlacementDefinition& definition_;
  const MissionSimulation& simulation_;
  StagePresentation& presentation_;
  PlacementRandom& random_;
  std::shared_ptr<memory::Domain> domain_;
  MissionPopulation population_;
  memory::Vector<NpcInitialBodySettings> body_settings_;
  memory::Vector<std::shared_ptr<const PreparedNpcModel>> bodies_;
  memory::Vector<std::shared_ptr<const PreparedNpcModel>> weapons_;
  static constexpr std::size_t kActiveCapacity = 50;
  std::array<std::shared_ptr<NpcActor>, kActiveCapacity> actors_{};
};
}  // namespace bw::game
