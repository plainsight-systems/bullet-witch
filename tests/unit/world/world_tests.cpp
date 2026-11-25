// Unit test: the world's tick, errors, clocks and publication
// (bw/world/world.hpp, bw/world/clocks.hpp), one table-driven scenario per
// invariant.
#include "bw/world/world.hpp"

#include "bw/camera/evaluator.hpp"
#include "bw/math/vector/vector3.hpp"
#include "bw/testing/harness.hpp"
#include "bw/world/clocks.hpp"
#include "bw/world/object.hpp"
#include "bw/world/tick_function.hpp"

#include <array>
#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace {

using bw::testing::require;
using bw::world::Stage;
using bw::world::WorldError;

struct Call {
  std::string_view name;
  Stage stage;
  [[nodiscard]] friend bool operator==(const Call&, const Call&) = default;
};

// A tick function that writes its name and stage to a shared log.
class Recorder final : public bw::world::TickFunction {
public:
  Recorder(std::string_view name, std::vector<Call>& log) : name_(name), log_(&log) {}
  void tick(const bw::world::TickContext& context) override {
    log_->push_back(Call{name_, context.stage});
  }

private:
  std::string_view name_;
  std::vector<Call>* log_;
};

// (world.hpp, contracts 3 and 4): stages in order; one stage in
// registration order; a prerequisite in a later stage demotes its
// dependent there, after it; a disabled prerequisite is ignored and a
// disabled function not called; a cycle runs both once and reports one
// entry naming both.
void tick_order() {
  struct Row {
    std::string_view name;
    Stage stage;
    // Index of the prerequisite row, or -1.
    int prerequisite;
    bool enabled;
  };
  constexpr std::array rows{
      Row{"late", Stage::Late, -1, true},
      Row{"first", Stage::BeforeSimulation, -1, true},
      Row{"second", Stage::BeforeSimulation, -1, true},
      Row{"during", Stage::DuringSimulation, -1, true},
      Row{"demoted", Stage::BeforeSimulation, 3, true},
      Row{"disabled", Stage::BeforeSimulation, -1, false},
      Row{"after_disabled", Stage::BeforeSimulation, 5, true},
      Row{"cycle_a", Stage::SimulationEnd, 8, true},
      Row{"cycle_b", Stage::SimulationEnd, 7, true},
  };
  std::vector<Call> log;
  std::vector<std::unique_ptr<Recorder>> recorders;
  std::vector<bw::world::TickFunctionId> ids;
  const std::unique_ptr<bw::world::World> world = bw::world::World::create({});
  for (const Row& row : rows) {
    recorders.push_back(std::make_unique<Recorder>(row.name, log));
    const bw::world::TickFunctionResult registered =
        world->register_tick_function(row.name, row.stage, *recorders.back());
    require(registered.ok(), "registers " + std::string(row.name));
    ids.push_back(registered.id);
  }
  for (std::size_t index = 0; index < rows.size(); ++index) {
    if (rows[index].prerequisite >= 0) {
      require(world->add_prerequisite(ids[index],
                                      ids[static_cast<std::size_t>(rows[index].prerequisite)]) ==
                  WorldError::None,
              index, "the prerequisite is added");
    }
    if (!rows[index].enabled) {
      require(world->set_enabled(ids[index], false) == WorldError::None, index, "disabled");
    }
  }
  const bw::world::TickReport report = world->tick(0.0);
  // The two functions of the cycle run once each in SimulationEnd; which
  // runs first is not promised.
  const Call cycle_a{"cycle_a", Stage::SimulationEnd};
  const Call cycle_b{"cycle_b", Stage::SimulationEnd};
  const bool cycle_once = log.size() == 8U && ((log[5] == cycle_a && log[6] == cycle_b) ||
                                               (log[5] == cycle_b && log[6] == cycle_a));
  if (cycle_once) {
    log.erase(log.begin() + 5, log.begin() + 7);
  }
  const std::vector<Call> expected{
      {"first", Stage::BeforeSimulation},          {"second", Stage::BeforeSimulation},
      {"after_disabled", Stage::BeforeSimulation}, {"during", Stage::DuringSimulation},
      {"demoted", Stage::DuringSimulation},        {"late", Stage::Late},
  };
  require(cycle_once && log == expected,
          "the calls are in stage order, then by prerequisite and registration");
  require(report.cycles.size() == 1U, "one cycle is reported");
  const bw::world::PrerequisiteCycle& cycle = report.cycles[0];
  require((cycle.function == "cycle_a" && cycle.prerequisite == "cycle_b") ||
              (cycle.function == "cycle_b" && cycle.prerequisite == "cycle_a"),
          "the report names both functions of the cycle");
}

// A tick function that tries to register another from inside the tick.
class Registrar final : public bw::world::TickFunction {
public:
  explicit Registrar(bw::world::World& world) : world_(&world) {}
  void tick(const bw::world::TickContext& /*context*/) override {
    error = world_->register_tick_function("inner", Stage::Late, *this).error;
  }
  WorldError error{WorldError::None};

private:
  bw::world::World* world_;
};

// (world.hpp, WorldError): one row per name, each breaking one rule.
void errors() {
  std::vector<Call> log;
  Recorder recorder("recorder", log);
  std::array<WorldError, 6> got{};
  {
    bw::world::WorldDescription description{};
    description.max_objects = 1;
    const auto world = bw::world::World::create(description);
    static_cast<void>(world->create_object({}));
    got[0] = world->create_object({}).error;
  }
  {
    bw::world::WorldDescription description{};
    description.max_tick_functions = 1;
    const auto world = bw::world::World::create(description);
    static_cast<void>(world->register_tick_function("one", Stage::Late, recorder));
    got[1] = world->register_tick_function("two", Stage::Late, recorder).error;
  }
  {
    bw::world::WorldDescription description{};
    description.max_prerequisites = 0;
    const auto world = bw::world::World::create(description);
    const auto one = world->register_tick_function("one", Stage::Late, recorder).id;
    const auto two = world->register_tick_function("two", Stage::Late, recorder).id;
    got[2] = world->add_prerequisite(one, two);
  }
  {
    const auto world = bw::world::World::create({});
    const auto one = world->register_tick_function("one", Stage::Late, recorder).id;
    got[3] = world->add_prerequisite(one, bw::world::TickFunctionId{7});
  }
  {
    const auto world = bw::world::World::create({});
    const auto evaluator = bw::camera::Evaluator::create({});
    got[4] = world->set_camera(*evaluator, bw::world::ObjectId{3});
  }
  {
    const auto world = bw::world::World::create({});
    Registrar registrar(*world);
    static_cast<void>(world->register_tick_function("registrar", Stage::Late, registrar));
    static_cast<void>(world->tick(0.0));
    got[5] = registrar.error;
  }
  constexpr std::array expected{WorldError::ObjectsFull,       WorldError::FunctionsFull,
                                WorldError::PrerequisitesFull, WorldError::UnknownFunction,
                                WorldError::UnknownObject,     WorldError::TickInProgress};
  for (std::size_t row = 0; row < expected.size(); ++row) {
    require(got[row] == expected[row], row, "the error named");
  }
}

// (clocks.hpp, contract 1; world.hpp, contract 6): each clock advances by
// the step and holds the last step; the publication holds the objects in
// creation order, as created before the first tick; with no camera set the
// published camera is not valid.
void clocks_and_publication() {
  const auto world = bw::world::World::create({});
  bw::world::Transform moved{};
  moved.location = bw::math::Vector3f{1.0F, 2.0F, 3.0F};
  const auto first = world->create_object({}).id;
  const auto second = world->create_object(moved).id;
  for (const bool ticked : {false, true}) {
    const bw::world::Publication published = world->publication();
    require(published.objects.size() == 2U && published.objects[0].id == first &&
                published.objects[1].id == second &&
                published.objects[1].transform.location == moved.location,
            ticked ? "published after a tick" : "published before the first tick");
    require(!published.camera_valid, "no camera is valid with none set");
    static_cast<void>(world->tick(0.25));
  }
  static_cast<void>(world->tick(0.5));
  const bw::world::Clocks& clocks = world->clocks();
  require(clocks.world == 1.0 && clocks.unpaused == 1.0 && clocks.real == 1.0 &&
              clocks.audio == 1.0 && clocks.step == 0.5,
          "every clock advanced by each step and the step is the last");
}

using Scenario = bw::testing::Scenario<void>;
constexpr std::array kScenarios{
    Scenario{"tick_order", &tick_order},
    Scenario{"errors", &errors},
    Scenario{"clocks_and_publication", &clocks_and_publication},
};

} // namespace

int main(int argc, char** argv) { return bw::testing::run_scenario(argc, argv, kScenarios); }
