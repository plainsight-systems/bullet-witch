// Native post-result power-up navigation and confirmed purchase policy.
// Original root Lua activates this task after campaign result and saves after
// it exits; it is not an in-mission pause feature. Borrow only
// UpgradePurchaseControl for this task's lifetime. The Profile outlives this
// nonmovable owner. Presentation is a copied observation. Main thread only.
// No I/O, actors, mutable clock borrows or resource grants. Frontend elapsed
// time advances original UI transitions; input is rejected during entry and
// dialog opening/closing. Purchase occurs after the confirmation closes.
// Algorithm: select one of three original categories -> select a purchasable
// row -> query current cost/affordability -> confirm -> atomically revalidate
// and purchase through Profile -> refresh from the authoritative Record.
// Cancellation never mutates progression. Exiting requests global conceal; save
// success remains a separate original save-task result. Invalid input/state
// throws before writes. Fixed six-upgrade domain, no per-frame allocation.
// Design Core C.9 (#rc-private), I.4
// (#ri-typed), Con.2 (#rconst-fct) narrow authority and typed observations;
// implementation E.4 (#re-design-invariants) revalidates at commit. Both phases
// MEM.9 (guidelines/memory/MEM.9-embedded-allocate-at-init.md) retain bounded
// storage. See player-gameplay.md and player-progression-rules.md.
#pragma once

#include "bw/math/scalar.hpp"
#include "menu_flow.hpp"
#include "power_up_presentation.hpp"
#include "profile.hpp"

namespace bw::game {
class PowerUpFlow {
 public:
  explicit PowerUpFlow(UpgradePurchaseControl purchases);
  ~PowerUpFlow() = default;
  PowerUpFlow(const PowerUpFlow&) = delete;
  PowerUpFlow& operator=(const PowerUpFlow&) = delete;
  PowerUpFlow(PowerUpFlow&&) = delete;
  PowerUpFlow& operator=(PowerUpFlow&&) = delete;
  void start();
  void update(MenuInput input, float elapsed_seconds);
  bool accepts_input() const noexcept;
  PowerUpPresentation presentation() const;
  std::size_t row_count() const;
  // Native outer art requests global conceal after its .4-second exit lead.
  // FrontEnd owns the later fade-complete/task-retirement boundary.
  bool conceal_requested() const noexcept { return conceal_requested_; }

 private:
  enum class Phase : std::uint8_t {
    outer_entry,
    category_reveal,
    categories,
    child_start,
    child_entry,
    child_reveal,
    upgrades,
    quote_hide,
    quote_route,
    quote,
    dialog_open,
    dialog,
    dialog_accept,
    dialog_close,
    dialog_retire,
    dialog_finish,
    purchase,
    child_hide,
    child_exit_start,
    child_exit,
    outer_hide,
    outer_exit_start,
    outer_exit
  };
  void transition(Phase phase);
  void advance_visuals(float seconds);
  void open_dialog();
  void finish_dialog();
  void accept();
  void cancel();
  void navigate(int direction);
  void show_upgrades();
  UpgradeSelection selected_upgrade() const;
  UpgradePurchaseControl purchases_;
  PowerUpPage page_{PowerUpPage::categories};
  UpgradeCategory category_{UpgradeCategory::stat};
  std::size_t focused_{};
  unsigned upgrade_row_{};
  PowerUpNotice notice_{PowerUpNotice::none};
  Phase phase_{Phase::outer_entry};
  float phase_seconds_{}, outer_seconds_{}, child_seconds_{},
      dialog_progress_{};
  float wallet_opacity_{}, category_strength_{1.F};
  float category_target_{1.F}, description_target_{};
  bw::math::DampedResponseState<float> description_;
  struct CategoryMotion {
    bw::math::DampedResponseState<float> displacement, kick;
  };
  std::array<CategoryMotion, 4> category_motion_{};
  float child_pulse_seconds_{}, child_highlight_{};
  float category_bob_seconds_{}, category_bob_{};
  float outer_prompt_{}, child_prompt_{}, outer_prompt_target_{},
      child_prompt_target_{};
  bool dialog_gate_open_{}, purchase_confirmed_{}, conceal_requested_{};
};
}  // namespace bw::game
