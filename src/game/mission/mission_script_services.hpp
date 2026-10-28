// Narrow receivers for original mission commands and serial callbacks.
// Condition scan/take confers no Lua or world mutation capability. The VM
// borrows one stable service owner through its lifetime; arguments are values
// or call-only strings and never confer world, profile or clock mutation.
// Each operation must reach its real game owner before returning. Pending
// message/fade observations name actual retained tasks; absent services throw
// rather than acknowledge an unsupported operation. Script waits observe the
// supplied immutable control interval and never sample wall time. Single game
// thread, fixed scalar work except owner-defined setup operations. Design
// Core I.11 (#ri-raw) makes borrowing explicit; E.6 (#re-raii) keeps owner
// teardown ordered. Performance MEM.9 (guidelines/memory/
// MEM.9-embedded-allocate-at-init.md) keeps bridge polling allocation-free.
// Source sequence and limits: mission-startup-implementation.md.
#pragma once
#include <cstdint>
#include <string_view>

#include "game/player/camera/initial_camera.hpp"
#include "mission_callbacks.hpp"
#include "mission_control.hpp"

namespace bw::game {
enum class PopulationFamily : std::uint8_t;
struct MissionScriptInterval {
  std::uint64_t sequence{};
  float delta_seconds{};
};
class MissionScriptServices {
 public:
  MissionScriptServices() = default;
  virtual ~MissionScriptServices() = default;
  MissionScriptServices(const MissionScriptServices&) = delete;
  MissionScriptServices& operator=(const MissionScriptServices&) = delete;
  MissionScriptServices(MissionScriptServices&&) = delete;
  MissionScriptServices& operator=(MissionScriptServices&&) = delete;
  virtual void pause(ScriptPause kind) = 0;
  virtual void resume() = 0;
  virtual void set_background() = 0;
  virtual void set_player_position(ScriptedPlayerPose pose) = 0;
  virtual void set_player_visible(bool visible) = 0;
  virtual void set_cockpit_visible(bool visible) = 0;
  virtual void set_trap_callback(std::string_view function,
                                 MissionTrapEvent) = 0;
  virtual void set_timer_callback(std::string_view function,
                                  MissionTimerEvent timer) = 0;
  virtual void scan_callbacks() = 0;
  virtual std::optional<MissionCallbackName> take_callback() = 0;
  virtual void set_goal_target(const std::array<float, 3>& position) = 0;
  virtual void reset_goal_target() = 0;
  virtual void show_system_message(std::uint32_t index) = 0;
  virtual void cancel_system_message() = 0;
  virtual std::uint32_t checkpoint() const = 0;
  virtual void set_checkpoint(std::uint32_t checkpoint) = 0;
  virtual void show_message(std::uint32_t message) = 0;
  virtual bool message_pending() const = 0;
  virtual void fade_in(float seconds) = 0;
  virtual bool fade_pending() const = 0;
  virtual MissionScriptInterval control_interval() const = 0;
  virtual void play_music(std::uint32_t index) = 0;
  virtual void stop_music(float seconds) = 0;
  virtual void remove_population(PopulationFamily) = 0;
  virtual void remove_background() = 0;
  virtual void cancel_realtime_event_actors() = 0;
  virtual void set_environment_sound(std::uint32_t) = 0;
  virtual void stop_magic() = 0;
  virtual void begin_game_over() = 0;
  virtual bool game_over_pending() const = 0;
};
}  // namespace bw::game
