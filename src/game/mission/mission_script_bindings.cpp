// Original mission-script bridge over concrete game-owned services. Validate
// complete Lua arguments before mutation, copy retained values, and poll real
// message/fade/control-time observations. No original instruction is skipped.
// Context owns callbacks and outlives no borrowed receiver. Unsupported setup
// fails through Context rather than reporting success. Single owner thread;
// bounded scalar polls, callback allocation occurs only on native entry.
// Design I.11 (#ri-raw), implementation I.1 (#ri-explicit) and E.4
// (#re-design-invariants) preserve capability/lifetime and argument invariants.
// Both-phase MEM.9 (guidelines/memory/MEM.9-embedded-allocate-at-init.md)
// keeps waits allocation-free after their original bridge entry. See
// research/engine/subsystems/mission-startup-implementation.md.
#include <cmath>
#include <limits>
#include <stdexcept>

#include "game/world/mission_population.hpp"
#include "mission_driver.hpp"

namespace bw::game {
namespace {
void require_count(std::span<const script::Value> args, std::size_t size) {
  if (args.size() != size) {
    throw std::invalid_argument("Mission script argument count");
  }
}
double number(const script::Value& value) {
  const auto* result = std::get_if<double>(&value);
  if (result == nullptr || !std::isfinite(*result)) {
    throw std::invalid_argument("Mission script requires finite number");
  }
  return *result;
}
float scalar(const script::Value& value) {
  const auto result = number(value);
  if (std::abs(result) > std::numeric_limits<float>::max()) {
    throw std::out_of_range("Mission start scalar exceeds source range");
  }
  return static_cast<float>(result);
}
std::int32_t integer(const script::Value& value) {
  const auto result = number(value);
  if (std::floor(result) != result ||
      result < std::numeric_limits<std::int32_t>::min() ||
      result > std::numeric_limits<std::int32_t>::max()) {
    throw std::invalid_argument("Mission script requires source integer");
  }
  return static_cast<std::int32_t>(result);
}
std::uint32_t index(const script::Value& value) {
  const auto result = integer(value);
  if (result < 0) throw std::out_of_range("Negative mission script index");
  return static_cast<std::uint32_t>(result);
}
}  // namespace
MissionScriptServices& MissionDriver::script_services() const {
  if (script_services_ == nullptr) {
    throw std::logic_error(
        "Mission script services require completed world setup");
  }
  return *script_services_;
}
void MissionDriver::start(MissionScriptServices& services) {
  if (progress_.phase != MissionPhase::awaiting_world_setup || !context_ ||
      script_services_ != nullptr) {
    throw std::logic_error("Mission start admission is out of order");
  }
  // Prepare the original next entry in the same VM, never reload the chunk or
  // manufacture its placement in C++. The first advance executes its commands.
  context_->start("start");
  script_services_ = &services;
  progress_ = {MissionPhase::starting, {}};
}
void MissionDriver::script_bindings() {
  if (!context_) throw std::logic_error("Mission script context absent");
  auto& context = *context_;
  // Full mission outcomes are outside initialization. Preserve the suspended
  // native call, expose the exact missing producer, and leave world/pause
  // ownership alive. Do not return success or partially remove actors.
  for (const auto* symbol :
       {"_ExecMissionClear", "_WaitMissionClear", "_ClearMission"}) {
    context.bind(symbol, [this, symbol](auto args) {
      require_count(args, 0);
      progress_ = {MissionPhase::awaiting_gameplay,
                   std::string(symbol) +
                       " requires combat/mission-result ownership; "
                       "mission script remains suspended"};
      return script::Reply{{}, [] { return true; }};
    });
  }
  struct FamilyRemoval {
    const char* symbol;
    PopulationFamily family;
  };
  for (const auto command :
       {FamilyRemoval{"_RemoveEnemyAll", PopulationFamily::enemy},
        FamilyRemoval{"_RemoveResistanceAll", PopulationFamily::resistance},
        FamilyRemoval{"_RemoveCitizenAll", PopulationFamily::citizen}}) {
    context.bind(command.symbol, [this, family = command.family](auto args) {
      require_count(args, 0);
      script_services().remove_population(family);
      return script::Reply{};
    });
  }
  context.bind("_CancelRealTimeEventAll", [this](auto args) {
    require_count(args, 0);
    script_services().cancel_realtime_event_actors();
    return script::Reply{};
  });
  context.bind("_SetEnvSound", [this](auto args) {
    require_count(args, 1);
    script_services().set_environment_sound(index(args[0]));
    return script::Reply{};
  });
  context.bind("_RemoveBgObjAll", [this](auto args) {
    require_count(args, 0);
    script_services().remove_background();
    return script::Reply{};
  });
  context.bind("_StopMagic", [this](auto args) {
    require_count(args, 0);
    script_services().stop_magic();
    return script::Reply{};
  });
  context.bind("_ExecGameOver", [this](auto args) {
    require_count(args, 0);
    script_services().begin_game_over();
    return script::Reply{};
  });
  context.bind("_WaitGameOver", [this](auto args) {
    require_count(args, 0);
    return script::Reply{
        {}, [this] { return script_services().game_over_pending(); }};
  });
  context.bind("_PadPause", [this](auto args) {
    require_count(args, 0);
    script_services().pause(ScriptPause::input);
    return script::Reply{};
  });
  context.bind("_GamePause", [this](auto args) {
    require_count(args, 0);
    script_services().pause(ScriptPause::simulation);
    return script::Reply{};
  });
  for (const auto* symbol : {"_PadResume", "_GameResume"}) {
    context.bind(symbol, [this](auto args) {
      require_count(args, 0);
      script_services().resume();
      return script::Reply{};
    });
  }
  context.bind("_SetBgObj", [this](auto args) {
    require_count(args, 0);
    script_services().set_background();
    return script::Reply{};
  });
  context.bind("_SetPlayerPosition", [this](auto args) {
    require_count(args, 4);
    const ScriptedPlayerPose pose{
        {scalar(args[0]), scalar(args[1]), scalar(args[2])}, scalar(args[3])};
    script_services().set_player_position(pose);
    return script::Reply{};
  });
  context.bind("_SetPlayerVisible", [this](auto args) {
    require_count(args, 1);
    const auto visible = integer(args[0]);
    script_services().set_player_visible(visible != 0);
    return script::Reply{};
  });
  context.bind("_DispCockpit", [this](auto args) {
    require_count(args, 1);
    const auto visible = integer(args[0]);
    script_services().set_cockpit_visible(visible != 0);
    return script::Reply{};
  });
  context.bind("_SetTrapCallback", [this](auto args) {
    require_count(args, 3);
    const auto* function = std::get_if<std::string>(&args[0]);
    const auto kind = integer(args[1]);
    const auto event = integer(args[2]);
    if (function == nullptr || function->empty()) {
      throw std::invalid_argument("Mission trap requires callback name");
    }
    script_services().set_trap_callback(*function, {kind, event});
    return script::Reply{};
  });
  context.bind("_SetTimerCallback", [this](auto args) {
    require_count(args, 3);
    const auto* function = std::get_if<std::string>(&args[0]);
    if (function == nullptr || function->empty()) {
      throw std::invalid_argument("Mission timer requires callback name");
    }
    script_services().set_timer_callback(*function,
                                         {integer(args[1]), scalar(args[2])});
    return script::Reply{};
  });
  context.bind("_ResetGoalTarget", [this](auto args) {
    require_count(args, 0);
    script_services().reset_goal_target();
    return script::Reply{};
  });
  context.bind("_DispSystemMessage", [this](auto args) {
    require_count(args, 2);
    const auto message = index(args[0]);
    (void)integer(
        args[1]);  // Native binding reads but does not use this argument.
    constexpr std::uint32_t kLastSystemMessage = 5;
    script_services().show_system_message(
        message > kLastSystemMessage ? 0 : message);
    return script::Reply{};
  });
  context.bind("_CancelSystemMessage", [this](auto args) {
    require_count(args, 0);
    script_services().cancel_system_message();
    return script::Reply{};
  });
  context.bind("_StopBgm2", [this](auto args) {
    require_count(args, 1);
    script_services().stop_music(scalar(args[0]));
    return script::Reply{};
  });
  context.bind("_SetGoalTarget", [this](auto args) {
    require_count(args, 3);
    const std::array<float, 3> position{scalar(args[0]), scalar(args[1]),
                                        scalar(args[2])};
    script_services().set_goal_target(position);
    return script::Reply{};
  });
  context.bind("_SetCheckPoint", [this](auto args) {
    require_count(args, 1);
    script_services().set_checkpoint(index(args[0]));
    return script::Reply{};
  });
  context.bind("_DispWindowMessage2", [this](auto args) {
    require_count(args, 1);
    script_services().show_message(index(args[0]));
    return script::Reply{};
  });
  context.bind("_WaitWindowMessage2", [this](auto args) {
    require_count(args, 0);
    return script::Reply{
        {}, [this] { return script_services().message_pending(); }};
  });
  context.bind("_FadeIn", [this](auto args) {
    require_count(args, 1);
    const auto seconds = scalar(args[0]);
    if (seconds < 0) throw std::invalid_argument("Negative mission fade");
    script_services().fade_in(seconds);
    return script::Reply{};
  });
  context.bind("_FadeWait", [this](auto args) {
    require_count(args, 0);
    return script::Reply{{},
                         [this] { return script_services().fade_pending(); }};
  });
  context.bind("_Wait", [this](auto args) {
    require_count(args, 1);
    const auto seconds = scalar(args[0]);
    if (seconds < 0) throw std::invalid_argument("Negative mission wait");
    // Native Wait tests its retained float before subtracting this frame's
    // interval and yields after subtraction, even when it crosses zero.
    return script::Reply{
        {}, [this, remaining = seconds, last = std::uint64_t{}]() mutable {
          if (remaining <= 0) return false;
          const auto interval = script_services().control_interval();
          if (interval.sequence != last) {
            remaining -= interval.delta_seconds;
            last = interval.sequence;
          }
          return true;
        }};
  });
  context.bind("_PlayBgm", [this](auto args) {
    require_count(args, 1);
    script_services().play_music(index(args[0]));
    return script::Reply{};
  });
}
}  // namespace bw::game
