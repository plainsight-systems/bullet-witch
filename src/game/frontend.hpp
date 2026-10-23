// Front-end composition owns load/menu policies, borrows profile/storage/media/
// presentation until destruction, and exposes completion to the original root.
// Calls are single-threaded. Resource errors throw; ordinary persistence errors
// require retry or explicit continuation without saving. Original assets remain
// owned by MenuSurface across the load/menu transition and release on
// departure. Algorithm: load -> reveal menu -> select -> persist -> conceal ->
// publish. Inputs are pressed/repeated masks and game delta; outputs are screen
// progress and a bounded selection value. It does not create or simulate a
// mission. Frame work is bounded; storage performs bounded I/O only on load or
// explicit changes. Design MCP I.30 (#ri-encapsulate) isolates compatibility
// behavior; implementation E.6 (#re-raii) retires presentation during unwind;
// cpp-performance COPY.4
// (guidelines/copy-move/COPY.4-rule-of-five-rule-of-zero.md) makes this
// borrowed-capability lifetime explicit. See frontend-integration.md.
#pragma once
#include "menu_surface.hpp"
#include "profile_load_flow.hpp"
#include "root_driver.hpp"

namespace bw::game {
class FrontEnd {
 public:
  FrontEnd(Profile& profile, platform::SaveStorage& storage, LogoMedia& media,
           MenuSurface& surface, MenuUnlocks unlocks);
  ~FrontEnd();
  FrontEnd(const FrontEnd&) = delete;
  FrontEnd& operator=(const FrontEnd&) = delete;
  FrontEnd(FrontEnd&&) = delete;
  FrontEnd& operator=(FrontEnd&&) = delete;
  void start(Screen screen);
  void update(Screen screen, LogoUpdate input, std::uint32_t repeated);
  ScreenProgress progress(Screen screen) const;
  const std::optional<MenuSelection>& selection() const;
  const Profile& profile() const { return profile_; }

 private:
  enum class Phase : std::uint8_t {
    resources,
    revealing,
    menu,
    save_error,
    conceal,
    closing,
    finish
  };
  void show_error(ProfileResult result, unsigned choice);
  void persist();
  Profile& profile_;
  platform::SaveStorage& storage_;
  LogoMedia& media_;
  MenuSurface& surface_;
  ProfileLoadFlow load_;
  MenuFlow menu_;
  ScreenProgress menu_progress_;
  Phase phase_{Phase::resources};
  ProfileResult save_error_;
  unsigned error_choice_{};
  bool saving_enabled_{true};
};
}  // namespace bw::game
