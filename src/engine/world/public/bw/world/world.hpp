#ifndef BW_WORLD_WORLD_HPP
#define BW_WORLD_WORLD_HPP

#include "bw/camera/evaluator.hpp"
#include "bw/camera/pose.hpp"
#include "bw/world/clocks.hpp"
#include "bw/world/object.hpp"
#include "bw/world/tick_function.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>

// Responsibility: what exists at one moment and the order in which one
// tick changes it (docs/architecture/world.md). The world owns every
// object's transform and lifetime, advances its clocks by the step it is
// handed, runs the registered tick functions in stages, updates the camera
// after them, and publishes what the renderer reads. It holds no game
// rule and no renderer: a run with nothing drawing runs the same tick
// (invariant 16).
//
// Contract.
//   1. Objects. create_object(transform) makes an object with that
//      transform; ObjectsFull when the description's capacity is used;
//      TickInProgress, and nothing made, when called from inside tick().
//      transform(id) reads it. Nothing outside the world writes a
//      transform.
//   2. Tick functions. register_tick_function(name, stage, function)
//      registers `function` (observed, tick_function.hpp contract 3) to
//      run in `stage`, enabled; FunctionsFull at capacity. The name is
//      copied and is what a cycle report says.
//      add_prerequisite(function, prerequisite) makes `prerequisite`
//      finish before `function` starts; UnknownFunction for an id the
//      world did not give, PrerequisitesFull at capacity.
//      set_enabled(function, enabled) takes it out of or back into the
//      next tick's queue. register_tick_function, add_prerequisite,
//      set_enabled and create_object return TickInProgress, and change
//      nothing, when called from inside tick().
//   3. tick(step), in this order: the clocks advance by `step`
//      (clocks.hpp); every enabled tick function is queued, before any
//      stage runs; the stages run in order, each to its end before the
//      next starts; the camera is updated (contract 5); the Late stage
//      runs; the publication is made (contract 6). Precondition: step is
//      finite and not negative, and tick is not called from inside tick.
//   4. Order. A function runs in the later of its own stage and the
//      stages its enabled prerequisites run in, never an earlier one, and
//      only after every enabled prerequisite has run. A disabled
//      prerequisite is ignored. Functions of one stage that no
//      prerequisite orders run in registration order. A prerequisite that
//      would close a cycle is not used: the function runs as if it did
//      not name it, and the tick's report holds one entry naming both
//      functions. So the same steps, registrations and inputs in the same
//      order give the same calls in the same order (invariant 2).
//   5. The camera. set_camera(evaluator, owner) names the camera the
//      world updates and the object it is evaluated for; the evaluator is
//      observed. Each tick, after the AfterSimulation stage and before the
//      Late stage, the world writes the owner's location and rotation
//      into the evaluator's context and calls evaluator.evaluate(step)
//      (bw/camera/evaluator.hpp). UnknownObject for an owner the world
//      did not make. With no camera set the published camera is not
//      valid.
//   6. Publication. publication() is what the consumers read, as of the
//      end of the last tick: every object's id and transform, in creation
//      order; the camera's pose and whether it is valid; and the camera
//      cut flag. The cut flag is set when a tick's evaluation reports a
//      cut and stays set until clear_camera_cut(), which the host calls
//      after the frame that used it is drawn, or after reading it when
//      nothing draws. Before the first tick the publication holds the
//      objects as created and no valid camera.
//   7. Failures are WorldError by name; no call falls back. A world is
//      created into a unique_ptr and is neither copied nor moved.
//
// Core guidelines read: I.11 and R.1 (created into a unique_ptr), R.3 and
// F.7 (tick functions and the evaluator are observed), E.27 (named
// errors), I.1 (the step is an argument; no clock is read). Performance
// guidelines read: MEM.9 (every table is sized at creation; a tick
// allocates nothing), EMB.2 (fixed capacities from the description).
namespace bw::world {

struct WorldDescription {
  std::uint32_t max_objects{16};
  std::uint32_t max_tick_functions{16};
  std::uint32_t max_prerequisites{32};
};

enum class WorldError : std::uint8_t {
  None,
  ObjectsFull,
  FunctionsFull,
  PrerequisitesFull,
  UnknownFunction,
  UnknownObject,
  TickInProgress,
};

struct ObjectResult {
  ObjectId id{};
  WorldError error{WorldError::None};
  [[nodiscard]] bool ok() const noexcept { return error == WorldError::None; }
};

struct TickFunctionResult {
  TickFunctionId id{};
  WorldError error{WorldError::None};
  [[nodiscard]] bool ok() const noexcept { return error == WorldError::None; }
};

// A prerequisite that was not used because it would close a cycle; the
// names view the world's copies.
struct PrerequisiteCycle {
  std::string_view function{};
  std::string_view prerequisite{};
};

struct TickReport {
  std::span<const PrerequisiteCycle> cycles{};
  // The camera evaluation's error this tick (evaluator.hpp,
  // Evaluation::error).
  camera::CameraError camera_error{camera::CameraError::None};
};

struct PublishedObject {
  ObjectId id{};
  Transform transform{};
};

struct Publication {
  std::span<const PublishedObject> objects{};
  camera::Pose camera{};
  bool camera_valid{};
  bool camera_cut{};
};

class World {
public:
  [[nodiscard]] static std::unique_ptr<World> create(const WorldDescription& description);
  World(const World&) = delete;
  World& operator=(const World&) = delete;
  ~World();

  [[nodiscard]] ObjectResult create_object(const Transform& transform);
  // Precondition: `id` is an object of this world.
  [[nodiscard]] const Transform& transform(ObjectId id) const noexcept;

  [[nodiscard]] TickFunctionResult register_tick_function(std::string_view name, Stage stage,
                                                          TickFunction& function);
  // `earlier_function` is the prerequisite of `later_function`.
  [[nodiscard]] WorldError add_prerequisite(TickFunctionId later_function,
                                            TickFunctionId earlier_function);
  [[nodiscard]] WorldError set_enabled(TickFunctionId function, bool enabled);

  [[nodiscard]] WorldError set_camera(camera::Evaluator& evaluator, ObjectId owner);

  // Contract 3. The report views storage the next tick reuses.
  TickReport tick(double step);

  [[nodiscard]] const Clocks& clocks() const noexcept;
  [[nodiscard]] Publication publication() const noexcept;
  void clear_camera_cut() noexcept;

private:
  World();
  struct State;
  std::unique_ptr<State> state_;
};

} // namespace bw::world
#endif
