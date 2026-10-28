// Execute prepared native action chains under BehaviorRuntime's single clock.
// Resolve names/parameters once, allocate four bounded slot stores at actor
// admission, preserve suspended phases, then dispatch primary and companions in
// source order. Only typed commands cross to the actor; all observations are
// const borrows. Primary outcome bytes alone control behavior selection.
// Core SL.con.3 (#rsl-bounds) checks slots and parameter envelopes; performance
// MEM.9 (guidelines/memory/MEM.9-embedded-allocate-at-init.md) confines domain
// allocation to preparation. Both implementation corpora read. Source mapping
// and remaining navigation boundaries: first-combat-physics-boundary.md.
// Core E.4 (#re-design-invariants) separates executable actions from
// structural/control nodes before publishing the prepared library; MEM.9
// retains bounded initialization-only storage (both corpora reread).
// Core I.4 (#ri-typed) distinguishes authored posture indices from motion
// families by naming the prepared field; performance CDSA.32 transforms that
// immutable source table value once, before action execution (source file
// guidelines/cpu-dsa/CDSA.32-transform-static-data-into-the-consumer-layout.md).
// Registered action zero prepares a no-op even when its display label differs
// from noname; runtime terminal selection decides when to instantiate it.
// E.4 and MEM.9 reread: label controls remain excluded and storage stays
// bounded.
#include "native_actions.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <numbers>
#include <stdexcept>

#include "bw/content/formats/binary_reader.hpp"
#include "bw/math/euler.hpp"
#include "bw/math/quadratic.hpp"

namespace bw::game {
namespace {
using Vec3 = math::Vector3<float>;
using Reader = resources::detail::Reader;
constexpr std::size_t kWord = sizeof(std::uint32_t);
constexpr float kReactionBlend = 0.3F, kHalfReadyWeight = 0.5F;
constexpr float kGroundProbeDown = 20, kSideFootOffset = 0.5F;
constexpr float kCoverRefresh = 2, kCoverSearchRetreat = 10;
constexpr std::int32_t kCoverHeadingSamples = 15, kCoverSide = 2;
constexpr std::int32_t kFollowModeDigit = 10, kPatrolForwardEnd = 20,
                       kPatrolReverseLoop = -10, kPatrolReverseEnd = -20;
constexpr std::uint8_t kReactionUnavailable = 5, kSequenceFinished = 6,
                       kSequenceUnavailable = 7;
constexpr std::uint8_t kTimeout = 255, kSuccess = 1, kUnavailable = 2;
enum class Kind : std::uint8_t {
  none,
  wait,
  random_wait,
  change,
  increment,
  reaction,
  sequence_reaction,
  ready,
  shoot,
  random_shoot,
  force_shoot,
  random_force_shoot,
  voice,
  target_request,
  broadcast,
  movement,
  dodge,
  grapple
};
struct Wait {
  float timeout{}, range{};
  std::int32_t motion_posture{};
};
struct Change {
  std::string_view name;
};
struct Increment {
  std::int32_t mode{}, first{}, last{};
};
struct Reaction {
  float timeout{}, delay{};
  std::uint32_t flags{};
  std::string_view start{}, loop{}, finish{};
  std::int32_t target{}, callback{-1}, callback_mode{};
};
struct Weapon {
  float timeout{}, period{}, period_range{}, duration{}, duration_range{};
  std::int32_t target{}, mode{}, aim_mode{};
};
struct Voice {
  float delay{};
  NpcVoiceCommand command;
};
struct TargetRequest {
  float delay{}, duration_range{};
  NpcTargetCommand command;
};
struct Broadcast {
  float delay{};
  NpcBroadcastCommand command;
};
// Navigation parameters are prepared here; the retained controller consumes
// them separately from the behavior cadence. No physics state lives in a node.
enum class MovementKind : std::uint8_t {
  follow,
  angular,
  flock,
  patrol,
  side,
  hideaway,
  hidecome,
  unadmitted
};
struct ContactAction {
  std::int32_t target{}, mode{};
};
struct Movement {
  MovementKind kind{MovementKind::unadmitted};
  std::string_view name;
  float timeout{}, speed{}, radius{}, radius_range{}, angle_range{},
      angle_center{}, near_distance{}, interval{}, interval_range{}, jitter{};
  std::int32_t posture{}, target{}, mode{};
  std::uint32_t flags{};
};
using Parameters =
    std::variant<std::monostate, Wait, Change, Increment, Reaction, Weapon,
                 Voice, TargetRequest, Broadcast, Movement, ContactAction>;
struct Action {
  Kind kind{};
  Parameters parameters;
};
struct Chain {
  std::size_t begin{}, count{};
};
struct PreparedTree {
  const formats::BehaviorTreeDefinition* definition{};
  std::size_t begin{}, count{};
};
class ParametersReader {
 public:
  explicit ParametersReader(std::span<const std::uint8_t> bytes)
      : reader_{bytes} {}
  float real(std::size_t index) const {
    const float value = std::bit_cast<float>(reader_.u32(index * kWord));
    if (!std::isfinite(value)) {
      throw std::invalid_argument("Nonfinite native action parameter");
    }
    return value;
  }
  std::int32_t integer(std::size_t index) const {
    return std::bit_cast<std::int32_t>(reader_.u32(index * kWord));
  }
  std::uint32_t bits(std::size_t index) const {
    return reader_.u32(index * kWord);
  }

 private:
  Reader reader_;
};
Action prepare(const formats::BehaviorTreeDefinition& tree,
               std::string_view name, std::span<const std::uint8_t> bytes) {
  const ParametersReader p{bytes};
  if (name.empty() || name == "noname") return {};
  if (name == "wait") {
    return {Kind::wait,
            Wait{p.real(0), 0,
                 npc_movement_settings(p.integer(1)).original_posture}};
  }
  if (name == "waitrandom") {
    return {Kind::random_wait,
            Wait{p.real(0), p.real(1),
                 npc_movement_settings(p.integer(2)).original_posture}};
  }
  if (name == "changeai") {
    return {Kind::change, Change{tree.string(p.integer(0))}};
  }
  if (name == "increment") {
    const Increment value{p.integer(0), p.integer(1), p.integer(2)};
    if (value.first > value.last ||
        value.last == std::numeric_limits<std::int32_t>::max()) {
      throw std::invalid_argument("Invalid native action counter range");
    }
    return {Kind::increment, value};
  }
  if (name == "reaction" || name == "reactionSL") {
    const bool sequence = name == "reactionSL";
    const std::size_t target = sequence ? 5 : 3;
    Reaction value{.timeout = p.real(0),
                   .delay = p.real(target + 1),
                   .flags = p.bits(1),
                   .start = tree.string(p.integer(2)),
                   .target = p.integer(target),
                   .callback = p.integer(target + 2),
                   .callback_mode = p.integer(target + 3)};
    if (sequence) {
      value.loop = tree.string(p.integer(3));
      value.finish = tree.string(p.integer(4));
    }
    return {sequence ? Kind::sequence_reaction : Kind::reaction, value};
  }
  if (name == "readygun") {
    return {Kind::ready, Weapon{.timeout = p.real(0), .target = p.integer(1)}};
  }
  if (name == "lookshoot" || name == "forceshoot" ||
      name == "lookshootrandom" || name == "forceshootrandom") {
    const bool random = name.ends_with("random");
    const bool forced = name.starts_with("force");
    Weapon value{.timeout = p.real(0),
                 .target = p.integer(random ? 2 : 1),
                 .mode = random ? p.integer(1) : 0,
                 .aim_mode = p.integer(random ? 3 : 2)};
    if (random) {
      constexpr std::size_t kPeriodRangeField = 5, kDurationField = 6,
                            kDurationRangeField = 7;
      value.period = p.real(4);
      value.period_range = p.real(kPeriodRangeField);
      value.duration = p.real(kDurationField);
      value.duration_range = p.real(kDurationRangeField);
      if (value.period <= 0 || value.period_range < 0) {
        throw std::invalid_argument("Invalid native firing period");
      }
    }
    return {forced ? (random ? Kind::random_force_shoot : Kind::force_shoot)
                   : (random ? Kind::random_shoot : Kind::shoot),
            value};
  }
  if (name == "voice") {
    return {Kind::voice,
            Voice{p.real(0), {p.integer(1), p.integer(2), p.integer(3)}}};
  }
  if (name == "request_target") {
    constexpr std::size_t kDelayField = 5, kDurationField = 6,
                          kDurationRangeField = 7;
    return {Kind::target_request,
            TargetRequest{p.real(kDelayField),
                          p.real(kDurationRangeField),
                          {p.integer(0), p.integer(1), p.real(2), p.real(3),
                           p.real(4), p.real(kDurationField)}}};
  }
  if (name == "broadcastai") {
    return {Kind::broadcast,
            Broadcast{p.real(3), {p.integer(1), p.integer(4), p.real(2)}}};
  }
  if (name == "dodge" || name == "grapple") {
    return {name == "dodge" ? Kind::dodge : Kind::grapple,
            ContactAction{p.integer(0), p.integer(1)}};
  }
  constexpr std::array movement_names{"flocktocharacterF",
                                      "flocktocharacterB",
                                      "flocktocharacterTraceF",
                                      "flocktocharacterTraceB",
                                      "flocktomessageB",
                                      "flocktomessageTraceF",
                                      "flocktomessageTraceB",
                                      "MovetocharacterAngle",
                                      "MovetocharacterAngleNear",
                                      "MovetocharacterAngleMiddle",
                                      "Followtocharacter",
                                      "positioningmoveNear",
                                      "positioningmoveMiddle",
                                      "hideaway",
                                      "hidecome",
                                      "movetoside",
                                      "patrol",
                                      "grapple",
                                      "dodge"};
  if (std::ranges::find(movement_names, name) != movement_names.end()) {
    Movement movement{.name = name, .timeout = p.real(0)};
    if (name == "Followtocharacter") {
      movement.kind = MovementKind::follow;
      constexpr std::size_t kFarDistanceField = 5, kNearDistanceField = 6;
      movement.mode = p.integer(1);
      movement.posture = p.integer(2);
      movement.speed = p.real(3);
      movement.target = p.integer(4);
      movement.radius = p.real(kFarDistanceField);
      movement.near_distance = p.real(kNearDistanceField);
      if (movement.mode % kFollowModeDigit < 0 ||
          movement.mode % kFollowModeDigit > 2) {
        throw std::invalid_argument("Invalid native follow mode");
      }
    } else if (name == "patrol") {
      movement.kind = MovementKind::patrol;
      movement.mode = p.integer(1);
      movement.target = p.integer(2);
      movement.posture = p.integer(3);
      movement.speed = p.real(4);
    } else if (name == "movetoside") {
      movement.kind = MovementKind::side;
      constexpr std::size_t kIntervalField = 5, kIntervalRangeField = 6,
                            kJitterField = 7;
      movement.posture = p.integer(1);
      movement.speed = p.real(2);
      movement.target = p.integer(3);
      movement.radius = p.real(4);
      movement.interval = p.real(kIntervalField);
      movement.interval_range = p.real(kIntervalRangeField);
      movement.jitter = p.real(kJitterField);
    } else if (name == "hideaway" || name == "hidecome") {
      movement.kind =
          name == "hideaway" ? MovementKind::hideaway : MovementKind::hidecome;
      movement.posture = p.integer(1);
      movement.speed = p.real(2);
      movement.target = p.integer(3);
      movement.radius = p.real(4);
      if (movement.kind == MovementKind::hidecome) {
        constexpr std::size_t kExposureRangeField = 5;
        movement.interval_range = p.real(kExposureRangeField);
      }
    } else if (name.starts_with("flock") ||
               name.starts_with("MovetocharacterAngle") ||
               name.starts_with("positioningmove")) {
      movement.kind = name.starts_with("MovetocharacterAngle")
                          ? MovementKind::angular
                          : MovementKind::flock;
      constexpr std::size_t kRadiusRangeField = 5, kAngleRangeField = 6,
                            kAngleCenterField = 7, kFlagsField = 8;
      movement.posture = p.integer(1);
      movement.speed = p.real(2);
      const bool message = name.starts_with("flocktomessage");
      const std::size_t shift = message ? 1 : 0;
      if (!message) movement.target = p.integer(3);
      movement.radius = p.real(4 - shift);
      movement.radius_range = p.real(kRadiusRangeField - shift);
      movement.angle_range = p.real(kAngleRangeField - shift);
      movement.angle_center = p.real(kAngleCenterField - shift);
      movement.flags = p.bits(kFlagsField - shift);
    }
    return {Kind::movement, movement};
  }
  throw std::invalid_argument("Unprepared native action: " + std::string(name));
}
// Original aim smoothing is shared by an actor's action context, while each
// firing action retains its own burst cycle and previous target observation.
struct AimInterpolation {
  Vec3 base{}, delta{};
  float elapsed{}, duration{};
  Vec3 value() const {
    return math::add(base,
                     math::scale(delta, duration > 0 ? elapsed / duration : 1));
  }
};
Vec3 displaced_aim(const Vec3& target, const Vec3& origin, float cone,
                   SourceRandom& random) {
  const auto delta = math::subtract(target, origin);
  constexpr float kMinimumDirectionSquared = 1.0000001111620804e-6F;
  const float squared = math::dot(delta, delta);
  if (squared <= 0 || squared < kMinimumDirectionSquared) return {};
  const float length = math::length(delta);
  const auto direction = math::divide(delta, length);
  const Vec3 up =
      direction[0] == 0 && direction[2] == 0 ? Vec3{1, 0, 0} : Vec3{0, 1, 0};
  const auto basis = math::orthonormal_basis(direction, up);
  const float polar = random.unit() * cone;
  const float azimuth = random.unit() * std::numbers::pi_v<float>;
  const auto rotation = math::euler_xyz_transform(math::EulerPlacement<float>{
                                                      {0, polar, azimuth}, {}})
                            .forward;
  const auto result = math::multiply(rotation, basis);
  return math::add(
      origin,
      math::scale(Vec3{result[2][0], result[2][1], result[2][2]}, length));
}
struct PredictionFrame {
  float seconds{}, speed{};
};
Vec3 predicted_target(const Vec3& target, const Vec3& previous,
                      const Vec3& origin, PredictionFrame frame) {
  const float seconds = frame.seconds, speed = frame.speed;
  constexpr float kMinimumVelocitySquared = 1.0000001111620804e-6F;
  const auto delta = math::subtract(target, previous);
  const auto velocity =
      seconds > 0 && math::dot(delta, delta) >= kMinimumVelocitySquared
          ? math::divide(delta, seconds)
          : Vec3{};
  const auto separation = math::subtract(target, origin);
  const float a = math::dot(velocity, velocity) - speed * speed;
  const float b = math::dot(velocity, separation) * 2;
  const float c = math::dot(separation, separation);
  // The source linear branch uses c/b, unlike the quadratic equation's -c/b.
  // Preserve that compatibility choice explicitly at the game boundary.
  const auto roots = math::quadratic_roots(a, b, a == 0 ? -c : c);
  if (roots) {
    for (const float time : *roots) {
      if (time > 0) return math::add(target, math::scale(velocity, time));
    }
  }
  return target;
}
struct ActionState {
  std::uint8_t outcome{}, phase{};
  float timeout{}, period{1}, fraction{0.5F}, burst_elapsed{};
  std::int32_t counter{};
  Vec3 previous_target{}, retained_target{}, movement_goal{}, movement_origin{};
  float movement_radius{}, movement_remaining{};
  std::array<Vec3, 4> movement_candidates{};
  std::int32_t patrol_index{}, patrol_direction{1};
  std::optional<NpcCoverSearch> cover;
  std::int32_t cover_side{1}, cover_steps{};
  Vec3 cover_direction{}, previous_heading{};
  bool callback_armed{};
};
struct Slot {
  explicit Slot(const std::shared_ptr<memory::Domain>& domain)
      : states(memory::Allocator<ActionState>{domain}) {}
  Chain chain;
  memory::Vector<ActionState> states;
  bool entered{}, suspended{};
};
bool timed_out(ActionState& state, float timeout, float elapsed) {
  if (timeout > 0 && timeout < elapsed) {
    state.outcome = kTimeout;
    return true;
  }
  return false;
}
}  // namespace
struct NativeActionLibrary::Impl {
  explicit Impl(const std::shared_ptr<memory::Domain>& domain)
      : actions(memory::Allocator<Action>{domain}),
        chains(memory::Allocator<Chain>{domain}),
        trees(memory::Allocator<PreparedTree>{domain}) {}
  memory::Vector<Action> actions;
  memory::Vector<Chain> chains;
  memory::Vector<PreparedTree> trees;
  std::size_t maximum_chain{};
  Chain lookup(const BehaviorActionRequest& request) const {
    for (const auto& tree : trees) {
      if (tree.definition == request.definition) {
        if (request.node >= tree.count) {
          throw std::out_of_range("Native action node");
        }
        const auto chain = chains.at(tree.begin + request.node);
        if (chain.count == 0) {
          throw std::invalid_argument(
              "Behavior control is not a native action");
        }
        return chain;
      }
    }
    throw std::invalid_argument("Native action definition was not admitted");
  }
};
NativeActionLibrary::NativeActionLibrary(
    std::span<const formats::BehaviorTreeDefinition* const> definitions,
    const std::shared_ptr<memory::Domain>& domain) {
  auto data =
      std::allocate_shared<Impl>(memory::Allocator<Impl>{domain}, domain);
  for (const auto* definition : definitions) {
    if (!definition) {
      throw std::invalid_argument("Null native action definition");
    }
    data->trees.push_back(
        {definition, data->chains.size(), definition->nodes().size()});
    for (std::size_t node = 0; node < definition->nodes().size(); ++node) {
      const auto& primary = definition->nodes()[node];
      // Label transfers belong to BehaviorRuntime. Action zero is also used
      // by selectors, but its source factory is valid after terminal descent.
      if (primary.action < 0) {
        data->chains.push_back({data->actions.size(), 0});
        continue;
      }
      const auto attached = definition->attached_actions(node);
      const Chain chain{data->actions.size(), 1 + attached.size()};
      const auto admit = [&](std::string_view name,
                             std::span<const std::uint8_t> parameters,
                             std::size_t companion) {
        try {
          data->actions.push_back(prepare(*definition, name, parameters));
        } catch (const std::exception& error) {
          throw std::invalid_argument(
              "Native action admission tree=" +
              std::to_string(data->trees.size() - 1) + " node=" +
              std::to_string(node) + " companion=" + std::to_string(companion) +
              " action=" + std::string(name) + " bytes=" +
              std::to_string(parameters.size()) + ": " + error.what());
        }
      };
      if (primary.action == 0) {
        data->actions.push_back(Action{});
      } else {
        admit(primary.action_name, primary.parameters, 0);
      }
      for (std::size_t i = 0; i < attached.size(); ++i) {
        const auto& action = attached[i];
        admit(action.action_name, action.parameters, i + 1);
      }
      data->chains.push_back(chain);
      data->maximum_chain = std::max(data->maximum_chain, chain.count);
    }
  }
  impl_ = std::move(data);
}
NativeActionLibrary::~NativeActionLibrary() = default;
struct NativeActions::Impl {
  Impl(const Impl&) = delete;
  Impl& operator=(const Impl&) = delete;
  Impl(Impl&&) = delete;
  Impl& operator=(Impl&&) = delete;
  ~Impl() = default;
  Impl(std::shared_ptr<const NativeActionLibrary::Impl> prepared,
       const NpcActionQueries& observation, NpcActionReceiver& receiver,
       SourceRandom& random, const std::shared_ptr<memory::Domain>& domain)
      : library(std::move(prepared)),
        queries(observation),
        output(receiver),
        rng(random),
        slots{Slot{domain}, Slot{domain}, Slot{domain}, Slot{domain}} {
    for (auto& slot : slots) slot.states.resize(library->maximum_chain);
  }
  std::shared_ptr<const NativeActionLibrary::Impl> library;
  const NpcActionQueries& queries;
  NpcActionReceiver& output;
  SourceRandom& rng;
  std::array<Slot, 4> slots;
  AimInterpolation aim;
  void reset_aim(const Weapon& weapon, const NpcActionTarget& target) {
    const auto settings = queries.aim_settings(weapon.aim_mode, true);
    aim.base = displaced_aim(target.aim_position, queries.pose().position,
                             settings.cone_radians, rng);
    aim.delta = math::subtract(target.aim_position, aim.base);
    aim.elapsed = 0;
    aim.duration = 0;
  }
  void advance_aim(const Weapon& weapon, const NpcActionTarget& target,
                   const Vec3& predicted, float seconds) {
    const auto settings = queries.aim_settings(weapon.aim_mode, target.player);
    if (aim.elapsed < aim.duration) {
      aim.elapsed = std::min(aim.duration, aim.elapsed + seconds);
    }
    if (aim.duration <= aim.elapsed) {
      const auto next = displaced_aim(predicted, queries.pose().position,
                                      settings.cone_radians, rng);
      const auto current = aim.value();
      constexpr float kTransitionJitter = 0.1F;
      aim.duration =
          rng.unit() * kTransitionJitter + settings.transition_seconds;
      aim.elapsed = 0;
      aim.base = current;
      aim.delta = math::subtract(next, current);
    }
  }
  void burst(const Action& action, const Weapon& weapon, ActionState& state) {
    const bool random = action.kind == Kind::random_shoot ||
                        action.kind == Kind::random_force_shoot;
    if (random) {
      state.period = weapon.period + rng.unit() * weapon.period_range;
      state.fraction =
          (weapon.duration + rng.unit() * weapon.duration_range) / state.period;
    } else {
      constexpr float kPeriodRange = 5, kLookPeriod = 0.1F, kForcePeriod = 5.5F;
      constexpr float kLookFractionRange = 0.2F, kForceFractionRange = 0.25F;
      constexpr float kMinimumFraction = 0.1F;
      const bool forced = action.kind == Kind::force_shoot;
      state.period =
          rng.unit() * kPeriodRange + (forced ? kForcePeriod : kLookPeriod);
      state.fraction =
          rng.unit() * (forced ? kForceFractionRange : kLookFractionRange) +
          kMinimumFraction;
    }
  }
  void shooting(const Action& action, ActionState& state,
                BehaviorActionFrame frame) {
    const auto& weapon = std::get<Weapon>(action.parameters);
    if (timed_out(state, weapon.timeout, frame.elapsed_seconds)) return;
    const auto target = queries.target(weapon.target);
    if (!target) {
      state.outcome = kSuccess;
      return;
    }
    const bool forced = action.kind == Kind::force_shoot ||
                        action.kind == Kind::random_force_shoot;
    const auto pose = queries.pose();
    const bool visible = queries.can_shoot(target->aim_position);
    auto observed = target->aim_position;
    if (forced) {
      if (visible) {
        state.retained_target = observed;
      } else {
        state.burst_elapsed = 0;
        if (action.kind == Kind::random_force_shoot) {
          burst(action, weapon, state);
        }
      }
      observed = state.retained_target;
    }
    const auto settings = queries.aim_settings(weapon.aim_mode, target->player);
    auto predicted = observed;
    if (settings.predict_target && (!forced || visible) &&
        pose.projectile_speed > 0) {
      predicted = predicted_target(
          observed, state.previous_target, pose.position,
          PredictionFrame{frame.delta_seconds, pose.projectile_speed});
      state.previous_target = target->aim_position;
    }
    advance_aim(weapon, *target, predicted, frame.delta_seconds);
    if (forced && pose.target_knowledge <= 1) return;
    const auto aim_point = aim.value();
    const bool random = action.kind == Kind::random_shoot ||
                        action.kind == Kind::random_force_shoot;
    const bool tracking = random && weapon.mode == 1;
    auto direction =
        math::subtract(tracking ? aim_point : predicted, pose.weapon_origin);
    if ((random && !tracking) || action.kind == Kind::force_shoot) {
      direction = math::normalize_or(direction, Vec3{});
    }
    const bool firing = state.burst_elapsed / state.period <= state.fraction &&
                        (forced || queries.can_shoot(predicted));
    // The aim message always precedes ready/fire messages, and the world weapon
    // owner alone applies cooldown/ammo and creates a projectile.
    output.submit(NpcAimCommand{direction});
    if (tracking) output.submit(NpcReadyWeaponCommand{1});
    if (firing) output.submit(NpcFireCommand{aim_point});
    if (!tracking && (!firing || action.kind == Kind::random_shoot)) {
      output.submit(NpcReadyWeaponCommand{kHalfReadyWeight});
    }
    state.burst_elapsed += frame.delta_seconds;
    if (state.period < state.burst_elapsed) {
      state.burst_elapsed -= state.period;
      burst(action, weapon, state);
    }
  }
  void face(std::int32_t selector) {
    constexpr std::int32_t kNoFacing = -5;
    if (selector == kNoFacing) return;
    output.submit(NpcFaceCommand{queries.reaction_facing(selector)});
  }
  void start_reaction(
      const Reaction& reaction, ActionState& state, std::string_view name,
      NpcReactionRequestKind admission = NpcReactionRequestKind::ordinary) {
    if (name.empty()) {
      state.phase = 4;
      return;
    }
    if (name != "null") {
      if (!output.begin_reaction(
              {name, reaction.flags, kReactionBlend, false, admission})) {
        state.phase = kReactionUnavailable;
        return;
      }
    }
    if (reaction.callback >= 0) {
      output.submit(NpcReactionVoiceCommand{
          static_cast<std::uint32_t>(reaction.callback_mode)});
      state.callback_armed = true;
    }
    state.phase = 2;
  }
  Vec3 movement_destination(const Movement& movement, ActionState& state,
                            const NpcActionTarget& target) {
    const auto pose = queries.pose();
    auto reference = math::subtract(target.position, pose.position);
    reference = math::normalize_or(reference, pose.forward);
    const auto up =
        reference[0] == 0 && reference[2] == 0 ? Vec3{1, 0, 0} : Vec3{0, 1, 0};
    const auto basis = math::orthonormal_basis(reference, up);
    constexpr float kDegreesToRadians = std::numbers::pi_v<float> / 180.F;
    const float angle = (movement.angle_center - movement.angle_range +
                         rng.unit() * (movement.angle_range * 2)) *
                        kDegreesToRadians;
    const auto rotated =
        math::add(math::scale(Vec3{basis[0][0], basis[0][1], basis[0][2]},
                              std::sin(angle)),
                  math::scale(Vec3{basis[2][0], basis[2][1], basis[2][2]},
                              std::cos(angle)));
    const bool target_centered = (movement.flags & 1U) != 0;
    const auto goal = math::add(
        target_centered ? target.position : pose.position,
        math::scale(rotated, target_centered ? -state.movement_radius
                                             : state.movement_radius));
    constexpr float kGroundUp = 2, kGroundDown = 20;
    const auto from_target = math::subtract(goal, target.position);
    const float projection_up =
        std::min(std::sqrt(math::dot(from_target, from_target)), kGroundUp) +
        kGroundUp;
    return queries.ground_destination(goal, projection_up, kGroundDown);
  }
  void patrol_next(const Movement& movement, ActionState& state,
                   std::size_t count) {
    state.patrol_index += state.patrol_direction;
    state.outcome = kSuccess;
    const auto size = static_cast<std::int32_t>(count);
    if (count == 0) return;
    if (state.patrol_index >= 0 && state.patrol_index < size) return;
    state.outcome = kUnavailable;
    state.patrol_index = std::clamp(state.patrol_index, 0, size - 1);
    constexpr std::int32_t kModeBase = 100;
    switch (movement.mode % kModeBase) {
      case 0:
        state.patrol_index -= state.patrol_direction;
        state.patrol_direction = -state.patrol_direction;
        state.patrol_index =
            std::clamp(state.patrol_index, 1, std::max(1, size - 2));
        break;
      case kPatrolForwardEnd:
      case kPatrolReverseLoop:
        state.patrol_index = size - 1;
        break;
      default:
        state.patrol_index = 0;
        break;
    }
  }
  void enter_patrol(const Movement& movement, ActionState& state) {
    const auto points = queries.patrol_points(movement.target);
    state.patrol_index = 0;
    state.patrol_direction = 1;
    if (points.empty()) return;
    float nearest = std::numeric_limits<float>::max();
    const auto position = queries.pose().position;
    for (std::size_t i = 0; i < points.size(); ++i) {
      const auto relative = math::subtract(position, points[i]);
      const float distance = math::dot(relative, relative);
      if (distance < nearest) {
        nearest = distance;
        state.patrol_index = static_cast<std::int32_t>(i);
      }
    }
    constexpr std::int32_t kModeBase = 100;
    const auto mode = movement.mode % kModeBase;
    if (mode == kPatrolReverseEnd || mode == kPatrolReverseLoop) {
      state.patrol_direction = -1;
    } else if (mode == 0) {
      state.patrol_direction =
          points.size() < static_cast<std::size_t>(state.patrol_index) * 2 ? 1
                                                                           : -1;
    }
    const auto next = state.patrol_index + state.patrol_direction;
    if (next >= 0 && static_cast<std::size_t>(next) < points.size()) {
      const auto start = points[static_cast<std::size_t>(state.patrol_index)];
      const auto segment =
          math::subtract(points[static_cast<std::size_t>(next)], start);
      const float denominator = math::dot(segment, segment);
      const float projection =
          denominator > 0
              ? math::dot(math::subtract(position, start), segment) /
                    denominator
              : 0;
      if (projection > 0 && projection < 1) state.patrol_index = next;
    }
    const auto point = points[static_cast<std::size_t>(state.patrol_index)];
    const auto delta =
        math::subtract(Vec3{point[0], position[1], point[2]}, position);
    if (math::dot(delta, delta) < 1) {
      patrol_next(movement, state, points.size());
    }
    state.outcome = 0;
  }
  void update_side(const Movement& movement, ActionState& state,
                   const NpcActionTarget& target, float seconds) {
    state.movement_remaining -= seconds;
    if (state.movement_remaining >= 0) return;
    state.movement_remaining =
        movement.interval + rng.unit() * movement.interval_range;
    const auto f = target.forward;
    const std::array<Vec3, 4> directions{Vec3{-f[2], f[1], f[0]}, f,
                                         Vec3{f[2], f[1], -f[0]},
                                         math::scale(f, -1.F)};
    constexpr float kFullCircle = 2 * std::numbers::pi_v<float>;
    for (std::size_t i = 0; i < directions.size(); ++i) {
      const float angle = rng.unit() * kFullCircle;
      const Vec3 jitter{std::sin(angle) * movement.jitter, 0,
                        std::cos(angle) * movement.jitter};
      state.movement_candidates.at(i) = math::add(
          target.position,
          math::add(math::scale(directions.at(i), movement.radius), jitter));
    }
    std::size_t nearest = 0;
    float distance = std::numeric_limits<float>::max();
    for (std::size_t i = 0; i < directions.size(); ++i) {
      const auto relative = math::subtract(queries.pose().position,
                                           state.movement_candidates.at(i));
      const float squared = math::dot(relative, relative);
      if (squared < distance) {
        distance = squared;
        nearest = i;
      }
    }
    std::size_t selected = (nearest + 3) % directions.size();
    float dot =
        math::dot(f, math::subtract(state.movement_candidates.at(selected),
                                    target.position));
    for (std::size_t offset = 1; offset < 3; ++offset) {
      const auto index = (nearest + 3 + offset) % directions.size();
      const float candidate = math::dot(
          f,
          math::subtract(state.movement_candidates.at(index), target.position));
      if (candidate < dot) {
        dot = candidate;
        selected = index;
      }
    }
    state.movement_goal = state.movement_candidates.at(selected);
  }
  std::int32_t cover_side(const Vec3& point, const Vec3& origin,
                          const Vec3& threat, std::int32_t previous) const {
    const auto forward = math::subtract(point, origin);
    const auto relative = math::subtract(threat, origin);
    constexpr float kTiny = 1.0000001111620804e-6F;
    if (math::dot(forward, forward) < kTiny ||
        math::dot(relative, relative) < kTiny) {
      return previous;
    }
    const auto normal = math::divide(forward, math::length(forward));
    const Vec3 up =
        normal[0] == 0 && normal[2] == 0 ? Vec3{1, 0, 0} : Vec3{0, 1, 0};
    const auto basis = math::orthonormal_basis(normal, up);
    const Vec3 right{basis[0][0], basis[0][1], basis[0][2]};
    return math::dot(right, relative) < 0 ? kCoverSide : -kCoverSide;
  }
  void initialize_cover_direction(const Movement& movement,
                                  ActionState& state) {
    const auto pose = queries.pose();
    bool found = false;
    for (const auto& point : queries.navigation_queries().cover_points()) {
      const auto delta = math::subtract(point.position, pose.position);
      if (math::dot(delta, delta) < pose.body_radius * pose.body_radius) {
        state.cover_direction = point.direction;
        found = true;
        break;
      }
    }
    if (!found) {
      const auto angle = rng.unit() * (2 * std::numbers::pi_v<float>);
      state.cover_direction = {std::sin(angle), 0, std::cos(angle)};
    }
    const auto target = movement_target(movement, false);
    const auto navigation = queries.navigation_state();
    state.cover_side =
        target ? -cover_side(math::add(pose.position, state.cover_direction),
                             pose.position, target->position, navigation.side)
               : navigation.side;
    state.cover_steps = kCoverHeadingSamples;
    state.previous_heading = navigation.heading;
    output.set_navigation_side(state.cover_side);
  }
  void request_cover(ActionState& state, const NpcActionTarget& target) {
    if (!state.cover) throw std::logic_error("Cover search was not admitted");
    const auto pose = queries.pose();
    state.cover->request({pose.position, target.position, pose.body_radius,
                          pose.body_height, 0});
    state.callback_armed = true;
  }
  void cover_movement(const Movement& movement, ActionState& state,
                      BehaviorActionFrame frame) {
    const auto target = movement_target(movement, false);
    if (!target) {
      state.outcome = 3;
      return;
    }
    const auto pose = queries.pose();
    const auto navigation = queries.navigation_state();
    if (!state.cover) throw std::logic_error("Cover action was not entered");
    state.cover->advance(frame.delta_seconds);
    const bool visible = queries.can_see(target->position);
    if (movement.kind == MovementKind::hidecome) {
      state.movement_goal = target->position;
      if (state.phase < 2) {
        if (state.phase == 0 && state.cover_steps > 0) {
          if (!navigation.active) {
            state.previous_heading = navigation.heading;
          } else if (--state.cover_steps == 0 &&
                     math::dot(math::subtract(navigation.heading,
                                              state.previous_heading),
                               state.cover_direction) < 0) {
            state.cover_side = -state.cover_side;
            output.set_navigation_side(state.cover_side);
            output.reset_navigation();
          }
        }
        if (visible) {
          state.phase = 2;
          state.movement_remaining =
              movement.radius + rng.unit() * movement.interval_range;
          state.movement_goal = pose.position;
        }
      } else if (state.phase == 2) {
        state.outcome = kUnavailable;
        state.movement_remaining -= frame.delta_seconds;
        state.movement_goal = pose.position;
        if (state.movement_remaining <= 0) {
          state.phase = 3;
          output.set_navigation_side(-state.cover_side);
          state.movement_goal = state.movement_origin;
        }
      } else {
        state.movement_goal = state.movement_origin;
        if (!navigation.active) {
          state.phase = 1;
          output.set_navigation_side(state.cover_side);
          state.movement_goal = target->position;
        }
      }
      if (queries.navigation_queries().traversable(state.movement_origin,
                                                   target->position)) {
        state.outcome = kSuccess;
      }
    } else {
      auto away = math::subtract(pose.position, target->position);
      const auto full_squared = math::dot(away, away);
      away[1] = 0;
      constexpr float kTiny = 1.0000001111620804e-6F;
      const auto squared = math::dot(away, away);
      if (squared < kTiny && state.phase != 0 && state.phase != 4) {
        state.phase = 4;
        state.cover->reset();
        state.callback_armed = false;
        state.movement_goal = target->position;
      }
      if (state.phase <= 2 && squared >= kTiny) {
        const auto distance =
            state.phase == 2 ? kCoverSearchRetreat : movement.radius + 1;
        state.movement_goal = math::add(
            pose.position, math::scale(away, distance / std::sqrt(squared)));
      }
      switch (state.phase) {
        case 0:
          state.movement_remaining -= frame.delta_seconds;
          if (state.movement_remaining <= 0 &&
              full_squared >= movement.radius * movement.radius) {
            state.phase = 1;
          }
          break;
        case 1:
          output.set_navigation_side(cover_side(state.movement_goal,
                                                pose.position, target->position,
                                                navigation.side));
          if (!state.callback_armed) {
            request_cover(state, *target);
          } else {
            state.phase = 2;
          }
          break;
        case 2: {
          state.outcome = 0;
          const auto result = state.cover->result();
          if (result.ready) {
            if (result.found) {
              state.phase = 3;
              state.movement_goal = result.position;
              state.movement_remaining = kCoverRefresh;
              output.set_navigation_side(
                  cover_side(result.position, pose.position, target->position,
                             navigation.side));
            } else {
              state.outcome = kUnavailable;
            }
            request_cover(state, *target);
          }
          break;
        }
        case 3: {
          const auto result = state.cover->result();
          if (result.ready && !result.found) {
            state.outcome = kUnavailable;
            state.phase = 1;
            request_cover(state, *target);
          } else if (result.ready) {
            state.movement_goal = result.position;
            state.movement_remaining -= frame.delta_seconds;
            if (state.movement_remaining < 0) {
              output.set_navigation_side(
                  cover_side(result.position, pose.position, target->position,
                             navigation.side));
              request_cover(state, *target);
              state.movement_remaining = kCoverRefresh;
            }
          }
          if (!navigation.active) {
            state.phase = 4;
            state.cover->reset();
            state.callback_armed = false;
          }
          break;
        }
        case 4:
          state.outcome = kSuccess;
          if (visible) {
            state.phase = 0;
            state.outcome = 0;
            state.cover->reset();
            state.callback_armed = false;
          }
          break;
        default:
          throw std::logic_error("Invalid retained cover phase");
      }
    }
    (void)output.navigate({.destination = state.movement_goal});
    (void)timed_out(state, movement.timeout, frame.elapsed_seconds);
  }
  std::optional<NpcActionTarget> movement_target(const Movement& movement,
                                                 bool alternate) const {
    if (movement.name.starts_with("flocktomessage")) {
      const auto position = queries.last_message_position();
      if (!position) return std::nullopt;
      return NpcActionTarget{.position = *position};
    }
    return queries.movement_target(movement.target, alternate);
  }
  void enter_movement(const Movement& movement, ActionState& state,
                      bool resumed) {
    if (movement.kind == MovementKind::unadmitted) {
      throw std::logic_error("Unadmitted native navigation action: " +
                             std::string(movement.name));
    }
    const auto settings = npc_movement_settings(movement.posture);
    output.submit(NpcPostureCommand{settings.original_posture,
                                    queries.movement_speed(movement.speed),
                                    false});
    if (movement.kind == MovementKind::hideaway ||
        movement.kind == MovementKind::hidecome) {
      if (resumed && movement.kind == MovementKind::hidecome) return;
      output.reset_navigation();
      state.phase = 0;
      state.movement_origin = queries.pose().position;
      state.movement_goal = state.movement_origin;
      state.movement_remaining = 0;
      state.cover.emplace(queries.navigation_queries());
      if (movement.kind == MovementKind::hidecome) {
        initialize_cover_direction(movement, state);
      }
      return;
    }
    if (movement.kind == MovementKind::patrol) {
      output.reset_navigation();
      if (!resumed) enter_patrol(movement, state);
      return;
    }
    if (movement.kind == MovementKind::side) {
      output.reset_navigation();
      state.movement_remaining = 0;
      return;
    }
    if (movement.kind == MovementKind::follow) {
      output.reset_navigation();
      return;
    }
    if (resumed && movement.kind == MovementKind::flock) return;
    output.reset_navigation();
    const auto target = movement_target(movement, false);
    if (!target) {
      state.outcome = kUnavailable;
      return;
    }
    const auto range = (movement.flags >> 2U) & 7U;
    state.movement_radius = movement.radius +
                            rng.unit() * movement.radius_range +
                            (range != 0 ? queries.movement_range(range) : 0);
    state.movement_origin = (movement.flags & 1U) != 0 &&
                                    !movement.name.starts_with("flocktomessage")
                                ? target->position
                                : queries.pose().position;
    state.movement_goal = movement_destination(movement, state, *target);
    if (movement.kind == MovementKind::angular) {
      state.movement_goal =
          math::subtract(state.movement_goal, target->position);
    }
  }
  void movement(const Movement& movement, ActionState& state,
                BehaviorActionFrame frame) {
    if (movement.kind == MovementKind::patrol) {
      const auto points = queries.patrol_points(movement.target);
      const auto destination =
          points.empty() ? queries.initial_position()
                         : points[static_cast<std::size_t>(std::clamp(
                               state.patrol_index, 0,
                               static_cast<std::int32_t>(points.size() - 1)))];
      constexpr std::int32_t kModeBase = 100, kModeDigit = 10;
      const bool complete = output.navigate(NpcNavigationRequest{
          .destination =
              queries.ground_destination(destination, 0, kGroundProbeDown),
          .trace_path = (movement.mode / kModeBase) % kModeDigit == 0,
          .receiver = (movement.mode / kModeBase) % kModeDigit == 0
                          ? NpcNavigationReceiver::routed
                          : NpcNavigationReceiver::direct});
      if (complete) {
        patrol_next(movement, state, points.size());
      } else {
        state.outcome = 0;
      }
      (void)timed_out(state, movement.timeout, frame.elapsed_seconds);
      return;
    }
    if (movement.kind == MovementKind::hideaway ||
        movement.kind == MovementKind::hidecome) {
      cover_movement(movement, state, frame);
      return;
    }
    const auto target = movement_target(
        movement, ((movement.mode / kFollowModeDigit) & 1) != 0);
    if (!target) {
      state.outcome = kUnavailable;
      return;
    }
    NpcNavigationRequest request{.destination = target->position};
    const auto pose = queries.pose();
    if (movement.kind == MovementKind::follow) {
      const float radius = npc_movement_settings(movement.posture).radius;
      const float radius_squared = radius * radius;
      const float far_squared = movement.radius * movement.radius;
      constexpr float kFollowMargin = 4;
      request.approach_start_squared =
          radius_squared * 4 + far_squared + kFollowMargin;
      request.approach_stop_squared = radius_squared + far_squared;
      request.retreat_start_squared =
          movement.near_distance * movement.near_distance;
      request.retreat_distance =
          radius * 2 + movement.near_distance + kFollowMargin;
      if (movement.mode % kFollowModeDigit == 1) {
        request.retreat_start_squared = -1;
        request.retreat_distance = -1;
      } else if (movement.mode % kFollowModeDigit == 2) {
        const auto relative = math::subtract(pose.position, target->position);
        request.approach_start_squared = math::dot(relative, relative) + 2;
        request.approach_stop_squared = request.approach_start_squared;
      }
    } else if (movement.kind == MovementKind::angular) {
      constexpr float kGroundUp = 2, kGroundDown = 20;
      request.destination = queries.ground_destination(
          math::add(target->position, state.movement_goal), kGroundUp,
          kGroundDown);
    } else if (movement.kind == MovementKind::side) {
      update_side(movement, state, *target, frame.delta_seconds);
      request.destination =
          queries.ground_destination(state.movement_goal, 0, kGroundProbeDown);
      request.destination[1] += kSideFootOffset;
    } else if (movement.kind == MovementKind::flock) {
      request.destination = state.movement_goal;
      if (movement.name.starts_with("flocktomessage")) {
        constexpr float kMessageGoalHeight = 0.5F;
        request.destination[1] = pose.position[1] + kMessageGoalHeight;
      }
      request.approach_start_squared = request.minimum_squared;
      request.approach_stop_squared = request.minimum_squared;
      request.trace_path = (movement.flags & 2U) != 0;
      request.receiver = request.trace_path ? NpcNavigationReceiver::routed
                                            : NpcNavigationReceiver::direct;
    } else {
      throw std::logic_error("Unadmitted native navigation action: " +
                             std::string(movement.name));
    }
    bool completed = output.navigate(request);
    if (movement.kind == MovementKind::flock && request.trace_path) {
      const auto relative =
          math::subtract(pose.position, state.movement_origin);
      const float distance_squared = math::dot(relative, relative);
      if ((movement.flags & 1U) == 0) {
        completed =
            state.movement_radius * state.movement_radius <= distance_squared;
      } else {
        completed =
            completed &&
            distance_squared < state.movement_radius * state.movement_radius &&
            queries.can_shoot(state.movement_origin);
      }
    }
    state.outcome = completed ? kSuccess : 0;
    (void)timed_out(state, movement.timeout, frame.elapsed_seconds);
  }
  void enter_action(const Action& action, ActionState& state, bool resumed) {
    switch (action.kind) {
      case Kind::wait:
      case Kind::random_wait: {
        const auto& p = std::get<Wait>(action.parameters);
        state.timeout = p.timeout;
        if (action.kind == Kind::random_wait) {
          state.timeout += rng.unit() * p.range;
        }
        output.submit(NpcPostureCommand{p.motion_posture, 1, true});
        break;
      }
      case Kind::increment: {
        const auto& p = std::get<Increment>(action.parameters);
        if (!resumed) state.counter = p.first;
        if (p.mode == 0) {
          ++state.counter;
          if (state.counter > p.last) state.counter = p.first;
        } else if (p.mode == 1 || p.mode == 2) {
          const auto range = static_cast<std::uint32_t>(
              static_cast<std::int64_t>(p.last) - p.first + 1);
          auto value = static_cast<std::int32_t>(
              static_cast<std::int64_t>(p.first) + rng.next(range));
          if (p.mode == 2 && value == state.counter) {
            value = value == p.last ? p.first : value + 1;
          }
          state.counter = value;
        }
        break;
      }
      case Kind::reaction:
      case Kind::sequence_reaction: {
        const auto& p = std::get<Reaction>(action.parameters);
        if (!resumed) state.phase = 1;
        face(p.target);
        if (action.kind == Kind::reaction && p.delay == 0) {
          start_reaction(p, state, p.start);
        }
        break;
      }
      case Kind::shoot:
      case Kind::random_shoot:
      case Kind::force_shoot:
      case Kind::random_force_shoot: {
        const auto& p = std::get<Weapon>(action.parameters);
        if (const auto target = queries.target(p.target)) {
          reset_aim(p, *target);
          state.previous_target = target->aim_position;
          state.retained_target = target->aim_position;
        }
        state.burst_elapsed = 0;
        if (action.kind != Kind::force_shoot) burst(action, p, state);
        output.submit(NpcReadyWeaponCommand{1});
        break;
      }
      case Kind::voice:
      case Kind::target_request:
      case Kind::broadcast:
        state.phase = 0;
        break;
      case Kind::dodge: {
        const auto& p = std::get<ContactAction>(action.parameters);
        constexpr std::int32_t message_position = -3;
        if (p.target == message_position) {
          if (const auto point = queries.last_message_position()) {
            output.submit(NpcDodgeCommand{*point});
          } else {
            throw std::logic_error(
                "Dodge selected before its source message position exists");
          }
        } else if (const auto target =
                       queries.movement_target(p.target, false)) {
          output.submit(NpcDodgeCommand{target->position});
        } else {
          state.outcome = kUnavailable;
        }
        break;
      }
      case Kind::grapple: {
        const auto& p = std::get<ContactAction>(action.parameters);
        constexpr std::array<std::uint32_t, 9> kAttackModes{7, 0, 1,  2, 3,
                                                            4, 5, 10, 11};
        const auto target = queries.target(p.target);
        if (!target) {
          state.outcome = kSuccess;
          break;
        }
        const std::uint32_t attack =
            p.mode >= 2 && p.mode <= 10
                ? kAttackModes.at(static_cast<std::size_t>(p.mode - 2))
                : 6;
        output.submit(NpcMeleeCommand{target->identity, attack});
        face(p.target);
        break;
      }
      case Kind::movement:
        enter_movement(std::get<Movement>(action.parameters), state, resumed);
        break;
      case Kind::none:
      case Kind::change:
      case Kind::ready:
        break;
      default:
        throw std::logic_error(
            "Native shooting/navigation action body is not complete");
    }
  }
  void exit_action(const Action& action) {
    if (action.kind == Kind::reaction ||
        action.kind == Kind::sequence_reaction) {
      const auto& p = std::get<Reaction>(action.parameters);
      constexpr std::uint32_t kKeepReaction = 2;
      const auto pose = queries.pose();
      if ((p.flags & kKeepReaction) == 0 && pose.reaction_active &&
          !pose.dead) {
        output.submit(NpcReactionCommand{{}, p.flags, 0, true});
      }
    }
  }
  void advance_action(const Action& action, ActionState& state,
                      BehaviorActionFrame frame) {
    switch (action.kind) {
      case Kind::dodge:
        if (!queries.dodge_running()) state.outcome = kSuccess;
        break;
      case Kind::grapple: {
        const auto& p = std::get<ContactAction>(action.parameters);
        if (queries.melee_idle()) {
          state.outcome = queries.target(p.target) ? kUnavailable : kSuccess;
        } else {
          face(p.target);
        }
        break;
      }
      case Kind::movement:
        movement(std::get<Movement>(action.parameters), state, frame);
        break;
      case Kind::none:
        break;
      case Kind::wait:
      case Kind::random_wait:
        (void)timed_out(state, state.timeout, frame.elapsed_seconds);
        break;
      case Kind::change: {
        const auto name = std::get<Change>(action.parameters).name;
        state.outcome = kSuccess;
        output.submit(NpcBehaviorCommand{
            name.starts_with("retn") ? queries.return_behavior() : name});
        break;
      }
      case Kind::increment:
        state.outcome = static_cast<std::uint8_t>(
            static_cast<std::uint32_t>(state.counter));
        break;
      case Kind::reaction: {
        const auto& p = std::get<Reaction>(action.parameters);
        const bool expired =
            p.timeout > 0 && p.timeout <= frame.elapsed_seconds;
        switch (state.phase) {
          case 1:
            face(p.target);
            if (p.delay <= frame.elapsed_seconds) {
              start_reaction(p, state, p.start);
            }
            if (expired && state.phase != kReactionUnavailable) state.phase = 2;
            break;
          case 2: {
            const auto pose = queries.pose();
            if (((!pose.motion_looping && pose.motion_completed) ||
                 !pose.reaction_active) ||
                expired ||
                (state.callback_armed && p.timeout == 0 &&
                 queries.reaction_voice_finished())) {
              state.phase = 4;
            }
            face(p.target);
            break;
          }
          case 4:
            state.outcome = expired ? kTimeout : kSuccess;
            break;
          case kReactionUnavailable:
            state.outcome = kUnavailable;
            break;
          default:
            break;
        }
        break;
      }
      case Kind::shoot:
      case Kind::random_shoot:
      case Kind::force_shoot:
      case Kind::random_force_shoot:
        shooting(action, state, frame);
        break;
      case Kind::sequence_reaction: {
        const auto& p = std::get<Reaction>(action.parameters);
        const bool expired =
            p.timeout > 0 && p.timeout <= frame.elapsed_seconds;
        switch (state.phase) {
          case 1:
            face(p.target);
            if (p.delay <= frame.elapsed_seconds) {
              start_reaction(p, state, p.start,
                             NpcReactionRequestKind::sequence);
              if (state.phase == kReactionUnavailable) {
                state.phase = kSequenceUnavailable;
                break;
              }
            }
            if (expired) {
              if ((p.flags & 2U) == 0 && queries.pose().reaction_active) {
                output.submit(NpcReactionCommand{{}, p.flags, 0, true});
              }
              state.phase = 3;
            }
            break;
          case 2:
            if (queries.pose().motion_completed) {
              if (p.loop.empty()) {
                state.phase = kSequenceFinished;
                break;
              }
              start_reaction(p, state, p.loop,
                             NpcReactionRequestKind::sequence);
              state.phase = state.phase == kReactionUnavailable
                                ? kSequenceUnavailable
                                : 3;
            }
            if (expired) state.phase = kSequenceFinished;
            break;
          case 3: {
            const auto pose = queries.pose();
            if ((!pose.motion_looping && pose.motion_completed) || expired ||
                (state.callback_armed && p.timeout == 0 &&
                 queries.reaction_voice_finished())) {
              state.phase = kSequenceFinished;
            }
            face(p.target);
            break;
          }
          case kSequenceFinished:
            state.outcome = expired ? kTimeout : kSuccess;
            break;
          case kSequenceUnavailable:
            state.outcome = kUnavailable;
            break;
          default:
            break;
        }
        break;
      }
      case Kind::ready: {
        const auto& p = std::get<Weapon>(action.parameters);
        if (timed_out(state, p.timeout, frame.elapsed_seconds)) break;
        const auto target = queries.target(p.target);
        if (!target) {
          state.outcome = kSuccess;
          break;
        }
        output.submit(NpcAimCommand{math::subtract(
            target->aim_position, queries.pose().weapon_origin)});
        output.submit(NpcReadyWeaponCommand{1});
        break;
      }
      case Kind::voice: {
        const auto& p = std::get<Voice>(action.parameters);
        if (state.phase == 0 && p.delay < frame.elapsed_seconds) {
          output.submit(p.command);
          state.phase = 1;
        } else if (state.phase == 1 && queries.voice_finished()) {
          state.outcome = kSuccess;
          state.phase = 2;
        }
        break;
      }
      case Kind::target_request: {
        const auto& p = std::get<TargetRequest>(action.parameters);
        if (state.phase == 0 && p.delay <= frame.elapsed_seconds) {
          auto command = p.command;
          command.duration += rng.unit() * p.duration_range;
          output.submit(command);
          state.outcome = kSuccess;
        } else if (state.phase == 1) {
          state.outcome = kSuccess;
        }
        break;
      }
      case Kind::broadcast: {
        const auto& p = std::get<Broadcast>(action.parameters);
        if (state.phase == 0 && p.delay <= frame.elapsed_seconds) {
          output.submit(p.command);
          state.phase = 1;
        } else if (state.phase == 1) {
          state.outcome = kSuccess;
        }
        break;
      }
      default:
        throw std::logic_error(
            "Native shooting/navigation action body is not complete");
    }
  }
};
NativeActions::NativeActions(const NativeActionLibrary& library,
                             const NpcActionQueries& queries,
                             NpcActionReceiver& output, SourceRandom& random,
                             const std::shared_ptr<memory::Domain>& domain)
    : impl_(std::allocate_shared<Impl>(memory::Allocator<Impl>{domain},
                                       library.impl_, queries, output, random,
                                       domain)) {}
NativeActions::~NativeActions() = default;
BehaviorActionState NativeActions::state(std::size_t index) const {
  const auto& slot = impl_->slots.at(index);
  if (!slot.entered) return {};
  const auto outcome = slot.states.front().outcome;
  const auto kind = impl_->library->actions.at(slot.chain.begin).kind;
  return {outcome != 0, true,
          kind == Kind::reaction || kind == Kind::sequence_reaction, outcome};
}
void NativeActions::enter(std::size_t index,
                          const BehaviorActionRequest& request) {
  auto& slot = impl_->slots.at(index);
  if (slot.entered) {
    throw std::logic_error("Native action slot already entered");
  }
  slot.chain = impl_->library->lookup(request);
  for (auto& state : slot.states) state = ActionState{};
  slot.entered = true;
  slot.suspended = false;
  for (std::size_t i = 0; i < slot.chain.count; ++i) {
    impl_->enter_action(impl_->library->actions.at(slot.chain.begin + i),
                        slot.states.at(i), false);
  }
}
void NativeActions::advance(std::size_t index, BehaviorActionFrame frame) {
  auto& slot = impl_->slots.at(index);
  if (!slot.entered || slot.suspended) {
    throw std::logic_error("Native action slot is not active");
  }
  if (!std::isfinite(frame.delta_seconds) ||
      !std::isfinite(frame.elapsed_seconds) || frame.delta_seconds < 0 ||
      frame.elapsed_seconds < 0) {
    throw std::invalid_argument("Invalid native action frame");
  }
  for (std::size_t i = 0; i < slot.chain.count; ++i) {
    impl_->advance_action(impl_->library->actions.at(slot.chain.begin + i),
                          slot.states.at(i), frame);
  }
}
void NativeActions::exit(std::size_t index) {
  auto& slot = impl_->slots.at(index);
  if (!slot.entered) return;
  for (std::size_t i = 0; i < slot.chain.count; ++i) {
    impl_->exit_action(impl_->library->actions.at(slot.chain.begin + i));
  }
  slot.entered = false;
  slot.suspended = false;
}
void NativeActions::suspend(std::size_t index) {
  auto& slot = impl_->slots.at(index);
  if (!slot.entered || slot.suspended) {
    throw std::logic_error("Native action slot cannot suspend");
  }
  for (std::size_t i = 0; i < slot.chain.count; ++i) {
    impl_->exit_action(impl_->library->actions.at(slot.chain.begin + i));
  }
  slot.suspended = true;
}
void NativeActions::resume(std::size_t index) {
  auto& slot = impl_->slots.at(index);
  if (!slot.entered || !slot.suspended) {
    throw std::logic_error("Native action slot cannot resume");
  }
  slot.suspended = false;
  for (std::size_t i = 0; i < slot.chain.count; ++i) {
    impl_->enter_action(impl_->library->actions.at(slot.chain.begin + i),
                        slot.states.at(i), true);
  }
}
}  // namespace bw::game
