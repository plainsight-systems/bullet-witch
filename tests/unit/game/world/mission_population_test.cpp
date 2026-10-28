// Synthetic STBL contracts verify schema joins, original inclusive 2D triggers,
// cell-based placement distribution and failure publication. Tests use no game
// files and no processes. Design Core I.1 (#ri-explicit), implementation
// SL.con.3 (#rsl-bounds), both-phase performance MEM.6/MEM.9
// (guidelines/memory/) exercise injected metadata and fixed expansion storage.
#include "game/world/mission_population.hpp"

#include <limits>
#include <numbers>

#include "../../../fixtures/camera_fixture.hpp"
namespace {
using camera_fixture::require;
using camera_fixture::scalar;
using camera_fixture::word;
constexpr std::size_t kShapes = 64, kSections = 256, kChara = 448,
                      kGroups = 696, kTraps = 840, kStrings = 872;
void name(bw::resources::Bytes& bytes, std::size_t offset,
          std::string_view value) {
  std::copy(value.begin(), value.end(),
            bytes.begin() + static_cast<std::ptrdiff_t>(offset));
}
void cell(bw::resources::Bytes& bytes, std::size_t base, std::size_t column,
          std::uint32_t value) {
  word(bytes, base + column * 8, 1);
  word(bytes, base + column * 8 + 4, value);
}
bw::resources::Bytes fixture() {
  bw::resources::Bytes bytes(1024);
  name(bytes, 0, "STBL");
  word(bytes, 4, 1);
  word(bytes, 12, 2);
  word(bytes, 16, kShapes);
  word(bytes, 28, 3);
  word(bytes, 32, kSections);
  name(bytes, kShapes + 32, "box");
  scalar(bytes, kShapes, 10);
  scalar(bytes, kShapes + 8, 20);
  scalar(bytes, kShapes + 16, 4);
  scalar(bytes, kShapes + 24, 2);
  name(bytes, kShapes + 96 + 32, "orb");
  scalar(bytes, kShapes + 96, -10);
  scalar(bytes, kShapes + 96 + 16, 3);
  word(bytes, kShapes + 96 + 64, 1);
  name(bytes, kSections, "chara");
  word(bytes, kSections + 32, 1);
  word(bytes, kSections + 36, 31);
  word(bytes, kSections + 40, kChara);
  name(bytes, kSections + 64, "group");
  word(bytes, kSections + 64 + 32, 2);
  word(bytes, kSections + 64 + 36, 9);
  word(bytes, kSections + 64 + 40, kGroups);
  name(bytes, kSections + 128, "trap");
  word(bytes, kSections + 128 + 32, 2);
  word(bytes, kSections + 128 + 36, 2);
  word(bytes, kSections + 128 + 40, kTraps);
  name(bytes, kStrings, "enemy");
  name(bytes, kStrings + 8, "2");
  name(bytes, kStrings + 12, "1");
  cell(bytes, kChara, 0, 7);
  cell(bytes, kChara, 1, kStrings);
  cell(bytes, kChara, 2, 3);
  cell(bytes, kChara, 3, 99);
  for (std::size_t row = 0; row < 2; ++row) {
    const auto base = kGroups + row * 72;
    cell(bytes, base, 0, 10);
    cell(bytes, base, 1, static_cast<std::uint32_t>(kShapes + row * 96 + 32));
    cell(bytes, base, 3, 7);
    cell(bytes, base, 4, static_cast<std::uint32_t>(kStrings + 8 + row * 4));
    cell(bytes, kTraps + row * 16, 0, 10);
    cell(bytes, kTraps + row * 16, 1,
         static_cast<std::uint32_t>(kShapes + row * 96 + 32));
  }
  return bytes;
}
class FixedRandom final : public bw::game::PlacementRandom {
 public:
  std::uint16_t next_random15() override { return value; }
  std::uint16_t value{};
};
class PopulationFixture final : public bw::game::PopulationReceivers {
 public:
  bw::math::Vector3<float> project_ground(
      const bw::game::SpawnCandidate& candidate) const override {
    auto position = candidate.position;
    position[1] = 4;
    return position;
  }
  bool visible(const bw::math::Vector3<float>&) const override {
    return on_screen;
  }
  bool admit(bw::game::PopulationIdentity id,
             const bw::game::PopulationActorState& state) override {
    if (unavailable) return false;
    require(!live.at(id.value), "Admission must not duplicate a live actor");
    states.at(id.value) = state;
    states.at(id.value).retirement_eligible = true;
    live.at(id.value) = true;
    ++admissions;
    return true;
  }
  bw::game::PopulationActorState observe(
      bw::game::PopulationIdentity id) const override {
    require(live.at(id.value), "Only live actors can be observed");
    return states.at(id.value);
  }
  void retire(bw::game::PopulationIdentity id) override {
    require(live.at(id.value), "Retirement requires a live actor");
    live.at(id.value) = false;
    ++retirements;
  }
  std::array<bw::game::PopulationActorState, 257> states{};
  std::array<bool, 257> live{};
  unsigned admissions{}, retirements{};
  bool unavailable{}, on_screen{};
};
template <typename Action>
void rejects(Action action) {
  bool failed = false;
  try {
    action();
  } catch (const bw::resources::Error&) {
    failed = true;
  }
  require(failed, "Invalid original placement must fail explicitly");
}
}  // namespace
void mission_population_tests() {
  const auto domain =
      bw::memory::Domain::create(std::make_shared<camera_fixture::Backing>());
  auto bytes = fixture();
  const auto definition =
      bw::content::MissionPlacementDefinition::decode(bytes, domain);
  require(definition.groups().size() == 2 &&
              definition.archetypes().size() == 1 &&
              definition.triggers().size() == 2 &&
              definition.archetypes()[0].life == 99,
          "Placement rows retain archetype joins and source identities");
  require(definition.contains_2d(0, {14, 900, 22}) &&
              !definition.contains_2d(0, {14.01F, 0, 22}) &&
              definition.contains_2d(1, {-7, 900, 0}) &&
              !definition.contains_2d(1, {-6.99F, 0, 0}),
          "Inclusive original trap boundaries discard height");
  FixedRandom random;
  bw::game::MissionPopulationExpansion expansion;
  const auto candidates = expansion.prepare(definition, 10, random);
  require(candidates.size() == 3 &&
              candidates[0].position == std::array<float, 3>{7, 0, 19} &&
              candidates[1].position == std::array<float, 3>{7, 0, 21} &&
              candidates[2].position == std::array<float, 3>{-10, 0, 0} &&
              candidates[0].admission_distance == 100 &&
              candidates[0].retirement_distance == 100,
          "Source cell distribution and center branch preserve independent "
          "instances");
  require(candidates[0].ground_projection_required && !candidates[0].ground_3d,
          "Ground projection remains an explicit world query");
  {
    bw::game::PlacementRandomStream stream(1);
    require(stream.next_random15() == 41 && stream.next_random15() == 18467,
            "Windows CRT sequence remains distinct from the game RNG");
    PopulationFixture receiver;
    bw::game::MissionPopulation population(definition, domain);
    bw::game::PopulationObservation view{{10, 0, 20}, {10, 0, 20}, false};
    population.advance(view, random, receiver);
    require(
        population.fired_count() == 2 && population.pending_count() == 6 &&
            population.active_count() == 0,
        "Trigger row state preserves duplicate groups and next-pass admission");
    receiver.unavailable = true;
    population.advance(view, random, receiver);
    require(
        population.pending_count() == 6 && receiver.admissions == 0,
        "Unavailable factory retains pending requests without losing groups");
    receiver.unavailable = false;
    population.advance(view, random, receiver);
    require(population.active_count() == 1 && population.pending_count() == 5 &&
                receiver.states[1].placement.position[1] == 4 &&
                receiver.states[1].life == 99,
            "One grounded actor enters with authored life per pass");
    const auto snapshot = population.checkpoint(receiver);
    require(snapshot.count == 6 && snapshot.trigger_count == 2 &&
                snapshot.fired[0] == 1 && snapshot.fired[1] == 1 &&
                !snapshot.records[0].deferred &&
                snapshot.records[0].identity.value == 1 &&
                snapshot.records[1].deferred &&
                snapshot.records[0].state.life == 99,
            "Checkpoint captures living actors before deferred source records");
    receiver.states[1].life = 73;
    require(snapshot.records[0].state.life == 99,
            "Owned checkpoint does not alias later actor mutation");
    view.camera = {1000, 0, 0};
    population.advance(view, random, receiver);
    require(population.active_count() == 0 && receiver.retirements == 1,
            "Distant eligible actors return to storage");
    view.camera = {10, 0, 20};
    population.advance(view, random, receiver);
    require(!receiver.live[1],
            "Retired actor observes original two-pass delay");
    population.advance(view, random, receiver);
    require(receiver.live[1] && receiver.states[1].life == 73 &&
                population.fired_count() == 2,
            "Readmission preserves identity/life and never refires traps");
  }
  {
    PopulationFixture receiver;
    bw::game::MissionPopulation population(definition, domain);
    bw::game::PopulationObservation view{{10, 0, 20}, {10, 0, 20}, false};
    population.advance(view, random, receiver);
    for (unsigned i = 0; i < 6; ++i) {
      population.advance(view, random, receiver);
    }
    require(population.active_identities().size() == 6 &&
                population.active_identities().front().value == 1,
            "Active identities preserve admission order");
    for (std::size_t i = 2; i <= 6; ++i) {
      receiver.states.at(i).retirement_eligible = false;
    }
    view.camera = {1000, 0, 0};
    population.advance(view, random, receiver);
    require(population.active_identities().size() == 5 &&
                population.active_identities().front().value == 2,
            "Retirement preserves remaining traversal order");
    view.camera = {10, 0, 20};
    population.advance(view, random, receiver);
    population.advance(view, random, receiver);
    require(population.active_identities().size() == 6 &&
                population.active_identities().front().value == 2 &&
                population.active_identities().back().value == 1,
            "Readmission appends identity instead of reusing traversal slot");
  }
  {
    using bw::game::PopulationFamily;
    require(bw::game::population_family_flags("PW0") == 1 &&
                bw::game::population_family_flags("hM0") == 8 &&
                bw::game::population_family_flags("HW1") == 8 &&
                bw::game::population_family_flags("HR0") == 4 &&
                bw::game::population_family_flags("eb0") == 2,
            "Deferred source family classification changed");
    for (const auto family :
         {PopulationFamily::enemy, PopulationFamily::resistance,
          PopulationFamily::citizen}) {
      auto removal_bytes = fixture();
      name(removal_bytes, kStrings,
           family == PopulationFamily::enemy        ? "ea0\0"
           : family == PopulationFamily::resistance ? "hr0\0"
                                                    : "hm0\0");
      removal_bytes[kStrings + 3] = 0;
      const auto removal_definition =
          bw::content::MissionPlacementDefinition::decode(removal_bytes,
                                                          domain);
      bw::game::MissionPopulation population(removal_definition, domain);
      PopulationFixture receiver;
      const bw::game::PopulationObservation view{
          {10, 0, 20}, {10, 0, 20}, false};
      population.advance(view, random, receiver);
      population.advance(view, random, receiver);
      population.remove_family(family == PopulationFamily::enemy
                                   ? PopulationFamily::citizen
                                   : PopulationFamily::enemy,
                               receiver);
      require(population.active_count() == 1 && population.pending_count() == 5,
              "Family removal touched an unrelated active or pending actor");
      population.remove_family(family, receiver);
      require(population.active_count() == 0 &&
                  population.pending_count() == 0 &&
                  receiver.retirements == 1 && population.fired_count() == 2,
              "Removal must retire matching live and pending without resetting "
              "triggers");
      population.remove_family(family, receiver);
      population.advance(view, random, receiver);
      require(
          receiver.retirements == 1 && population.pending_count() == 0,
          "Repeated family removal replayed retirement or triggered respawn");
    }
  }
  random.value = std::numeric_limits<std::uint16_t>::max();
  {
    PopulationFixture receiver;
    bw::game::MissionPopulation population(definition, domain);
    bw::game::PopulationObservation view{{10, 0, 20}, {10, 0, 20}, false};
    random.value = 0;
    population.advance(view, random, receiver);
    for (unsigned i = 0; i < 6; ++i) population.advance(view, random, receiver);
    const auto identity = population.active_identities()[2];
    receiver.states.at(identity.value).life = 0;
    require(
        population.retire_dead(identity, receiver) &&
            population.active_count() == 5 && population.pending_count() == 0 &&
            population.fired_count() == 2,
        "Death removes one identity without deferred residency or refiring");
    require(!population.retire_dead(identity, receiver),
            "Repeated corpse retirement cannot remove another actor");
    population.advance(view, random, receiver);
    require(population.active_count() == 5 && !receiver.live.at(identity.value),
            "Dead actor does not return through admission on the next frame");
    random.value = std::numeric_limits<std::uint16_t>::max();
  }
  rejects([&] { (void)expansion.prepare(definition, 10, random); });
  require(expansion.candidates().empty(),
          "Failed expansion publishes no partial population");
  require(expansion.prepare(definition, 99, random).empty(),
          "Absent source group produces no actors");
  auto rotated = fixture();
  scalar(rotated, kShapes + 28, std::numbers::pi_v<float> / 2);
  const auto rotated_definition =
      bw::content::MissionPlacementDefinition::decode(rotated, domain);
  require(rotated_definition.contains_2d(0, {11, 0, 23}) &&
              !rotated_definition.contains_2d(0, {13, 0, 20}),
          "Authored region yaw controls original containment");
  auto malformed = fixture();
  cell(malformed, kGroups, 3, 999);
  rejects([&] {
    (void)bw::content::MissionPlacementDefinition::decode(malformed, domain);
  });
  auto unsupported = fixture();
  cell(unsupported, kGroups, 5, 2);
  rejects([&] {
    (void)bw::content::MissionPlacementDefinition::decode(unsupported, domain);
  });
  auto invalid = fixture();
  scalar(invalid, kShapes, std::numeric_limits<float>::infinity());
  rejects([&] {
    (void)bw::content::MissionPlacementDefinition::decode(invalid, domain);
  });
}
