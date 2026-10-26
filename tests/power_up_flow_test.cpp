// In-process native power-up transaction/navigation checks. Real Profile owns
// the Record; UI only gets its purchase capability. No fabricated world, files,
// processes or game assets. Small fixed values bound all work. Both-phase Core
// C.9/E.4 and MEM.9 check authority, revalidation and rejection without adding
// production setters for fixture convenience.
#include "game/power_up_flow.hpp"

#include <stdexcept>
#include <type_traits>

#include "game/mission_player_state.hpp"

namespace {
using bw::game::PowerUpFlow;
using bw::game::Profile;
using bw::game::UpgradeCategory;
constexpr std::uint32_t kConfirm = 0x1010, kCancel = 0x2020, kDown = 0x02000000;
void require(bool value, const char* reason) {
  if (!value) throw std::runtime_error(reason);
}
void settle(PowerUpFlow& flow) {
  constexpr float kFrame = 1.F / 60.F;
  for (unsigned frame = 0;
       frame < 120 && !flow.accepts_input() && !flow.conceal_requested();
       ++frame) {
    flow.update({}, kFrame);
  }
  require(flow.accepts_input() || flow.conceal_requested(),
          "bounded source UI transition completes");
}
void command(PowerUpFlow& flow, bw::game::MenuInput input) {
  settle(flow);
  flow.update(input, 1.F / 60.F);
  settle(flow);
}
void choose(PowerUpFlow& flow) { command(flow, {kConfirm, 0}); }
void next(PowerUpFlow& flow) { command(flow, {0, kDown}); }
void back(PowerUpFlow& flow) { command(flow, {kCancel, 0}); }
template <class T>
concept CanAwardResults = requires(T value) { value.award_result({}); };
template <class T>
concept CanSetCostume = requires(T value) { value.set_costume(1); };
static_assert(!CanAwardResults<bw::game::UpgradePurchaseControl>);
static_assert(!CanSetCostume<bw::game::UpgradePurchaseControl>);
static_assert(!std::is_copy_constructible_v<Profile>);
static_assert(!std::is_move_constructible_v<Profile>);
static_assert(!std::is_copy_constructible_v<bw::game::MissionPlayerState>);
static_assert(!std::is_move_constructible_v<bw::game::MissionPlayerState>);
static_assert(!std::is_move_constructible_v<PowerUpFlow>);

void confirmation_and_cancel() {
  Profile profile;
  (void)profile.award_result({0, 0});
  const auto before = profile.progression();
  PowerUpFlow flow(profile.upgrade_purchases());
  choose(flow);
  choose(flow);
  require(flow.presentation().page == bw::game::PowerUpPage::confirmation &&
              profile.progression() == before,
          "entering confirmation never spends points");
  back(flow);
  require(profile.progression() == before && flow.presentation().focused == 1 &&
              flow.presentation().page == bw::game::PowerUpPage::confirmation,
          "Back selects No without spending or skipping the source dialog");
  choose(flow);
  choose(flow);
  choose(flow);
  require(profile.progression().levels.stats[0] == 2 &&
              profile.progression().points == 150,
          "confirmed native stat row spends source cost once");
  back(flow);
  next(flow);
  choose(flow);
  next(flow);
  choose(flow);
  choose(flow);
  require(profile.progression().levels.weapons[1] == 1 &&
              profile.progression().points == 110,
          "weapon acquisition uses its category and original first price");
}
void rejection_and_revalidation() {
  Profile profile;
  PowerUpFlow flow(profile.upgrade_purchases());
  choose(flow);
  choose(flow);
  require(flow.presentation().notice ==
                  bw::game::PowerUpNotice::insufficient_points &&
              profile.progression().levels.stats[0] == 1,
          "unaffordable row opens the source notice without a purchase");
  (void)profile.award_result({0, 0});
  flow.start();
  choose(flow);
  choose(flow);
  // A separate legitimate Record transaction invalidates the displayed quote.
  require(profile.purchase({UpgradeCategory::stat, 0}) ==
              bw::game::PurchaseResult::purchased,
          "fixture performs an ordinary competing purchase");
  const auto changed = profile.progression();
  choose(flow);
  require(profile.progression() == changed &&
              flow.presentation().notice ==
                  bw::game::PowerUpNotice::insufficient_points,
          "confirmation rechecks new level/cost and cannot spend old quote");
}
void source_transition_admission() {
  Profile profile;
  (void)profile.award_result({0, 0});
  PowerUpFlow flow(profile.upgrade_purchases());
  // Entry drawing is observable, but early confirm cannot queue a purchase.
  flow.update({kConfirm, 0}, .5F);
  require(!flow.accepts_input() &&
              flow.presentation().page == bw::game::PowerUpPage::categories,
          "source entry track gates input");
  settle(flow);
  choose(flow);
  choose(flow);
  const auto before = profile.progression();
  flow.update({kConfirm, 0}, 1.F / 60.F);
  require(!flow.accepts_input() && profile.progression() == before,
          "confirm starts dialog closure before any debit");
  for (unsigned frame = 0; frame < 10; ++frame) {
    flow.update({kConfirm, 0}, 1.F / 60.F);
  }
  require(profile.progression() == before,
          "confirmation edges cannot buy while the source dialog remains open");
  settle(flow);
  require(profile.progression().levels.stats[0] == 2 &&
              profile.progression().points == 150,
          "retired confirmation commits exactly one transaction");
}
void original_exit_and_story_exclusion() {
  Profile profile;
  PowerUpFlow flow(profile.upgrade_purchases());
  next(flow);
  next(flow);
  choose(flow);
  require(
      flow.row_count() == 6,
      "only six original purchasable rows, no synthetic Back or story rows");
  back(flow);
  back(flow);
  require(!flow.conceal_requested() && flow.presentation().focused == 3,
          "first root Back selects Continue");
  back(flow);
  require(flow.conceal_requested(), "second root Back closes task");
  const auto before = profile.progression();
  choose(flow);
  require(profile.progression() == before,
          "completed task cannot process stale confirmation input");
}
void source_audio_feedback() {
  using bw::game::MenuSound;
  Profile profile;
  (void)profile.award_result({0, 0});
  PowerUpFlow flow(profile.upgrade_purchases());
  const auto opening = flow.take_sounds();
  require(opening.size == 1 && opening.requests[0].sound == MenuSound::panel &&
              opening.requests[0].delay_seconds == 0,
          "Power Up emits original immediate panel cue");
  require(flow.take_sounds().size == 0, "Opening cue drains once");
  settle(flow);
  flow.update({0, kDown}, 0);
  require(flow.take_sounds().requests[0].sound == MenuSound::cursor,
          "Category move emits source cursor");
  flow.update({0, 0x01000000}, 0);
  flow.update({kConfirm, 0}, 0);
  require(flow.take_sounds().requests[0].sound == MenuSound::confirm,
          "Child admission confirms once");
  settle(flow);
  flow.update({kConfirm, 0}, 0);
  unsigned opened = 0, closed = 0, purchased = 0;
  const auto drain = [&] {
    const auto sounds = flow.take_sounds();
    for (std::size_t i = 0; i < sounds.size; ++i) {
      const auto sound = sounds.requests.at(i).sound;
      opened += sound == MenuSound::dialog_open;
      closed += sound == MenuSound::dialog_close;
      purchased += sound == MenuSound::upgrade_purchase;
    }
  };
  for (unsigned i = 0; i < 120 && !flow.accepts_input(); ++i) {
    flow.update({}, 1.F / 60.F);
    drain();
  }
  require(opened == 1 && purchased == 0,
          "Quote opens dialog without purchase cue");
  flow.update({kConfirm, 0}, 0);
  drain();
  for (unsigned i = 0; i < 120 && !flow.accepts_input(); ++i) {
    flow.update({}, 1.F / 60.F);
    drain();
  }
  require(closed == 1 && purchased == 1,
          "Retired successful purchase emits one dedicated cue");
  flow.update({kCancel, 0}, 0);
  require(flow.take_sounds().requests[0].sound == MenuSound::back,
          "Child Back uses original back cue");
}

}  // namespace
void power_up_flow_contracts() {
  confirmation_and_cancel();
  rejection_and_revalidation();
  original_exit_and_story_exclusion();
  source_transition_admission();
  source_audio_feedback();
}
