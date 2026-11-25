// What exists at one moment and the order of one tick (bw/world/world.hpp).
#include "bw/world/world.hpp"

#include "bw/camera/evaluator.hpp"
#include "bw/world/clocks.hpp"
#include "bw/world/object.hpp"
#include "bw/world/tick_function.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace bw::world {

struct World::State {
  struct Function {
    std::string name{};
    Stage stage{Stage::BeforeSimulation};
    TickFunction* function{};
    bool enabled{true};
  };
  struct Prerequisite {
    std::uint32_t later{};
    std::uint32_t earlier{};
  };
  // Where a function is in this tick's queueing walk.
  enum class Mark : std::uint8_t { Unvisited, Visiting, Queued };

  WorldDescription description{};
  Clocks clocks{};
  std::vector<PublishedObject> objects{};
  std::vector<Function> functions{};
  std::vector<Prerequisite> prerequisites{};
  camera::Evaluator* camera{};
  ObjectId camera_owner{};
  bool ticking{};
  // Whether a tick has ended: before it the publication follows
  // create_object.
  bool ticked{};
  // This tick's camera evaluation, published after the Late stage.
  camera::Pose evaluated_camera{};
  bool evaluated_valid{};
  bool evaluated_cut{};
  // This tick's queue, in the order the walk queued them, each function's
  // stage after its prerequisites, and the walk's marks.
  std::vector<std::uint32_t> queue{};
  std::vector<Stage> run_stage{};
  std::vector<Mark> marks{};
  std::vector<PrerequisiteCycle> cycles{};
  // The publication's storage.
  std::vector<PublishedObject> published{};
  camera::Pose published_camera{};
  bool camera_valid{};
  bool camera_cut{};

  // Unvisited prerequisites are queued first, recursively; one still on the walk would close a
  // cycle and is not used, with a report; a disabled one is ignored. The function runs in the
  // latest of its own stage and its prerequisites' stages.
  void queue_function(std::uint32_t index) {
    marks[index] = Mark::Visiting;
    Stage stage = functions[index].stage;
    for (const Prerequisite& prerequisite : prerequisites) {
      if (prerequisite.later != index || !functions[prerequisite.earlier].enabled) {
        continue;
      }
      const std::uint32_t earlier = prerequisite.earlier;
      if (marks[earlier] == Mark::Visiting) {
        cycles.push_back(PrerequisiteCycle{functions[index].name, functions[earlier].name});
        continue;
      }
      if (marks[earlier] == Mark::Unvisited) {
        queue_function(earlier);
      }
      stage = std::max(stage, run_stage[earlier]);
    }
    run_stage[index] = stage;
    marks[index] = Mark::Queued;
    queue.push_back(index);
  }

  void run_stage_functions(Stage stage) {
    TickContext context{};
    context.clocks = clocks;
    context.stage = stage;
    for (const std::uint32_t index : queue) {
      if (run_stage[index] == stage) {
        functions[index].function->tick(context);
      }
    }
  }

  // Contract 6: the objects, then the camera this tick evaluated; the cut
  // flag stays set until cleared.
  void publish() {
    published.assign(objects.begin(), objects.end());
    published_camera = evaluated_camera;
    camera_valid = evaluated_valid;
    camera_cut = camera_cut || evaluated_cut;
  }
};

World::World() : state_(std::make_unique<State>()) {}
World::~World() = default;

std::unique_ptr<World> World::create(const WorldDescription& description) {
  std::unique_ptr<World> world(new World());
  State& state = *world->state_;
  state.description = description;
  state.objects.reserve(description.max_objects);
  state.published.reserve(description.max_objects);
  state.functions.reserve(description.max_tick_functions);
  state.queue.reserve(description.max_tick_functions);
  state.run_stage.reserve(description.max_tick_functions);
  state.marks.reserve(description.max_tick_functions);
  state.prerequisites.reserve(description.max_prerequisites);
  // At most one report per prerequisite.
  state.cycles.reserve(description.max_prerequisites);
  return world;
}

ObjectResult World::create_object(const Transform& transform) {
  State& state = *state_;
  if (state.ticking) {
    return ObjectResult{ObjectId{}, WorldError::TickInProgress};
  }
  if (state.objects.size() >= state.description.max_objects) {
    return ObjectResult{ObjectId{}, WorldError::ObjectsFull};
  }
  const ObjectId id{static_cast<std::uint32_t>(state.objects.size())};
  state.objects.push_back(PublishedObject{id, transform});
  // Before the first tick the publication holds the objects as created;
  // after it, an object appears at the end of the next tick (contract 6).
  if (!state.ticked) {
    state.published.push_back(PublishedObject{id, transform});
  }
  return ObjectResult{id, WorldError::None};
}

const Transform& World::transform(ObjectId id) const noexcept {
  return state_->objects[id.value].transform;
}

TickFunctionResult World::register_tick_function(std::string_view name, Stage stage,
                                                 TickFunction& function) {
  State& state = *state_;
  if (state.ticking) {
    return TickFunctionResult{TickFunctionId{}, WorldError::TickInProgress};
  }
  if (state.functions.size() >= state.description.max_tick_functions) {
    return TickFunctionResult{TickFunctionId{}, WorldError::FunctionsFull};
  }
  const TickFunctionId id{static_cast<std::uint32_t>(state.functions.size())};
  state.functions.push_back(State::Function{std::string(name), stage, &function, true});
  state.run_stage.push_back(stage);
  state.marks.push_back(State::Mark::Unvisited);
  return TickFunctionResult{id, WorldError::None};
}

WorldError World::add_prerequisite(TickFunctionId later_function, TickFunctionId earlier_function) {
  State& state = *state_;
  if (state.ticking) {
    return WorldError::TickInProgress;
  }
  if (later_function.value >= state.functions.size() ||
      earlier_function.value >= state.functions.size()) {
    return WorldError::UnknownFunction;
  }
  if (state.prerequisites.size() >= state.description.max_prerequisites) {
    return WorldError::PrerequisitesFull;
  }
  state.prerequisites.push_back(State::Prerequisite{later_function.value, earlier_function.value});
  return WorldError::None;
}

WorldError World::set_enabled(TickFunctionId function, bool enabled) {
  State& state = *state_;
  if (state.ticking) {
    return WorldError::TickInProgress;
  }
  if (function.value >= state.functions.size()) {
    return WorldError::UnknownFunction;
  }
  state.functions[function.value].enabled = enabled;
  return WorldError::None;
}

WorldError World::set_camera(camera::Evaluator& evaluator, ObjectId owner) {
  State& state = *state_;
  if (owner.value >= state.objects.size()) {
    return WorldError::UnknownObject;
  }
  state.camera = &evaluator;
  state.camera_owner = owner;
  return WorldError::None;
}

TickReport World::tick(double step) {
  State& state = *state_;
  state.ticking = true;
  advance(state.clocks, step);

  state.queue.clear();
  state.cycles.clear();
  camera::CameraError camera_error = camera::CameraError::None;
  std::fill(state.marks.begin(), state.marks.end(), State::Mark::Unvisited);
  for (std::uint32_t index = 0; index < state.functions.size(); ++index) {
    if (state.functions[index].enabled && state.marks[index] == State::Mark::Unvisited) {
      state.queue_function(index);
    }
  }

  for (std::uint32_t stage = 0; stage < kStageCount; ++stage) {
    const auto current = static_cast<Stage>(stage);
    if (current == Stage::Late) {
      if (state.camera != nullptr) {
        camera::Context& context = state.camera->context();
        const Transform& owner = state.objects[state.camera_owner.value].transform;
        context.owner_location = owner.location;
        context.owner_rotation = owner.rotation;
        const camera::Evaluation& evaluation = state.camera->evaluate(step);
        state.evaluated_camera = evaluation.pose;
        state.evaluated_valid = evaluation.valid;
        state.evaluated_cut = evaluation.cut;
        camera_error = evaluation.error;
      }
    }
    state.run_stage_functions(current);
  }

  state.publish();
  state.ticked = true;
  state.ticking = false;
  return TickReport{std::span<const PrerequisiteCycle>(state.cycles), camera_error};
}

const Clocks& World::clocks() const noexcept { return state_->clocks; }

Publication World::publication() const noexcept {
  const State& state = *state_;
  return Publication{std::span<const PublishedObject>(state.published), state.published_camera,
                     state.camera_valid, state.camera_cut};
}

void World::clear_camera_cut() noexcept { state_->camera_cut = false; }

} // namespace bw::world
