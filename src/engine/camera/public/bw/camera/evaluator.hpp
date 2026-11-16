#ifndef BW_CAMERA_EVALUATOR_HPP
#define BW_CAMERA_EVALUATOR_HPP

#include "bw/camera/pose.hpp"
#include "bw/camera/rig.hpp"
#include "bw/input/player_input.hpp"
#include "bw/math/rotation/quaternion.hpp"
#include "bw/math/vector/vector3.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>

// Responsibility: how the view is made each tick
// (docs/architecture/camera.md, invariants 1 to 10, 12 and 16). The game
// loads its rigs and names a director; each tick the director says which
// rig is wanted, the main layer's stack blends from the old rig to the
// new, the rigs' input slots turn the view from the look action, and the
// result is one pose and a cut flag. The world calls evaluate where it
// updates cameras (bw/world/world.hpp, contract 5). The evaluator holds no
// rule about when the view changes.
//
// Contract.
//   1. load_rigs(rigs, input) loads the rigs of one table. Every name a
//      transition holds must be a rig of this table or one loaded before,
//      and every action a boom's slot names must be an action of `input`.
//      A rig holds at most one Boom node; SecondBoom carries the name of a
//      rig with two. UnknownRig and UnknownAction carry the name that does
//      not exist; DuplicateRig the name loaded twice; RigsFull, NodesFull and
//      TransitionsFull as named. On an error nothing of the call is
//      loaded. The table's storage is copied; `input` is observed and is
//      the one every later evaluation reads.
//   2. The context is what the rigs are evaluated for: the owner's
//      location and rotation, which the world writes before each
//      evaluation.
//   3. evaluate(step), once a tick, in this order:
//      a. The director, when one is set, writes its requests: each
//         activates a rig on a layer, optionally with a transition of its
//         own. A request for a rig on the Base, Global or Visual layer
//         adds the rig to that layer after the rigs already there, once.
//         A request on the Main layer pushes the rig on the stack
//         (contract 4). A request naming no loaded rig is not applied and
//         the evaluation reports UnknownRig.
//      b. The starting pose: the owner's location, its facing yaw, pitch
//         0 and no lens.
//      c. Base layer: its rigs run in order, each on the pose so far.
//      d. Main layer: every entry's blend advances and every entry's slot
//         turns (contract 5); then every rig on the stack is run alone,
//         on the pose the base layer left. The results are blended from
//         the bottom up: the bottom entry's pose, then for each entry
//         above it blend(pose so far, the entry's pose, the entry's
//         weight) (pose.hpp, contract 4).
//      e. Global layer: its rigs run in order on the blended pose. That
//         pose is kept as the evaluation's pre_visual.
//      f. Visual layer: its rigs run in order; the result is the pose.
//      g. When a control rotation sink is set and the evaluation is
//         valid, the sink is given the pose's yaw and pitch.
//   4. Pushing on the main stack. A request for the rig already on top
//      pushes nothing. Otherwise a new entry goes on top. With an empty
//      stack the entry's blend is a Pop, whatever transition the request
//      carries; otherwise the first transition found in this order: the
//      one the request carries; the exit transitions of the rig on top, in
//      table order; the enter transitions of the new rig, in table order;
//      each of the last two only when it matches the change (rig.hpp,
//      contract 4). With none found the blend is a Pop. A rig may be on
//      the stack more than once: pushing the rig a blend is leaving adds a
//      third entry, whose blend starts from its
//      beginning. StackFull, reported by the evaluation, when the stack
//      has kMaxStackEntries; the request is then not applied.
//   5. Weights and slots. Each entry's blend advances by `step` in every
//      evaluation, the one that pushed it included; its weight is
//      blend_factor at its elapsed time over its time (rig.hpp, contract
//      3). The bottom entry's weight is 1. Each entry keeps its boom's
//      slot value, a yaw and a pitch: a pushed entry's starts at the
//      value of the entry that was on top, and at 0 when the stack was
//      empty or that rig had no boom. In every evaluation each entry's
//      slot turns as rig.hpp contract 2 says, reading the action's value
//      as of this tick's input evaluation (zero when the action did not
//      fire), with the speed the entries' slot speeds blended from the
//      bottom up by this evaluation's weights, as the poses are. So
//      while one rig blends to another the turn rate goes from the one's
//      to the other's, and the view does not turn when a rig is pushed.
//      After the blending, when an entry's weight is 1 the entries below
//      it are removed; with several such, below the highest.
//   6. The cut flag of an evaluation is set when an entry whose blend is
//      a Pop is in its first evaluation, and it carries through the
//      blending whenever an entry that is a cut has a weight above 0. It
//      is false in every other evaluation.
//   7. stack() is the main stack after the last evaluation, bottom first,
//      each entry with its rig and weight. evaluation() is the last
//      evaluation; before the first, and while the main stack is empty,
//      it is not valid.
//   8. The director, the sink and the player's input are observed; the evaluator
//      owns its rigs' copies and nothing else. It is created into a
//      unique_ptr and is neither copied nor moved.
//
// Core guidelines read: C.121 (the director and the sink are pure interfaces), R.3
// (observers), I.11 and R.1, E.27. Performance guidelines read: MEM.9
// (rigs and the stack are sized at creation; evaluate allocates nothing),
// EMB.2 (a fixed stack).
namespace bw::camera {

struct RigId {
  std::uint32_t value{0xFFFFFFFFU};
  [[nodiscard]] friend constexpr bool operator==(RigId, RigId) noexcept = default;
};

enum class Layer : std::uint8_t { Base, Main, Global, Visual };

struct RigRequest {
  RigId rig{};
  Layer layer{Layer::Main};
  // The request's own transition, or null; viewed for the evaluation.
  const Transition* transition{};
};

inline constexpr std::size_t kMaxRequests = 8;
inline constexpr std::size_t kMaxStackEntries = 8;

// What names the rigs wanted this tick (camera.md, Director). The game
// implements it.
class Director {
public:
  // Writes at most out.size() requests and returns the count; `out` holds
  // kMaxRequests.
  [[nodiscard]] virtual std::size_t run(double step, std::span<RigRequest> out) = 0;

protected:
  Director() = default;
  Director(const Director&) = default;
  Director& operator=(const Director&) = default;
  ~Director() = default;
};

// What is given the evaluated rotation after each valid evaluation
// (contract 3g). The player's controller implements it.
class ControlRotationSink {
public:
  virtual void set_control_rotation(float yaw_degrees, float pitch_degrees) = 0;

protected:
  ControlRotationSink() = default;
  ControlRotationSink(const ControlRotationSink&) = default;
  ControlRotationSink& operator=(const ControlRotationSink&) = default;
  ~ControlRotationSink() = default;
};

struct Context {
  math::Vector3f owner_location{math::zero_init};
  math::Quaternion4f owner_rotation{math::identity_init};
};

enum class CameraError : std::uint8_t {
  None,
  UnknownRig,
  UnknownAction,
  SecondBoom,
  DuplicateRig,
  RigsFull,
  NodesFull,
  TransitionsFull,
  StackFull,
};

struct CameraFailure {
  CameraError error{CameraError::None};
  // The rig or action name the error is about; empty otherwise.
  std::string name{};
  [[nodiscard]] bool ok() const noexcept { return error == CameraError::None; }
};

struct StackEntry {
  RigId rig{};
  float weight{};
};

struct Evaluation {
  Pose pose{};
  Pose pre_visual{};
  bool cut{};
  bool valid{};
  // StackFull when a request of this evaluation was not applied because
  // the stack was full; UnknownRig when a request named no loaded rig.
  CameraError error{CameraError::None};
};

struct EvaluatorDescription {
  std::uint32_t max_rigs{16};
  std::uint32_t max_nodes{128};
  std::uint32_t max_transitions{32};
};

class Evaluator {
public:
  [[nodiscard]] static std::unique_ptr<Evaluator> create(const EvaluatorDescription& description);
  Evaluator(const Evaluator&) = delete;
  Evaluator& operator=(const Evaluator&) = delete;
  ~Evaluator();

  [[nodiscard]] CameraFailure load_rigs(std::span<const RigDescription> rigs,
                                        const input::PlayerInput& input);
  // The rig of that name, or an id equal to RigId{} when none.
  [[nodiscard]] RigId find_rig(std::string_view name) const noexcept;
  // Precondition: `rig` is a rig of this evaluator.
  [[nodiscard]] std::string_view rig_name(RigId rig) const noexcept;

  // Observed; null for none.
  void set_director(Director* director) noexcept;
  // Observed; null for none.
  void set_control_rotation_sink(ControlRotationSink* sink) noexcept;
  [[nodiscard]] Context& context() noexcept;

  // Contract 3. Precondition: step is finite and not negative.
  const Evaluation& evaluate(double step);
  [[nodiscard]] const Evaluation& evaluation() const noexcept;
  [[nodiscard]] std::span<const StackEntry> stack() const noexcept;

private:
  Evaluator();
  struct State;
  std::unique_ptr<State> state_;
};

} // namespace bw::camera
#endif
