// Read-only original Lua adapters. E.4 (#re-design-invariants) rejects invalid
// identity/type inputs before querying an owner; MEM.9 prepares the fixed name
// table once and performs no observation caching with competing authority.
// Both implementation corpora read. Source math is in behavior_script_data;
// other live observations are explicitly required from their owning systems.
#include "behavior_observations.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
namespace bw::game {
namespace {
double number(std::span<const script::Value> args, std::size_t index) {
  if (index >= args.size()) {
    throw std::invalid_argument("Missing AI observation argument");
  }
  const auto* value = std::get_if<double>(&args[index]);
  if (!value || !std::isfinite(*value)) {
    throw std::invalid_argument("Invalid AI observation number");
  }
  return *value;
}
std::int32_t integer(std::span<const script::Value> args, std::size_t index) {
  const auto value = number(args, index);
  if (value < std::numeric_limits<std::int32_t>::min() ||
      value > std::numeric_limits<std::int32_t>::max()) {
    throw std::invalid_argument("AI observation integer out of range");
  }
  return static_cast<std::int32_t>(value);
}
std::string_view string(std::span<const script::Value> args,
                        std::size_t index) {
  if (index >= args.size()) {
    throw std::invalid_argument("Missing AI observation string");
  }
  const auto* value = std::get_if<std::string>(&args[index]);
  if (!value) {
    throw std::invalid_argument("Invalid AI observation string");
  }
  return *value;
}
void owner(std::span<const script::Value> args, std::uint32_t identity) {
  if (number(args, 0) != static_cast<double>(identity)) {
    throw std::invalid_argument("AI observation belongs to another actor");
  }
}
script::Reply result(double value) { return {{value}, {}}; }
bool prefix(std::string_view value, std::string_view expected) {
  if (value.size() < expected.size()) {
    return false;
  }
  return std::equal(expected.begin(), expected.end(), value.begin(),
                    [](char left, char right) {
                      if (right >= 'A' && right <= 'Z') {
                        right = static_cast<char>(right + ('a' - 'A'));
                      }
                      return left == right;
                    });
}
struct ScalarBinding {
  std::string_view name;
  BehaviorObservation observation;
};
constexpr std::array kScalars{
    ScalarBinding{"getTargetModelId", BehaviorObservation::target_model},
    ScalarBinding{"getLeaderSquareDistance",
                  BehaviorObservation::leader_distance_squared},
    ScalarBinding{"getEnemyKnowledgeLevel",
                  BehaviorObservation::enemy_knowledge},
    ScalarBinding{"getEnemySquareDistance",
                  BehaviorObservation::enemy_distance_squared},
    ScalarBinding{"getSequenceEndState", BehaviorObservation::completion_code},
    ScalarBinding{"getLevelRandom", BehaviorObservation::level_random},
    ScalarBinding{"getNowBehaviorSec", BehaviorObservation::elapsed_seconds},
    ScalarBinding{"isEnemyCanLook", BehaviorObservation::enemy_can_look},
    ScalarBinding{"getActorLife", BehaviorObservation::actor_life},
    ScalarBinding{"getActorLifeMax", BehaviorObservation::actor_maximum_life},
    ScalarBinding{"isEnemyCanShootWithReadyGun",
                  BehaviorObservation::enemy_can_shoot_ready_gun},
    ScalarBinding{"getEnemyActorAngle", BehaviorObservation::enemy_actor_angle},
    ScalarBinding{"getEnemyMoveAngle", BehaviorObservation::enemy_move_angle},
    ScalarBinding{"getLastMessageId", BehaviorObservation::last_message_id},
    ScalarBinding{"isTargetIsPlayer", BehaviorObservation::target_is_player},
    ScalarBinding{"getLeaderAngle", BehaviorObservation::leader_angle},
    ScalarBinding{"getLastMessageSquareDistance",
                  BehaviorObservation::last_message_distance_squared},
    ScalarBinding{"getLastMessageActorAngle",
                  BehaviorObservation::last_message_actor_angle},
    ScalarBinding{"getPlayerAngle", BehaviorObservation::player_angle},
    ScalarBinding{"getPlayerSquareDistance",
                  BehaviorObservation::player_distance_squared},
    ScalarBinding{"isPlayerCanLook", BehaviorObservation::player_can_look},
    ScalarBinding{"getLastSoundId", BehaviorObservation::last_sound_id}};
}  // namespace
float behavior_script_data(BehaviorScriptSettings settings,
                           std::string_view name, std::int32_t power) {
  constexpr float kMoveDefault = 50, kNearDefault = 3, kMiddleDefault = 10,
                  kFarDefault = 20;
  float value = -1;
  if (prefix(name, "group_number")) {
    value = static_cast<float>(settings.group_number);
  } else if (prefix(name, "loop_flag")) {
    value = static_cast<float>(settings.loop_flag);
  } else if (prefix(name, "move_range")) {
    value = settings.move_range == 0 ? kMoveDefault : settings.move_range;
  } else if (prefix(name, "attack_range_near")) {
    value = settings.attack_range_near == 0 ? kNearDefault
                                            : settings.attack_range_near;
  } else if (prefix(name, "attack_range_middle")) {
    value = settings.attack_range_middle == 0 ? kMiddleDefault
                                              : settings.attack_range_middle;
  } else if (prefix(name, "attack_range_far")) {
    value = settings.attack_range_far == 0 ? kFarDefault
                                           : settings.attack_range_far;
  } else {
    return -1;
  }
  const auto base = value;
  for (std::int32_t i = 1; i < power; ++i) {
    value *= base;
  }
  return value;
}
namespace {
template <class Identity>
void bind_observations(script::Context& context, Identity identity,
                       const BehaviorObservationSource& source) {
  for (const auto binding : kScalars) {
    context.bind(
        std::string(binding.name),
        [identity, &source, observation = binding.observation](auto args) {
          owner(args, identity());
          return result(source.scalar(observation));
        });
  }
  context.bind("isCallBackOn", [identity, &source](auto args) {
    owner(args, identity());
    return result(source.callback_on(string(args, 1)) ? 1 : 0);
  });
  context.bind("getScriptData", [identity, &source](auto args) {
    owner(args, identity());
    return result(behavior_script_data(source.script_settings(),
                                       string(args, 1), integer(args, 2)));
  });
  context.bind("isPreviousBehaviorLabelsIn", [identity, &source](auto args) {
    owner(args, identity());
    return result(
        source.previous_labels(integer(args, 1), integer(args, 2)) ? 1 : 0);
  });
  context.bind("getNearPathPointActorDistanceSquare",
               [identity, &source](auto args) {
                 owner(args, identity());
                 return result(source.path_distance_squared(integer(args, 1)));
               });
  context.bind("getNearPathPointActorAngle", [identity, &source](auto args) {
    owner(args, identity());
    return result(source.path_angle(integer(args, 1)));
  });
}
}  // namespace
void bind_behavior_observations(script::Context& context,
                                std::uint32_t identity,
                                const BehaviorObservationSource& source) {
  if (!identity) throw std::invalid_argument("Invalid AI observation identity");
  bind_observations(context, [identity] { return identity; }, source);
}
void bind_scoped_behavior_observations(
    script::Context& context, const std::uint32_t& identity,
    const BehaviorObservationSource& source) {
  bind_observations(
      context,
      [&identity] {
        if (!identity) {
          throw std::logic_error("AI observation has no active actor scope");
        }
        return identity;
      },
      source);
}
}  // namespace bw::game
