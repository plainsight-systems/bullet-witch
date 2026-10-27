// Mission coroutine admission observes the borrowed mission control owner.
// User pause blocks this coroutine; scripted world pause alone does not. Root
// control/fade services have their own live route. Transition-specific native
// bypass producers remain unavailable with genuine world startup. C.9/I.1
// design, E.4 implementation and both-phase MEM.9 preserve bounded owner-thread
// queries. Mission spell restriction binding borrows the explicit mission
// player owner through VM teardown. Only unchanged Lua publication changes the
// mask: load completion does not run start() or preapply mission-specific
// constants. Core I.30/C.9 design and E.4 (#re-design-invariants)
// implementation validate before publication; both-phase MEM.9
// (guidelines/memory/ MEM.9-embedded-allocate-at-init.md) adds no frame storage
// allocation. Game mission-script lifetime and load orchestration. Own one Lua
// context and an exclusive resource tag; borrow resources and load services
// through teardown. Calls are single-threaded. Encoded publication, native load
// readiness and gameplay setup are distinct: blocked loads suspend Lua,
// failures are terminal, and completing load never starts gameplay. Destruction
// cancels Lua polls before releasing tagged scripts. Native mission heap
// capacity is 256 KiB. Algorithm: request selected
// mission/default_callback packages -> load original chunks in native order ->
// prepare load -> pump native requests -> attach startup owners -> run start.
// After start, condition selection supplies one serial callback in the same VM.
// A named unimplemented gameplay producer retains its native wait and publishes
// awaiting_gameplay; world/pause owners remain live. Context/callback handles
// never escape this owner. Mission aliases are
// resolved by the session, not by modifying original Lua. Costs: O(package
// bytes) at setup plus bounded Lua pump work; named requests copy once across
// the wait boundary, not per poll. Runtime/package storage retains the injected
// Domain. Design MCP E.6 (#re-raii) owns partial failure cleanup;
// cpp-performance MEM.6
// (guidelines/memory/MEM.6-inject-allocators-behind-one-interface.md) injects
// domain ownership. See research/engine/subsystems/mission-launch.md.
#pragma once
#include "bw/content/formats/named_file_package.hpp"
#include "bw/content/resources/service.hpp"
#include "bw/script/context.hpp"
#include "mission_control.hpp"
#include "mission_player_state.hpp"
#include "mission_script_services.hpp"

namespace bw::game {
enum class MissionLoadKind : std::uint8_t {
  character_light,
  sky,
  stage,
  text,
  score,
  hiding_places,
  sound,
  sound_environment,
  sound_reverb,
  magic,
  background_objects,
  paths,
  placements
};
enum class MissionLoadState : std::uint8_t { pending, ready, blocked, failed };
struct MissionLoadReport {
  MissionLoadState state{MissionLoadState::pending};
  std::string diagnostic;
};
class MissionLoadServices {
 public:
  MissionLoadServices() = default;
  virtual ~MissionLoadServices() = default;
  MissionLoadServices(const MissionLoadServices&) = delete;
  MissionLoadServices& operator=(const MissionLoadServices&) = delete;
  MissionLoadServices(MissionLoadServices&&) = delete;
  MissionLoadServices& operator=(MissionLoadServices&&) = delete;
  // Retain the selected original package before load; ancillary members borrow
  // this owned payload until cancellation.
  virtual void retain_package(resources::NamedFilePackage package) = 0;
  // Immediate script activation starts presentation; it does not wait for I/O.
  virtual void begin_loading_scene(unsigned item) = 0;
  // A request copies its retained name; poll never reports unsupported work
  // ready.
  virtual void request(MissionLoadKind kind, std::string_view name) = 0;
  virtual MissionLoadReport poll() = 0;
  virtual void cancel() noexcept = 0;
};
enum class MissionPhase : std::uint8_t {
  scripts,
  loading,
  blocked,
  awaiting_world_setup,
  starting,
  running,
  awaiting_gameplay,
  failed,
  cancelled
};
struct MissionProgress {
  MissionPhase phase{MissionPhase::scripts};
  std::string diagnostic;
};
class MissionDriver {
 public:
  MissionDriver(resources::ResourceService& resources,
                MissionLoadServices& loads, resources::ResourceTag tag,
                std::string_view script_name,
                const std::shared_ptr<memory::Domain>& runtime_memory,
                const MissionControl& control,
                MissionPlayerState* player = nullptr);
  ~MissionDriver();
  MissionDriver(const MissionDriver&) = delete;
  MissionDriver& operator=(const MissionDriver&) = delete;
  MissionDriver(MissionDriver&&) = delete;
  MissionDriver& operator=(MissionDriver&&) = delete;
  // Advance genuine native waits; awaiting_world_setup retains the original VM.
  const MissionProgress& advance();
  // Bind after asset readiness and stable script-runtime ownership. This
  // retains the same loaded VM; services must outlive the driver.
  // Repeated/early admission is rejected.
  void start(MissionScriptServices& services);
  // Retire native callbacks before their world receivers. Asset cancellation
  // remains separate so render borrowers can release before load definitions.
  void retire_script() noexcept;
  void cancel() noexcept;
  const MissionProgress& progress() const { return progress_; }
  std::string_view stage_name() const { return stage_name_; }
  memory::HeapSnapshot memory_snapshot() const {
    return context_ ? context_->memory_snapshot() : memory::HeapSnapshot{};
  }

 private:
  void bindings();
  void script_bindings();
  void advance_callbacks();
  MissionScriptServices& script_services() const;
  struct ScriptPackage {
    std::string_view resource;
    std::string_view member;
  };
  resources::NamedFilePackage load_package(ScriptPackage source);
  resources::ResourceService& resources_;
  MissionLoadServices& loads_;
  resources::ResourceTag tag_;
  std::shared_ptr<memory::Domain> memory_;
  memory::String script_name_, package_name_, stage_name_;
  MissionProgress progress_;
  MissionPlayerState* player_{};
  MissionScriptServices* script_services_{};
  const MissionControl& control_;
  std::optional<script::Context> context_;
  std::optional<script::CoroutineId> callback_;
};
}  // namespace bw::game
