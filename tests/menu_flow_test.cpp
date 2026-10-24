// V1: public menu navigation, persisted choices and immutable handoff
// contracts. In-memory profile only; no storage processes, rendering or game
// assets. Algorithm: navigate pages using actual action masks, verify state and
// results. Bounded values and fixed action sequences. MCP
// design/implementation: Enum.3
// (#renum-class), SL.con.3 (#rsl-bounds), performance COPY.7
// (guidelines/copy-move/COPY.7-eliminate-hidden-copies.md) use explicit values
// and references. This does not establish visual fidelity or gameplay
// execution. Concept-menu change design/implementation: I.1 (#ri-explicit)
// verifies the explicit policy independently from campaign gates; COPY.7
// retains borrowed presentations and small action values.
#include "game/menu_flow.hpp"

#include <stdexcept>
#include <vector>

#include "game/pause_flow.hpp"
#include "game/profile.hpp"
#include "media_memory_fixture.hpp"

namespace {
void require(bool value, const char* message) {
  if (!value) {
    throw std::runtime_error(message);
  }
}
void down(bw::game::MenuFlow& flow) { flow.update({0, 0x02000000}); }
void accept(bw::game::MenuFlow& flow) { flow.update({0x1010, 0}); }
void cancel(bw::game::MenuFlow& flow) { flow.update({0x2020, 0}); }
void pause_review_transactions() {
  // An empty immutable catalog exercises unavailable clip pages without
  // preparing unrelated animation fixtures. Actual names/pages use the corpus.
  constexpr std::array<std::uint8_t, 8> skeleton_bytes{'_', 'B', 'L', 'B',
                                                       0,   0,   0,   0};
  const auto domain =
      bw::memory::Domain::create(std::make_shared<bw::test::MediaBacking>());
  const auto skeleton =
      bw::formats::SkeletonDefinition::parse(skeleton_bytes, domain);
  const bw::animation::PreparedClipCatalog catalog({}, skeleton, 0, {}, domain);
  bw::game::PauseFlow pause(0);
  pause.bind_review_catalogs(catalog, catalog, 0);
  pause.update(true, {});
  const auto choose = [&](std::string_view label) {
    for (std::size_t i = 0; i < pause.presentation().count; ++i) {
      if (pause.presentation().rows.at(pause.presentation().selected).label ==
          label) {
        pause.update(false, {0x1010, 0});
        return;
      }
      pause.update(false, {0, 0x02000000});
    }
    throw std::runtime_error("Missing pause row");
  };
  choose("Weapon class (preview)");
  pause.update(false, {0, 0x02000000});
  pause.update(false, {0x1010, 0});
  require(pause.requested_weapon() == 1 && pause.paused(),
          "Weapon request waits for transaction");
  pause.complete_weapon(false);
  require(pause.paused() && !pause.requested_weapon(),
          "Failed weapon remains paused");
  choose("Animation review");
  choose("Face");
  pause.update(false, {0, 0x02000000});
  pause.update(false, {0, 0x02000000});
  pause.update(false, {0x1010, 0});
  require(pause.requested_review() &&
              pause.requested_review()->kind ==
                  bw::game::AnimationReviewKind::facial &&
              pause.requested_review()->index == 3,
          "Face menu excludes absent face two without renumbering payload");
  std::string rejection = "Unavailable preview";
  pause.complete_review({false, rejection});
  rejection.assign("changed");
  require(
      pause.paused() && pause.presentation().message == "Unavailable preview",
      "Failure owns message and preserves pause");
  pause.update(false, {0x1010, 0});
  pause.complete_review({true, "Accepted"});
  require(
      !pause.paused() && !pause.requested_review() && !pause.update(false, {}),
      "Accepted preview resumes without repeating request");
  pause.update(true, {});
  choose("Animation review");
  choose("Body clips");
  require(
      pause.presentation().count == 1 && !pause.presentation().rows[0].enabled,
      "Empty catalog is visible but unavailable");
  pause.update(false, {0x1010, 0});
  require(!pause.requested_review(), "Disabled catalog never dispatches");
  pause.update(false, {0x2020, 0});
  choose("Scripted jump");
  pause.update(false, {0, 0x02000000});
  pause.update(false, {0, 0x02000000});
  pause.update(false, {0x1010, 0});
  require(pause.requested_review() && pause.requested_review()->index == 8,
          "Jump selector retains authored sparse value");
  require(
      pause.update(true, {}) && !pause.paused() && !pause.requested_review(),
      "Pause toggle cancels a pending request and consumes frame");
}
void navigation_and_new_game() {
  bw::game::Profile profile;
  bw::game::MenuFlow flow(profile);
  flow.start();
  require(flow.presentation().count == 6, "All original main rows exist");
  down(flow);
  require(flow.presentation().selected == 1,
          "Steam continue row remains enabled");
  down(flow);
  require(flow.presentation().selected == 3,
          "Navigation skips unavailable stage select");
  down(flow);
  require(flow.presentation().selected == 4 &&
              flow.presentation().rows.at(4).enabled,
          "Fresh profiles can enter concept missions without game completion");
  down(flow);
  require(flow.presentation().selected == 5, "Quit follows concept missions");
  down(flow);
  accept(flow);
  require(flow.presentation().page == bw::game::MenuPage::difficulty &&
              flow.presentation().count == 3,
          "New game enters installed difficulty choices");
  accept(flow);
  require(flow.selection() && flow.selection()->original_status == 1 &&
              flow.selection()->stage == 1 && flow.selection()->difficulty == 1,
          "Normal new game publishes original status1 andstage1");
  cancel(flow);
  require(flow.selection() && flow.selection()->original_status == 1,
          "Published selection is immutable until restart");

  // Concept selection leaves campaign progression and difficulty gates intact.
  flow.start();
  require(!flow.presentation().rows.at(2).enabled,
          "Concept access does not unlock campaign stage select");
  down(flow);
  down(flow);
  down(flow);
  accept(flow);
  require(flow.presentation().page == bw::game::MenuPage::extra &&
              flow.presentation().count == 18 &&
              flow.presentation().rows.at(1).compatibility_id == 1 &&
              flow.presentation().rows.at(1).label == "One Shot Kill" &&
              flow.presentation().rows.at(6).compatibility_id == 16,
          "Fresh profile exposes only the original supplied mission groups");
  down(flow);
  accept(flow);
  require(flow.presentation().page == bw::game::MenuPage::difficulty &&
              flow.presentation().count == 3,
          "Concept access does not unlock Chaos or Hell");
  accept(flow);
  require(
      flow.selection() &&
          flow.selection()->action == bw::game::MenuAction::concept_mission &&
          flow.selection()->stage == 1 &&
          flow.selection()->original_status == 5,
      "One Shot Kill preserves internal result5 for the native status "
      "accessor");
}
void continue_and_back() {
  bw::game::Profile profile;
  bw::game::MenuFlow flow(profile);
  flow.start();
  down(flow);
  accept(flow);
  require(flow.presentation().page == bw::game::MenuPage::confirm &&
              !flow.selection(),
          "Absent checkpoint cannot produce a continue handoff");
  cancel(flow);
  cancel(flow);
  require(flow.selection() && flow.selection()->original_status == 7,
          "Back from main returns to original title route");
}
class RecordStorage final : public bw::platform::SaveStorage {
 public:
  explicit RecordStorage(const bw::game::Profile& profile)
      : record(profile.record_bytes().begin(), profile.record_bytes().end()) {}
  bw::platform::SaveRead read(std::string_view name, std::size_t) override {
    if (name == "BulletWitchRecord") {
      return {bw::platform::SaveStatus::ok, record};
    }
    return {bw::platform::SaveStatus::missing, {}};
  }
  bw::platform::SaveStatus write(std::string_view,
                                 std::span<const std::byte>) override {
    return bw::platform::SaveStatus::ok;
  }
  std::vector<std::byte> record;
};
void persisted_availability() {
  bw::game::Profile profile;
  RecordStorage storage(profile);
  storage.record.at(0x7c + 5) = std::byte{1};
  storage.record.at(0x3c + 63) = std::byte{1};
  require(profile.load(storage).status == bw::platform::SaveStatus::ok,
          "Valid retained record loads");
  bw::game::MenuUnlocks unlocks;
  unlocks.stage_select = true;
  bw::game::MenuFlow flow(profile, unlocks);
  flow.start();
  down(flow);
  down(flow);
  down(flow);
  accept(flow);
  down(flow);
  down(flow);
  accept(flow);
  require(flow.presentation().count == 6 &&
              flow.presentation().rows.at(5).compatibility_id == 5,
          "Steam refresh enables all five costumes without achievement gates");
  for (unsigned index = 0; index < 5; ++index) {
    down(flow);
  }
  accept(flow);
  require(profile.costume() == 5,
          "Costume selection persists actual compatibility ID");
  cancel(flow);
  down(flow);
  down(flow);
  down(flow);
  down(flow);
  accept(flow);
  require(flow.presentation().count == 18 &&
              flow.presentation().rows.at(6).compatibility_id == 16 &&
              flow.presentation().rows.at(17).compatibility_id == 37,
          "Steam refresh supplies exact three six-mission groups");
  accept(flow);
  accept(flow);
  require(flow.selection() && flow.selection()->stage == 0 &&
              flow.selection()->original_status == 5,
          "Extra handoff retains native5 before the status accessor");
}
void options_and_stage() {
  bw::game::Profile profile;
  bw::game::MenuUnlocks unlocks;
  unlocks.stage_select = true;
  bw::game::MenuFlow flow(profile, unlocks);
  flow.start();
  down(flow);
  down(flow);
  down(flow);
  accept(flow);
  require(flow.presentation().page == bw::game::MenuPage::options,
          "Options is a child screen");
  flow.update({0x04000000, 0});
  require(profile.music_volume() == 240 && flow.settings_changed(),
          "Volume changes by recovered16-unitstep");
  flow.acknowledge_settings();
  require(!flow.settings_changed(),
          "Persistence acknowledgement clears notification");
  down(flow);
  down(flow);
  accept(flow);
  require(flow.presentation().page == bw::game::MenuPage::costume,
          "Costumes are reached through options");
  accept(flow);
  require(flow.presentation().page == bw::game::MenuPage::options &&
              profile.costume() == 0,
          "Confirming original costume updates profile andreturns options");
  cancel(flow);
  down(flow);
  down(flow);
  accept(flow);
  require(flow.presentation().count == 6,
          "All six campaign stages are selectable");
  down(flow);
  down(flow);
  accept(flow);
  require(flow.presentation().page == bw::game::MenuPage::difficulty,
          "Stage selection precedes difficulty");
  accept(flow);
  require(flow.selection() && flow.selection()->original_status == 3 &&
              flow.selection()->stage == 3,
          "Stage selection keeps original status3 andchosenstage");
}
}  // namespace
void test_menu_flow() {
  bw::game::PauseFlow pause(5);
  require(!pause.update(false, {}), "Gameplay frame stays unconsumed");
  require(pause.update(true, {0x1010, 0}) && pause.paused(),
          "Pause edge consumes confirm without immediately resuming");
  pause.update(false, {0, 0x02000000});
  pause.update(false, {0x1010, 0});
  require(pause.presentation().selected == 5,
          "Costume page selects live costume");
  pause.update(false, {0, 0x02000000});
  pause.update(false, {0x1010, 0});
  require(pause.requested_costume() == 0, "Costume navigation wraps");
  pause.complete_costume(false);
  require(pause.paused() && !pause.requested_costume(),
          "Failure preserves pause");
  require(pause.update(false, {0x2020, 0}) && !pause.paused(),
          "Resume consumes the whole input frame");
  require(!pause.update(false, {}), "Next frame can advance gameplay");
  pause_review_transactions();
  navigation_and_new_game();
  continue_and_back();
  options_and_stage();
  persisted_availability();
}
