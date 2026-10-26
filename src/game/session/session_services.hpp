// Responsibility grouping preserves the existing contract and owner policies.
// Design Core SF.11 (#rs-contained), implementation SF.13
// (#rs-portable-header-id), and both-phase performance COPY.4
// (guidelines/copy-move/COPY.4-rule-of-five-rule-of-zero.md) keep direct
// includes portable and preserve special members without extra payload copies.
// Gameplay handoff captures a typed permanent progression snapshot at the
// original StartMission boundary. It is immutable input to mission state, never
// a second permanent authority. Core C.9/I.30 (design), E.4 (implementation)
// preserve ownership; MEM.9
// (guidelines/memory/MEM.9-embedded-allocate-at-init.md) bounds the added copy
// to 15 level bytes and currency, once per entry. Memory migration: native
// runtime domain owns scheduler storage and the shared root/control block; Lua
// owns bounded backing from that domain. Legacy tests may omit the domain.
// Callbacks retire before root/flow destruction. Design/implementation R.1
// (#rr-raii) and MEM.6 (guidelines/memory/
// MEM.6-inject-allocators-behind-one-interface.md) govern ordered ownership.
// Front-end extension: FrontEnd is borrowed through session destruction. Lua
// remains the route owner; StartMission validates its identifier against the
// confirmed menu selection and captures an owned request/checkpoint once.
// Concept aliases retain selected package identity separately from root name.
// WaitMission remains pending until the owning composition reports actual
// retirement. Restart and checkpoint-zero reentry keep that wait pending;
// actual saved-world restoration and victory remain unavailable. Only source
// abort status 2 is supported. Root music/concept-unload acknowledge completed
// owner teardown in order, never ignore a live owner. I.1/E.6 and MEM.9 were
// reread for these implementation invariants; the retained status adds fixed
// storage. Main-thread only. Invalid/mismatched routes throw, never become
// successful gameplay. Design MCP I.30 (#ri-encapsulate) keeps compatibility
// slots at this boundary; implementation E.6 (#re-raii) retains owned request
// cleanup; cpp-performance COPY.4
// (guidelines/copy-move/COPY.4-rule-of-five-rule-of-zero.md) gives request data
// ordinary container ownership. Checkpoint copy is O(bytes) at handoff, not
// per-frame. See research/engine/subsystems/frontend-integration.md. Startup
// route extension: game policies borrow capabilities; composition owns them
// through cleanup. Design MCP R.20 (#rr-owner) retains explicit lifetime;
// implementation E.6
// (#re-raii) unwinds on failures. cpp-performance COPY.4
// (guidelines/copy-move/COPY.4-rule-of-five-rule-of-zero.md) preserves
// container ownership without per-frame payload copies. Shared presentation
// contracts are described in
// research/engine/subsystems/startup-title-integration.md. G2: startup session
// composition: original root and reconstructed screen tasks. Owns one
// scheduler, optional root driver, four flow policies and typed startup state.
// LogoMedia, TitleSurface and resources passed to start_root outlive this
// object. Single owning thread; no concurrent calls or recursive advance.
// Unsupported screens/globals fail explicitly. A failed flow never satisfies a
// script wait. Algorithm: start_root registers the driver; execute
// registers/activates a screen; advance snapshots input and settles lifecycle
// before updates; a completed screen requests idle, and its Wait observes idle
// then requests destruction. Screen key 0x2000 precedes root key 0x2001;
// equal-key ordering remains stable. Normal flow bookkeeping is fixed-size;
// playlist and task registration allocate at setup, while the existing
// scheduler allocates its bounded tick snapshot. No allocation-free scheduler
// or measured performance claim is implied. Design and implementation MCP:
// cpp-guidelines I.1
// (#ri-explicit) makes owners, failures and session fields explicit;
// cpp-performance MEM.9 (guidelines/memory/MEM.9-embedded-allocate-at-init.md)
// keeps per-frame flow state inline and confines composition allocation to
// registration/setup.
#pragma once
#include <memory>
#include <optional>

#include "bw/tasks/task_lifecycle_scheduler.hpp"
#include "game/mission/mission_transition.hpp"
#include "game/ui/frontend/frontend.hpp"
#include "game/ui/frontend/title_surface.hpp"
#include "game/ui/startup/advertise_flow.hpp"
#include "game/ui/startup/startup_flows.hpp"
#include "root_driver.hpp"

namespace bw::game {
struct GameplayRequest {
  // Owned handoff data; capturing this request does not yet load a world.
  std::string mission;
  MenuSelection selection;
  std::vector<std::byte> checkpoint;
  // Selected concept package/member alias; mission retains the original Lua
  // name.
  std::string script_name;
  Progression progression;
};
class GameSession final : public SessionServices {
 public:
  explicit GameSession(LogoMedia& media, TitleSurface* title_surface = nullptr,
                       FrontEnd* frontend = nullptr,
                       std::shared_ptr<memory::Domain> runtime_memory = {},
                       FrontEndAudio* audio = nullptr);
  ~GameSession() override;
  GameSession(const GameSession&) = delete;
  GameSession& operator=(const GameSession&) = delete;
  GameSession(GameSession&&) = delete;
  GameSession& operator=(GameSession&&) = delete;
  // Starts the owned root exactly once. Resources/tag follow RootDriver's
  // borrowed lifetime and exclusive-tag contract. No root exists before this.
  void start_root(resources::ResourceService& resources,
                  resources::ResourceTag tag);
  script::RunResult advance(LogoUpdate input, std::uint32_t repeated = 0,
                            const input::PhysicalState& physical = {},
                            float vertical_axis = 0);
  ScreenProgress progress(Screen screen) const;
  bool window_close_enabled() const { return window_close_enabled_; }
  double global(int slot) const override;
  void set_global(int slot, double value) override;
  // Register screen work; pending waits for activation and eventual idle state.
  void execute(Screen screen) override;
  bool pending(Screen screen) override;
  double status(Screen screen) const override;
  double latest_stage() const override;
  void start_mission(std::string_view mission) override;
  bool mission_pending() override;
  double mission_status() const override;
  void stop_mission_music() override;
  void unload_concept_mission() override;
  // Called only after mission/actor/audio/resource owners have retired. The
  // original root consumes status 2; victory/results are not representable.
  void complete_mission_retirement();
  // Validate before destroying live owners. Checkpoint reentry currently admits
  // only absent snapshots; real imported world restoration remains unavailable.
  // Completion follows actual teardown, refreshes permanent entry observations,
  // retains the captured live appearance and the root wait for reentry. The
  // request observes committed mission state before its owners retire; no
  // Profile/save mutation or capability travels through this value handoff.
  void validate_mission_transition(MissionTransitionRequest request) const;
  void complete_mission_transition(MissionTransitionRequest request);
  const std::optional<GameplayRequest>& gameplay_request() const {
    return gameplay_request_;
  }
  bool quit_requested() const;

 private:
  struct ScreenTask {
    std::optional<tasks::TaskId> id;
    bool executed{};
    bool waiting_for_activation{};
  };
  ScreenTask& task(Screen screen);
  void activate(Screen screen);
  void update(Screen screen);
  CompanyLogoFlow logos_;
  StartupCheckFlow check_;
  AdvertiseFlow advertise_;
  TitleFlow title_;
  TitleSurface* title_surface_{};
  FrontEnd* frontend_{};
  FrontEndAudio* audio_{};
  ScreenTask logo_task_, check_task_, advertise_task_, title_task_, load_task_,
      menu_task_, power_up_task_;
  LogoUpdate input_;
  std::uint32_t repeated_{};
  input::PhysicalState physical_;
  float vertical_axis_{};
  unsigned campaign_stage_{1}, replay_stage_{1};
  bool replay_mode_{}, concept_mode_{};
  std::optional<GameplayRequest> gameplay_request_;
  bool mission_retired_{}, retired_music_observed_{}, concept_unloaded_{};
  std::int32_t title_return_request_{};
  bool window_close_enabled_{};
  script::RunResult root_result_{script::RunState::idle, {}};
  std::shared_ptr<memory::Domain> runtime_memory_;
  std::shared_ptr<RootDriver> root_;
  // Destroy callbacks before root/flow state. Destruction never ticks them.
  tasks::TaskLifecycleScheduler scheduler_;
};
}  // namespace bw::game
