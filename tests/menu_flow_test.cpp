// V1: public menu navigation, persisted choices and immutable handoff
// contracts. In-memory profile only; no storage processes, rendering or game
// assets. Algorithm: navigate pages using actual action masks, verify state and
// results. Bounded values and fixed action sequences. MCP
// design/implementation: Enum.3
// (#renum-class), SL.con.3 (#rsl-bounds), performance COPY.7
// (guidelines/copy-move/COPY.7-eliminate-hidden-copies.md) use explicit values
// and references. This does not establish visual fidelity or gameplay
// execution.
#include "game/menu_flow.hpp"

#include <stdexcept>
#include <vector>

#include "game/profile.hpp"

namespace {
void require(bool value, const char* message) {
  if (!value) {
    throw std::runtime_error(message);
  }
}
void down(bw::game::MenuFlow& flow) { flow.update({0, 0x02000000}); }
void accept(bw::game::MenuFlow& flow) { flow.update({0x1010, 0}); }
void cancel(bw::game::MenuFlow& flow) { flow.update({0x2020, 0}); }
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
  require(flow.presentation().selected == 5,
          "Navigation skips unavailable extra entry");
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
          "Extra handoff preserves observed native5 instead of guessing Lua6");
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
  navigation_and_new_game();
  continue_and_back();
  options_and_stage();
  persisted_availability();
}
