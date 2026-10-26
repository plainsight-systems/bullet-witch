// Front-end composition owns load/menu policies, borrows profile/storage/media/
// presentation until destruction, and exposes completion to the original root.
// Calls are single-threaded. Resource errors throw; ordinary persistence errors
// follow original retry/overwrite decisions. Original assets remain
// owned by MenuSurface across the load/menu transition and release on
// departure. Algorithm: load -> reveal menu -> select -> conceal ->
// publish. Inputs are pressed/repeated masks and game delta; outputs are screen
// progress and a bounded selection value. It does not create or simulate a
// mission. The existing presentation delta advances menu-owned tracks during
// reveal and interaction; native menu admission owns its additional gate.
// Design/implementation I.1 (#ri-explicit) and MEM.9 (guidelines/memory/
// MEM.9-embedded-allocate-at-init.md) keep that handoff explicit and bounded.
// Frame work is bounded; storage performs bounded I/O only on load or
// explicit changes. Design MCP I.30 (#ri-encapsulate) isolates compatibility
// behavior; implementation E.6 (#re-raii) retires presentation during unwind;
// cpp-performance COPY.4
// (guidelines/copy-move/COPY.4-rule-of-five-rule-of-zero.md) makes this
// borrowed-capability lifetime explicit. See frontend-integration.md.
#pragma once
#include "frontend_options.hpp"
#include "menu_surface.hpp"
#include "power_up_flow.hpp"
#include "profile_load_flow.hpp"
#include "ranking_flow.hpp"
#include "root_driver.hpp"

namespace bw::game {
class FrontEndAudio;
class FrontEnd {
 public:
  FrontEnd(Profile& profile, platform::SaveStorage& storage, LogoMedia& media,
           MenuSurface& surface, MenuUnlocks unlocks,
           PowerUpOutput* power_up_output = nullptr,
           FrontEndAudio* audio = nullptr,
           std::optional<FrontendOptionsServices> options = std::nullopt,
           RankingOutput* ranking_output = nullptr);
  ~FrontEnd();
  FrontEnd(const FrontEnd&) = delete;
  FrontEnd& operator=(const FrontEnd&) = delete;
  FrontEnd(FrontEnd&&) = delete;
  FrontEnd& operator=(FrontEnd&&) = delete;
  // Profile loading precedes menu activation; this owner coordinates
  // fades/saves.
  void start(Screen screen);
  void update(Screen screen, LogoUpdate input, std::uint32_t repeated,
              const input::PhysicalState& physical = {},
              float vertical_axis = 0);
  ScreenProgress progress(Screen screen) const;
  const std::optional<MenuSelection>& selection() const;
  const Profile& profile() const { return profile_; }

 private:
  enum class Phase : std::uint8_t {
    resources,
    revealing,
    menu,
    conceal,
    closing,
    finish
  };
  void update_options(LogoUpdate input, MenuInput menu_input,
                      const input::PhysicalState& physical);
  void complete_options_child();
  void update_power_up(LogoUpdate input, std::uint32_t repeated);
  Profile& profile_;
  platform::SaveStorage& storage_;
  LogoMedia& media_;
  MenuSurface& surface_;
  PowerUpOutput* power_up_output_{};
  FrontEndAudio* audio_{};
  FrontendOptionsOutput* options_output_{};
  RankingOutput* ranking_output_{};
  RankingFlow ranking_;
  Progression child_progression_;
  std::optional<PauseOptionsFlow> options_;
  bool options_active_{}, options_returning_{};
  std::optional<OptionsChild> options_child_;
  ProfileLoadFlow load_;
  MenuFlow menu_;
  PowerUpFlow power_up_;
  ScreenProgress power_up_progress_;
  Phase power_up_phase_{Phase::resources};
  ScreenProgress menu_progress_;
  Phase phase_{Phase::resources};
};
}  // namespace bw::game
