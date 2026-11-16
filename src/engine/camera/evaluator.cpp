// How the view is made each tick (bw/camera/evaluator.hpp), with the rig
// nodes of bw/camera/rig.hpp.
#include "bw/camera/evaluator.hpp"

#include "bw/camera/pose.hpp"
#include "bw/camera/rig.hpp"
#include "bw/input/modifiers.hpp"
#include "bw/input/player_input.hpp"
#include "bw/math/vector/vector3.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace bw::camera {
namespace {

// A transition's rig names resolved to rig indices; kAnyRig for an empty
// name.
constexpr std::uint32_t kAnyRig = 0xFFFFFFFFU;
constexpr std::uint32_t kNoBoom = 0xFFFFFFFFU;

struct LoadedTransition {
  std::uint32_t from{kAnyRig};
  std::uint32_t to{kAnyRig};
  Blend blend{};
};

struct LoadedRig {
  std::string name{};
  std::uint32_t first_node{};
  std::uint32_t node_count{};
  std::uint32_t first_enter{};
  std::uint32_t enter_count{};
  std::uint32_t first_exit{};
  std::uint32_t exit_count{};
  // The rig's one Boom node, whose slot the rig's entry keeps; kNoBoom for
  // a rig without one.
  std::uint32_t boom{kNoBoom};
};

// A node with its slot's action resolved; the copied Node holds no view of
// the caller's storage (its slot's action name is cleared).
struct LoadedNode {
  Node node{};
  input::ActionId action{};
};

struct SlotValue {
  float yaw{};
  float pitch{};
};

struct Speed {
  float yaw{};
  float pitch{};
};

struct MainEntry {
  std::uint32_t rig{};
  Blend blend{};
  double elapsed{};
  float weight{};
  SlotValue slot{};
  bool first{true};
  Pose pose{};
};

struct LayerRig {
  std::uint32_t rig{};
  SlotValue slot{};
};

// An unset rig matches any.
bool matches(const LoadedTransition& transition, std::uint32_t from, std::uint32_t to) noexcept {
  return (transition.from == kAnyRig || transition.from == from) &&
         (transition.to == kAnyRig || transition.to == to);
}

// Brought into [0, wrap) first, then clamped where each limit is on.
float wrap_then_clamp(float value, float wrap, const AxisClamp& clamp) noexcept {
  if (wrap > 0.0F) {
    value = std::fmod(value, wrap);
    if (value < 0.0F) {
      value += wrap;
    }
    // A negative remainder smaller than half a step at `wrap` rounds to
    // `wrap` when added; the axis stays inside [0, wrap).
    if (value >= wrap) {
      value = 0.0F;
    }
  }
  if (clamp.clamp_lowest && value < clamp.lowest) {
    value = clamp.lowest;
  }
  if (clamp.clamp_highest && value > clamp.highest) {
    value = clamp.highest;
  }
  return value;
}

// Location moved by (x, y, z) along the pose's right, up and forward.
math::Vector3f move_in_pose(const math::Vector3f& location, float yaw, float pitch,
                            const Offset3& by) noexcept {
  return location + right(yaw) * by.x + up(yaw, pitch) * by.y + forward(yaw, pitch) * by.z;
}

} // namespace

struct Evaluator::State {
  EvaluatorDescription description{};
  const input::PlayerInput* input{};
  Director* director{};
  ControlRotationSink* sink{};
  Context context{};
  std::vector<LoadedRig> rigs{};
  std::vector<LoadedNode> nodes{};
  std::vector<LoadedTransition> transitions{};
  std::array<MainEntry, kMaxStackEntries> entries{};
  std::size_t entry_count{};
  std::array<StackEntry, kMaxStackEntries> stack{};
  std::vector<LayerRig> base{};
  std::vector<LayerRig> global{};
  std::vector<LayerRig> visual{};
  std::array<RigRequest, kMaxRequests> requests{};
  Evaluation evaluation{};

  [[nodiscard]] std::optional<std::uint32_t> rig_named(std::string_view name) const noexcept {
    for (std::size_t index = 0; index < rigs.size(); ++index) {
      if (rigs[index].name == name) {
        return static_cast<std::uint32_t>(index);
      }
    }
    return std::nullopt;
  }

  // The look action's value for a slot this tick: zero when it did not
  // fire (contract 5).
  [[nodiscard]] input::ActionValue look_value(input::ActionId action) const noexcept {
    const input::ActionState& state = input->state(action);
    return state.has(input::ActionEvent::Fired) ? state.value : input::ActionValue{};
  }

  // A slot turned by its action times `speed` times the step, then wrapped
  // and clamped (rig.hpp, contract 2).
  [[nodiscard]] SlotValue turn(const LoadedNode& boom, SlotValue slot, Speed speed,
                               double step) const noexcept {
    const input::ActionValue value = look_value(boom.action);
    const auto seconds = static_cast<float>(step);
    const InputSlot& settings = boom.node.slot;
    return SlotValue{wrap_then_clamp(slot.yaw + value.x * speed.yaw * seconds, settings.wrap_yaw,
                                     settings.clamp_yaw),
                     wrap_then_clamp(slot.pitch + value.y * speed.pitch * seconds,
                                     settings.wrap_pitch, settings.clamp_pitch)};
  }

  // A rig's nodes in order on `pose`, its booms reading `slot`.
  [[nodiscard]] Pose run_rig(std::uint32_t rig_index, Pose pose, SlotValue slot) const noexcept {
    const LoadedRig& rig = rigs[rig_index];
    for (std::uint32_t index = 0; index < rig.node_count; ++index) {
      const Node& node = nodes[rig.first_node + index].node;
      switch (node.kind) {
      case NodeKind::Attach:
        pose.location = context.owner_location;
        // A character stands upright, so its yaw is its whole turn.
        if (node.rotation) {
          pose.yaw_degrees = facing_yaw_degrees(context.owner_rotation);
          pose.pitch_degrees = 0.0F;
        }
        break;
      case NodeKind::Offset:
        pose.location =
            node.space == OffsetSpace::World
                ? pose.location + math::Vector3f{node.offset.x, node.offset.y, node.offset.z}
                : move_in_pose(pose.location, pose.yaw_degrees, pose.pitch_degrees, node.offset);
        break;
      case NodeKind::Boom:
        pose.yaw_degrees = slot.yaw;
        pose.pitch_degrees = slot.pitch;
        pose.location =
            move_in_pose(pose.location, pose.yaw_degrees, pose.pitch_degrees, node.offset);
        break;
      case NodeKind::Lens:
        pose.field_of_view_degrees = node.lens.field_of_view_degrees;
        pose.sensor_width = node.lens.sensor_width;
        pose.sensor_height = node.lens.sensor_height;
        pose.near_plane = node.lens.near_plane;
        pose.far_plane = node.lens.far_plane;
        break;
      }
    }
    return pose;
  }

  // Contract 4.
  [[nodiscard]] CameraError push(const RigRequest& request) noexcept {
    const std::uint32_t rig = request.rig.value;
    if (entry_count > 0 && entries[entry_count - 1].rig == rig) {
      return CameraError::None;
    }
    if (entry_count == kMaxStackEntries) {
      return CameraError::StackFull;
    }
    MainEntry entry{};
    entry.rig = rig;
    entry.blend = Blend{};
    if (entry_count > 0) {
      const MainEntry& top = entries[entry_count - 1];
      if (rigs[top.rig].boom != kNoBoom) {
        entry.slot = top.slot;
      }
      if (request.transition != nullptr) {
        entry.blend = request.transition->blend;
      } else if (const std::optional<Blend> found = find_transition(top.rig, rig);
                 found.has_value()) {
        entry.blend = *found;
      }
    }
    entries[entry_count] = entry;
    ++entry_count;
    return CameraError::None;
  }

  [[nodiscard]] std::optional<Blend> find_transition(std::uint32_t from,
                                                     std::uint32_t to) const noexcept {
    const LoadedRig& leaving = rigs[from];
    for (std::uint32_t index = 0; index < leaving.exit_count; ++index) {
      const LoadedTransition& transition = transitions[leaving.first_exit + index];
      if (matches(transition, from, to)) {
        return transition.blend;
      }
    }
    const LoadedRig& entering = rigs[to];
    for (std::uint32_t index = 0; index < entering.enter_count; ++index) {
      const LoadedTransition& transition = transitions[entering.first_enter + index];
      if (matches(transition, from, to)) {
        return transition.blend;
      }
    }
    return std::nullopt;
  }

  // A persistent layer's rigs in order on `pose`, each turning its own
  // slot at its own speed.
  [[nodiscard]] Pose run_layer(std::vector<LayerRig>& layer, Pose pose, double step) noexcept {
    for (LayerRig& entry : layer) {
      const LoadedRig& rig = rigs[entry.rig];
      if (rig.boom != kNoBoom) {
        const LoadedNode& boom = nodes[rig.boom];
        entry.slot = turn(boom, entry.slot,
                          Speed{boom.node.slot.speed_yaw, boom.node.slot.speed_pitch}, step);
      }
      pose = run_rig(entry.rig, pose, entry.slot);
    }
    return pose;
  }

  void add_to_layer(std::vector<LayerRig>& layer, std::uint32_t rig) {
    for (const LayerRig& entry : layer) {
      if (entry.rig == rig) {
        return;
      }
    }
    // A layer holds each rig once, so the storage reserved for every rig
    // at creation holds it.
    layer.push_back(LayerRig{rig, SlotValue{}});
  }
};

Evaluator::Evaluator() : state_(std::make_unique<State>()) {}
Evaluator::~Evaluator() = default;

std::unique_ptr<Evaluator> Evaluator::create(const EvaluatorDescription& description) {
  std::unique_ptr<Evaluator> evaluator(new Evaluator());
  State& state = *evaluator->state_;
  state.description = description;
  state.rigs.reserve(description.max_rigs);
  state.nodes.reserve(description.max_nodes);
  state.transitions.reserve(description.max_transitions);
  state.base.reserve(description.max_rigs);
  state.global.reserve(description.max_rigs);
  state.visual.reserve(description.max_rigs);
  return evaluator;
}

CameraFailure Evaluator::load_rigs(std::span<const RigDescription> rigs,
                                   const input::PlayerInput& input) {
  State& state = *state_;
  std::size_t node_total = 0;
  std::size_t transition_total = 0;
  for (std::size_t index = 0; index < rigs.size(); ++index) {
    const RigDescription& rig = rigs[index];
    if (state.rig_named(rig.name).has_value()) {
      return CameraFailure{CameraError::DuplicateRig, std::string(rig.name)};
    }
    for (std::size_t earlier = 0; earlier < index; ++earlier) {
      if (rigs[earlier].name == rig.name) {
        return CameraFailure{CameraError::DuplicateRig, std::string(rig.name)};
      }
    }
    node_total += rig.nodes.size();
    transition_total += rig.enter.size() + rig.exit.size();
  }
  if (state.rigs.size() + rigs.size() > state.description.max_rigs) {
    return CameraFailure{CameraError::RigsFull, {}};
  }
  if (state.nodes.size() + node_total > state.description.max_nodes) {
    return CameraFailure{CameraError::NodesFull, {}};
  }
  if (state.transitions.size() + transition_total > state.description.max_transitions) {
    return CameraFailure{CameraError::TransitionsFull, {}};
  }
  // A rig name of this table (its index after the rigs loaded before this
  // call, whichever of the table's rigs are already kept) or one loaded
  // before; kAnyRig for empty.
  const std::size_t first_of_table = state.rigs.size();
  const auto resolve = [&](std::string_view name) -> std::optional<std::uint32_t> {
    if (name.empty()) {
      return kAnyRig;
    }
    for (std::size_t index = 0; index < rigs.size(); ++index) {
      if (rigs[index].name == name) {
        return static_cast<std::uint32_t>(first_of_table + index);
      }
    }
    return state.rig_named(name);
  };
  // Every check before anything is kept, so an error loads nothing.
  for (const RigDescription& rig : rigs) {
    for (const std::span<const Transition> list : {rig.enter, rig.exit}) {
      for (const Transition& transition : list) {
        for (const std::string_view name : {transition.from, transition.to}) {
          if (!resolve(name).has_value()) {
            return CameraFailure{CameraError::UnknownRig, std::string(name)};
          }
        }
      }
    }
    std::size_t booms = 0;
    for (const Node& node : rig.nodes) {
      if (node.kind != NodeKind::Boom) {
        continue;
      }
      if (input.find_action(node.slot.action) == input::ActionId{}) {
        return CameraFailure{CameraError::UnknownAction, std::string(node.slot.action)};
      }
      if (++booms > 1) {
        return CameraFailure{CameraError::SecondBoom, std::string(rig.name)};
      }
    }
  }
  for (const RigDescription& description : rigs) {
    LoadedRig rig{};
    rig.name = std::string(description.name);
    rig.first_node = static_cast<std::uint32_t>(state.nodes.size());
    rig.node_count = static_cast<std::uint32_t>(description.nodes.size());
    for (const Node& node : description.nodes) {
      LoadedNode loaded{};
      loaded.node = node;
      loaded.node.slot.action = {};
      if (node.kind == NodeKind::Boom) {
        loaded.action = input.find_action(node.slot.action);
        rig.boom = static_cast<std::uint32_t>(state.nodes.size());
      }
      state.nodes.push_back(loaded);
    }
    const auto keep = [&](std::span<const Transition> list, std::uint32_t& first,
                          std::uint32_t& count) {
      first = static_cast<std::uint32_t>(state.transitions.size());
      count = static_cast<std::uint32_t>(list.size());
      for (const Transition& transition : list) {
        state.transitions.push_back(LoadedTransition{resolve(transition.from).value_or(kAnyRig),
                                                     resolve(transition.to).value_or(kAnyRig),
                                                     transition.blend});
      }
    };
    keep(description.enter, rig.first_enter, rig.enter_count);
    keep(description.exit, rig.first_exit, rig.exit_count);
    state.rigs.push_back(std::move(rig));
  }
  state.input = &input;
  return CameraFailure{};
}

RigId Evaluator::find_rig(std::string_view name) const noexcept {
  const std::optional<std::uint32_t> index = state_->rig_named(name);
  return index.has_value() ? RigId{*index} : RigId{};
}

std::string_view Evaluator::rig_name(RigId rig) const noexcept {
  return state_->rigs[rig.value].name;
}

void Evaluator::set_director(Director* director) noexcept { state_->director = director; }

void Evaluator::set_control_rotation_sink(ControlRotationSink* sink) noexcept {
  state_->sink = sink;
}

Context& Evaluator::context() noexcept { return state_->context; }

// Contract 3. A request naming a rig that is not loaded is not applied and the
// evaluation reports UnknownRig.
const Evaluation& Evaluator::evaluate(double step) {
  State& state = *state_;
  CameraError error = CameraError::None;
  if (state.director != nullptr) {
    const std::size_t written =
        std::min(state.director->run(step, std::span(state.requests)), kMaxRequests);
    for (std::size_t index = 0; index < written; ++index) {
      const RigRequest& request = state.requests[index];
      if (request.rig.value >= state.rigs.size()) {
        error = CameraError::UnknownRig;
        continue;
      }
      switch (request.layer) {
      case Layer::Main:
        if (const CameraError pushed = state.push(request); pushed != CameraError::None) {
          error = pushed;
        }
        break;
      case Layer::Base:
        state.add_to_layer(state.base, request.rig.value);
        break;
      case Layer::Global:
        state.add_to_layer(state.global, request.rig.value);
        break;
      case Layer::Visual:
        state.add_to_layer(state.visual, request.rig.value);
        break;
      }
    }
  }

  Pose pose{};
  pose.location = state.context.owner_location;
  pose.yaw_degrees = facing_yaw_degrees(state.context.owner_rotation);
  pose = state.run_layer(state.base, pose, step);

  bool cut = false;
  const std::size_t count = state.entry_count;
  if (count > 0) {
    // Every entry's blend advances, the one pushed this evaluation included.
    for (std::size_t index = 0; index < count; ++index) {
      MainEntry& entry = state.entries[index];
      entry.elapsed += step;
      const float fraction = entry.blend.time > 0.0F
                                 ? static_cast<float>(std::min(
                                       1.0, entry.elapsed / static_cast<double>(entry.blend.time)))
                                 : 1.0F;
      entry.weight = index == 0 ? 1.0F : blend_factor(entry.blend, fraction);
    }
    // The slot speeds mixed bottom up by this evaluation's weights.
    Speed speed{};
    bool have_speed = false;
    for (std::size_t index = 0; index < count; ++index) {
      const MainEntry& entry = state.entries[index];
      const LoadedRig& rig = state.rigs[entry.rig];
      if (rig.boom == kNoBoom) {
        continue;
      }
      const InputSlot& slot = state.nodes[rig.boom].node.slot;
      if (!have_speed) {
        speed = Speed{slot.speed_yaw, slot.speed_pitch};
        have_speed = true;
      } else {
        speed = Speed{speed.yaw + entry.weight * (slot.speed_yaw - speed.yaw),
                      speed.pitch + entry.weight * (slot.speed_pitch - speed.pitch)};
      }
    }
    // Every slot turns with the mixed speed; every rig runs alone on the
    // base layer's pose; the results blend bottom up.
    for (std::size_t index = 0; index < count; ++index) {
      MainEntry& entry = state.entries[index];
      const LoadedRig& rig = state.rigs[entry.rig];
      if (rig.boom != kNoBoom) {
        entry.slot = state.turn(state.nodes[rig.boom], entry.slot, speed, step);
      }
      entry.pose = state.run_rig(entry.rig, pose, entry.slot);
    }
    Pose blended = state.entries[0].pose;
    for (std::size_t index = 0; index < count; ++index) {
      const MainEntry& entry = state.entries[index];
      if (index > 0) {
        blended = blend(blended, entry.pose, entry.weight);
      }
      if (entry.first && entry.blend.kind == BlendKind::Pop && entry.weight > 0.0F) {
        cut = true;
      }
    }
    pose = blended;
    // The entries below the highest at weight 1 are removed.
    std::size_t keep_from = 0;
    for (std::size_t index = 0; index < count; ++index) {
      if (state.entries[index].weight >= 1.0F) {
        keep_from = index;
      }
    }
    state.entry_count = count - keep_from;
    for (std::size_t index = 0; index < state.entry_count; ++index) {
      state.entries[index] = state.entries[index + keep_from];
      state.entries[index].first = false;
      state.stack[index] = StackEntry{RigId{state.entries[index].rig}, state.entries[index].weight};
    }
  }

  pose = state.run_layer(state.global, pose, step);
  const Pose pre_visual = pose;
  pose = state.run_layer(state.visual, pose, step);

  state.evaluation = Evaluation{pose, pre_visual, cut, state.entry_count > 0, error};
  if (state.sink != nullptr && state.evaluation.valid) {
    state.sink->set_control_rotation(pose.yaw_degrees, pose.pitch_degrees);
  }
  return state.evaluation;
}

const Evaluation& Evaluator::evaluation() const noexcept { return state_->evaluation; }

std::span<const StackEntry> Evaluator::stack() const noexcept {
  return std::span(state_->stack).first(state_->entry_count);
}

} // namespace bw::camera
