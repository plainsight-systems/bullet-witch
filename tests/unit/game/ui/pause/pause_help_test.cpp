// In-process Help contracts: source Record availability/read bits, bounded
// source navigation and scroll, supplied-time transitions, atomic rejection.
// Fixtures use public storage/load contracts and do not add production mutation
// paths. Core I.1/C.9 and performance MEM.9 (guidelines/memory/
// MEM.9-embedded-allocate-at-init.md), both phases consulted.
#include "game/ui/pause/pause_help.hpp"

#include <algorithm>
#include <limits>
#include <map>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

namespace {
constexpr std::uint32_t kConfirm = 0x1010, kCancel = 0x2020, kDown = 0x02000000,
                        kPageDown = 0x20000000, kPageUp = 0x10000000;
constexpr double kFrame = 1. / 60;
void require(bool condition, const char* reason) {
  if (!condition) throw std::runtime_error(reason);
}
class Storage final : public bw::platform::SaveStorage {
 public:
  bw::platform::SaveRead read(std::string_view name, std::size_t) override {
    const auto found = files.find(std::string(name));
    if (found == files.end()) return {bw::platform::SaveStatus::missing, {}};
    return {bw::platform::SaveStatus::ok, found->second};
  }
  bw::platform::SaveStatus write(std::string_view name,
                                 std::span<const std::byte> data) override {
    files[std::string(name)] = {data.begin(), data.end()};
    return bw::platform::SaveStatus::ok;
  }
  std::map<std::string, std::vector<std::byte>> files;
};
template <class T>
concept CanUnlock = requires(T value) { value.refresh_installed_content(); };
template <class T>
concept CanPurchase = requires(T value) { value.purchase({}); };
static_assert(!CanUnlock<bw::game::HelpControl>);
static_assert(!CanPurchase<bw::game::HelpControl>);
static_assert(!std::is_move_constructible_v<bw::game::PauseHelpFlow>);
std::array<unsigned, bw::game::help_topic_count> lines() {
  std::array<unsigned, bw::game::help_topic_count> counts{};
  counts.fill(16);
  return counts;
}
void settle(bw::game::PauseHelpFlow& flow) {
  for (unsigned frame = 0; frame < 120; ++frame) flow.update({}, kFrame);
}
void fresh_profile_and_read_marker() {
  Storage storage;
  bw::game::Profile profile;
  require(profile.load(storage).status == bw::platform::SaveStatus::ok,
          "load fresh fixture");
  require(profile.save(storage).status == bw::platform::SaveStatus::ok,
          "save fresh fixture");
  const auto before = storage.files.at("BulletWitchRecord");
  const auto control = profile.help_control();
  require(control.progress().available[0] &&
              std::ranges::count(control.progress().available, true) == 1,
          "original topic zero available without learning bits");
  bool rejected = false;
  try {
    control.mark_read(1);
    throw std::runtime_error("locked Help accepted");
  } catch (const std::invalid_argument&) {
    rejected = true;
  }
  require(rejected, "invalid Help operation rejected");
  require(!profile.dirty(), "unavailable Help rejects before Record mutation");
  bw::game::MissionHelpState help(control);
  bw::game::PauseHelpFlow flow(help);
  flow.start(lines());
  flow.update({kConfirm, 0}, .5);
  require(flow.presentation().phase == bw::game::PauseHelpPhase::opening &&
              !control.progress().read[0],
          "opening consumes input");
  settle(flow);
  flow.update({kConfirm, 0}, kFrame);
  require(flow.presentation().phase == bw::game::PauseHelpPhase::reading &&
              control.progress().read[0] && profile.dirty(),
          "reading marks admitted topic only");
  require(profile.save(storage).status == bw::platform::SaveStatus::ok,
          "persist Help read marker");
  auto expected = before;
  expected.at(0x24) = std::byte{1};
  require(storage.files.at("BulletWitchRecord") == expected,
          "read marker preserves every unrelated Record byte");
  control.mark_read(0);
  require(!profile.dirty(), "repeat read does not dirty unchanged Record");
  bw::game::Profile loaded;
  require(loaded.load(storage).status == bw::platform::SaveStatus::ok &&
              loaded.help_control().progress().read[0],
          "read marker survives existing persistence path");
  flow.update({}, kFrame, -10000);
  settle(flow);
  require(flow.presentation().scroll_pixels > 159.9F &&
              flow.presentation().scroll_pixels <= 160,
          "source scroll range is (16-12)*40");
  flow.update({kCancel, 0}, kFrame);
  require(flow.presentation().phase == bw::game::PauseHelpPhase::topics,
          "Back returns from reading to topic list");
  flow.update({kConfirm, 0}, kFrame);
  require(flow.presentation().scroll_pixels == 0,
          "reentering reading resets original paragraph response");
  flow.update({kCancel, 0}, kFrame);
  flow.update({kCancel, 0}, kFrame);
  require(!flow.completed(), "closing retains original animation");
  settle(flow);
  require(flow.completed(), "source Help closing completes");
}
void learned_topics_and_navigation() {
  Storage storage;
  bw::game::Profile profile;
  require(profile.load(storage).status == bw::platform::SaveStatus::ok,
          "load fresh fixture");
  require(profile.save(storage).status == bw::platform::SaveStatus::ok,
          "save source fixture");
  auto& bytes = storage.files.at("BulletWitchRecord");
  bytes.at(0x1c) = std::byte{0xff};
  bytes.at(0x20) = std::byte{1};
  require(profile.load(storage).status == bw::platform::SaveStatus::ok,
          "load original learned bits");
  bw::game::MissionHelpState help(profile.help_control());
  bw::game::PauseHelpFlow flow(help);
  flow.start(lines());
  settle(flow);
  require(flow.presentation().count == 9 && flow.presentation().topics[8] == 32,
          "learned mask second word retains topic32");
  flow.update({0, kPageDown}, kFrame);
  require(flow.presentation().selected == 5,
          "native page step selects five rows");
  flow.update({0, kPageDown}, kFrame);
  require(flow.presentation().selected == 8, "page step clamps final topic");
  flow.update({0, kDown}, kFrame);
  require(flow.presentation().selected == 8, "final topic does not wrap");
  flow.update({kConfirm, 0}, kFrame);
  require(profile.help_control().progress().read[32],
          "read marker uses topic ID not filtered row");
  flow.update({kCancel, 0}, kFrame);
  flow.update({0, kPageUp}, kFrame);
  require(flow.presentation().selected == 3,
          "page back uses filtered list position");
  const auto before = flow.presentation();
  bool rejected = false;
  try {
    flow.update({}, std::numeric_limits<double>::quiet_NaN());
    throw std::runtime_error("NaN accepted");
  } catch (const std::invalid_argument&) {
    rejected = true;
  }
  require(rejected, "invalid Help time rejected");
  require(flow.presentation().selected == before.selected &&
              flow.presentation().elapsed == before.elapsed,
          "invalid presentation time cannot mutate flow");
  rejected = false;
  auto bad = lines();
  bad[0] = 0;
  try {
    flow.start(bad);
    throw std::runtime_error("invalid content accepted");
  } catch (const std::invalid_argument&) {
    rejected = true;
  }
  require(rejected, "invalid Help content rejected");
  require(flow.presentation().selected == before.selected,
          "invalid content rejected before reset");
}
void completed_concept_help_context() {
  Storage storage;
  bw::game::Profile profile;
  require(profile.load(storage).status == bw::platform::SaveStatus::ok &&
              profile.save(storage).status == bw::platform::SaveStatus::ok,
          "prepare fresh permanent Help fixture");
  const auto original_record = storage.files.at("BulletWitchRecord");
  bw::game::MissionPlayerState player(
      {profile.progression(),
       {},
       true,
       bw::game::ConceptProfile::development_completed});
  bw::game::MissionHelpState help(profile.help_control(), player);
  require(std::ranges::count(help.progress().available, true) == 32 &&
              !help.progress().available[18],
          "completed concept inherits source-granted topics without inventing "
          "topic18");
  require(std::ranges::count(profile.help_control().progress().available,
                             true) == 1 &&
              !profile.dirty(),
          "effective completed Help leaves permanent learning untouched");
  bw::game::PauseHelpFlow flow(help);
  flow.start(lines());
  settle(flow);
  require(flow.presentation().count == 32,
          "completed concept exposes the original topic list");
  flow.update({0, kDown}, kFrame);
  flow.update({kConfirm, 0}, kFrame);
  require(flow.presentation().phase == bw::game::PauseHelpPhase::reading &&
              help.progress().read[1] &&
              !profile.help_control().progress().read[1] && !profile.dirty(),
          "reading an inherited topic changes only mission read state");
  flow.update({kCancel, 0}, kFrame);
  require(flow.presentation().phase == bw::game::PauseHelpPhase::topics &&
              flow.presentation().selected == 1,
          "original Back returns from a paragraph to the navigable topic list");
  flow.update({0, kDown}, kFrame);
  require(flow.presentation().selected == 2,
          "topic navigation continues after reading");
  require(profile.save(storage).status == bw::platform::SaveStatus::ok &&
              storage.files.at("BulletWitchRecord") == original_record,
          "inherited-topic reads do not alter the saved Record");
  // Existing permanent learning remains live, rather than being frozen at
  // session construction. Saved acquisition uses the ordinary owner command.
  (void)profile.award_result({0, 0, bw::game::MissionRewardMode::campaign});
  require(profile.purchase({bw::game::UpgradeCategory::spell, 0}) ==
              bw::game::PurchaseResult::purchased,
          "original spell acquisition supplies permanent Help learning");
  help.mark_read(9);
  require(profile.help_control().progress().read[9],
          "new permanent learning takes read ownership during the existing "
          "session");
  bw::game::MissionPlayerState saved_player(
      {profile.progression(), {}, true, bw::game::ConceptProfile::saved});
  bw::game::MissionHelpState saved_help(profile.help_control(), saved_player);
  require(std::ranges::count(saved_help.progress().available, true) == 2 &&
              !saved_help.progress().available[1],
          "saved concept mode retains exact permanent eligibility");
  bw::game::MissionHelpState next_help(profile.help_control(), player);
  require(!next_help.progress().read[1],
          "inherited read state ends with its mission owner");
}
void logical_help_icons() {
  auto settings = bw::game::controller_default_settings();
  const auto keyboard = bw::game::help_input_icons(settings, false);
  const auto pad = bw::game::help_input_icons(settings, true);
  const auto action = [](const auto& icons,
                         bw::game::HelpInputAction id) -> const auto& {
    return icons.actions.at(static_cast<unsigned>(id));
  };
  const auto& move = action(keyboard, bw::game::HelpInputAction::movement);
  require(move.count == 4 && move.icons[0] == 16 && move.icons[1] == 30 &&
              move.icons[2] == 29 && move.icons[3] == 31,
          "logical movement resolves original W S A D keyboard artwork");
  require(
      action(keyboard, bw::game::HelpInputAction::camera).icons[0] == 99 &&
          action(keyboard, bw::game::HelpInputAction::fire).icons[0] == 94 &&
          action(keyboard, bw::game::HelpInputAction::evade).icons[0] == 54,
      "keyboard camera fire and evade follow source action table");
  require(action(pad, bw::game::HelpInputAction::movement).icons[0] == 100 &&
              action(pad, bw::game::HelpInputAction::fire).icons[0] == 131 &&
              action(pad, bw::game::HelpInputAction::reload).icons[0] == 122,
          "original default pad action icons retained");
  settings.keys[4] = 0x21;
  settings.controller_identity = bw::game::kSourceXinputIdentity;
  settings.buttons = bw::game::kSourceXinputButtons;
  bw::game::remap_controller_button(settings, 0, 12);
  require(action(bw::game::help_input_icons(settings, false),
                 bw::game::HelpInputAction::fire)
                      .icons[0] == 32 &&
              action(bw::game::help_input_icons(settings, true),
                     bw::game::HelpInputAction::fire)
                      .icons[0] == 120,
          "saved keyboard and pad remaps change only copied logical icon "
          "observations");
  settings.keys[0] = settings.keys[1];
  require(action(bw::game::help_input_icons(settings, false),
                 bw::game::HelpInputAction::movement)
                  .count == 3,
          "source keyboard action run deduplicates identical bindings");
  settings.keys[4] = 0x100;
  require(action(bw::game::help_input_icons(settings, false),
                 bw::game::HelpInputAction::fire)
                  .count == 0,
          "unassigned source key omits logical artwork");
  auto bad = keyboard;
  bad.actions[0].count = 5;
  require(bw::game::valid_help_input_icons(keyboard) &&
              bw::game::valid_help_input_icons(pad) &&
              !bw::game::valid_help_input_icons(bad),
          "invalid copied icon runs rejected");
}
void original_system_flags() {
  Storage storage;
  bw::game::Profile profile;
  require(profile.load(storage).status == bw::platform::SaveStatus::ok &&
              profile.save(storage).status == bw::platform::SaveStatus::ok,
          "prepare original System flag fixture");
  struct Flags {
    unsigned byte;
    bool subtitles, autosave;
  };
  constexpr std::array<Flags, 4> kCases{
      {{2, false, false}, {3, true, false}, {4, false, true}, {5, true, true}}};
  for (const auto& test : kCases) {
    storage.files.at("BulletWitchSystem").at(2) =
        static_cast<std::byte>(test.byte);
    require(
        profile.load(storage).status == bw::platform::SaveStatus::ok &&
            profile.subtitles_enabled() == test.subtitles &&
            profile.autosave_enabled() == test.autosave,
        "System subtitles bit zero and autosave bit two remain independent");
  }
}
}  // namespace
void pause_help_contracts() {
  fresh_profile_and_read_marker();
  learned_topics_and_navigation();
  original_system_flags();
  completed_concept_help_context();
  logical_help_icons();
}
