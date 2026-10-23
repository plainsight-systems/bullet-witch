// Startup route extension: game policies borrow capabilities; composition owns
// them through cleanup. Design MCP R.20 (#rr-owner) retains explicit lifetime;
// implementation E.6 (#re-raii) unwinds on failures. cpp-performance COPY.4
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

#include "advertise_flow.hpp"
#include "bw/tasks/task_lifecycle_scheduler.hpp"
#include "root_driver.hpp"
#include "startup_flows.hpp"
#include "title_surface.hpp"

namespace bw::game {
class StartupSession final : public StartupServices {
 public:
  explicit StartupSession(LogoMedia& media,
                          TitleSurface* title_surface = nullptr);
  ~StartupSession() override;
  StartupSession(const StartupSession&) = delete;
  StartupSession& operator=(const StartupSession&) = delete;
  StartupSession(StartupSession&&) = delete;
  StartupSession& operator=(StartupSession&&) = delete;
  // Starts the owned root exactly once. Resources/tag follow RootDriver's
  // borrowed lifetime and exclusive-tag contract. No root exists before this.
  void start_root(resources::ResourceService& resources,
                  resources::ResourceTag tag);
  script::RunResult advance(LogoUpdate input);
  ScreenProgress progress(Screen screen) const;
  bool window_close_enabled() const { return window_close_enabled_; }
  double global(int slot) const override;
  void set_global(int slot, double value) override;
  void execute(Screen screen) override;
  bool pending(Screen screen) override;
  double status(Screen screen) const override;
  double latest_stage() const override;

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
  ScreenTask logo_task_, check_task_, advertise_task_, title_task_;
  LogoUpdate input_;
  std::int32_t title_return_request_{};
  bool window_close_enabled_{};
  script::RunResult root_result_{script::RunState::idle, {}};
  std::unique_ptr<RootDriver> root_;
  // Destroy callbacks before root/flow state. Destruction never ticks them.
  tasks::TaskLifecycleScheduler scheduler_;
};
}  // namespace bw::game
