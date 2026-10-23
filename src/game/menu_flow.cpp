// G2: bounded native game menu selection reconstruction.
// Inputs/ownership: MenuFlow's header contract; Profile supplies saved
// decisions and injected unlocks supply achievement availability. All
// state is single-threaded. Failures from profile validation propagate to the
// session. Algorithm: construct page rows, wrap/skip navigation, dispatch one
// edge and publish an immutable original-status selection. Costume selection
// and options update their actual profile fields; campaign difficulty updates
// live record0. No mission or checkpoint is fabricated. Extra status5 is
// preserved despite the original root only recognizing6 (documented unresolved
// route mismatch). Costs: bounded64-row work, constexpr labels and no update
// heap allocation. MCP design: Enum.3 (#renum-class); implementation SL.con.3
// (#rsl-bounds) checks indices via at(); performance COPY.7
// (guidelines/copy-move/COPY.7-eliminate- hidden-copies.md) borrows profile and
// copies only small presentation values.
#include "menu_flow.hpp"

#include <algorithm>
#include <array>
#include <string_view>

#include "profile.hpp"

namespace bw::game {
namespace {
constexpr std::array<std::string_view, 5> difficulties{"Easy", "Normal", "Hard",
                                                       "Chaos", "Hell"};
constexpr std::array<std::string_view, 6> stages{
    "Stage 1", "Stage 2", "Stage 3", "Stage 4", "Stage 5", "Stage 6"};
constexpr std::array<std::string_view, 12> costumes{
    "Original",  "Costume 1", "Costume 2",  "Costume 3",
    "Costume 4", "Costume 5", "Costume 6",  "Costume 7",
    "Costume 8", "Costume 9", "Costume 10", "Costume 11"};
constexpr std::array<std::string_view, 17> volumes{
    "0", "1",  "2",  "3",  "4",  "5",  "6",  "7", "8",
    "9", "10", "11", "12", "13", "14", "15", "16"};
constexpr auto extras = [] {
  std::array<std::string_view, 64> result{};
  result.fill("Extra Mission");
  return result;
}();
constexpr unsigned volume_step = 16;
constexpr unsigned volume_steps = 16;
constexpr unsigned volume_maximum = 256;
constexpr std::size_t quit_row = 5;
constexpr unsigned extra_status = 5;
constexpr unsigned title_status = 7;
constexpr std::uint32_t up_mask = 0x01000000U;
constexpr std::uint32_t down_mask = 0x02000000U;
constexpr std::uint32_t left_mask = 0x04000000U;
constexpr std::uint32_t right_mask = 0x08000000U;
constexpr std::uint32_t confirm_mask = 0x1010;
constexpr std::uint32_t cancel_mask = 0x2020;
}  // namespace
MenuFlow::MenuFlow(Profile& profile, MenuUnlocks unlocks)
    : profile_(profile), unlocks_(unlocks) {}
void MenuFlow::start() {
  selection_.reset();
  settings_changed_ = false;
  stage_ = 1;
  difficulty_ = profile_.get().difficulty();
  costume_ = profile_.get().costume();
  populate(MenuPage::main);
}
void MenuFlow::add(std::string_view label, std::string_view value,
                   bool enabled) {
  presentation_.rows.at(presentation_.count) = {label, value, enabled};
  ++presentation_.count;
}
void MenuFlow::populate(MenuPage page) {
  presentation_ = {};
  presentation_.page = page;
  switch (page) {
    case MenuPage::main:
      presentation_.title = "Bullet Witch";
      add("New Game");
      // Steam's original 0x470e50 always returns true, even with no checkpoint.
      // The selection itself reports absent data rather than fabricating it.
      add("Continue");
      add("Stage Select", {}, unlocks_.stage_select);
      add("Options");
      add("Extra Missions", {}, unlocks_.stage_select);
      add("Quit");
      break;
    case MenuPage::difficulty:
      presentation_.title = "Difficulty";
      for (unsigned index = 0; index < difficulties.size(); ++index) {
        if (unlocks_.difficulties.at(index)) {
          row_values_.at(presentation_.count) = index;
          if (index == difficulty_) {
            presentation_.selected = presentation_.count;
          }
          add(difficulties.at(index));
        }
      }
      break;
    case MenuPage::stage:
      presentation_.title = "Stage Select";
      for (unsigned index = 0; index < stages.size(); ++index) {
        row_values_.at(presentation_.count) = index + 1;
        add(stages.at(index));
      }
      break;
    case MenuPage::extra:
      profile_.get().refresh_installed_content();
      settings_changed_ = true;
      presentation_.title = "Extra Missions";
      for (unsigned index = 0; index < extras.size(); ++index) {
        if (profile_.get().extra_available(index)) {
          row_values_.at(presentation_.count) = index;
          add(extras.at(index));
          presentation_.rows.at(presentation_.count - 1).compatibility_id =
              index;
        }
      }
      if (presentation_.count == 0) {
        presentation_.message = "No extra missions are available.";
      }
      break;
    case MenuPage::costume:
      profile_.get().refresh_installed_content();
      settings_changed_ = true;
      presentation_.title = "Costume";
      for (unsigned index = 0; index < costumes.size(); ++index) {
        if (index == 0 || profile_.get().costume_available(index)) {
          row_values_.at(presentation_.count) = index;
          if (index == costume_) {
            presentation_.selected = presentation_.count;
          }
          add(costumes.at(index));
          presentation_.rows.at(presentation_.count - 1).compatibility_id =
              index;
        }
      }
      break;
    case MenuPage::options:
      presentation_.title = "Options";
      add("Music Volume",
          volumes.at(std::min(profile_.get().music_volume() / volume_step,
                              volume_steps)));
      add("Effects Volume",
          volumes.at(std::min(profile_.get().effects_volume() / volume_step,
                              volume_steps)));
      add("Costume", costumes.at(costume_));
      add("Back");
      break;
    case MenuPage::confirm:
      presentation_.title = "Continue";
      presentation_.message = "No saved checkpoint is available.";
      add("Back");
      break;
    case MenuPage::complete:
      break;
  }
}
void MenuFlow::navigate(int direction) {
  const auto count = presentation_.count;
  if (count == 0 || direction == 0) {
    return;
  }
  for (std::size_t attempts = 0; attempts < count; ++attempts) {
    if (direction < 0) {
      presentation_.selected =
          presentation_.selected == 0 ? count - 1 : presentation_.selected - 1;
    } else {
      presentation_.selected = (presentation_.selected + 1) % count;
    }
    if (presentation_.rows.at(presentation_.selected).enabled) {
      return;
    }
  }
}
void MenuFlow::finish(MenuAction action, unsigned original_status) {
  selection_ =
      MenuSelection{action, stage_, difficulty_, costume_, original_status};
  populate(MenuPage::complete);
}
void MenuFlow::accept() {
  if (presentation_.count == 0 ||
      !presentation_.rows.at(presentation_.selected).enabled) {
    return;
  }
  const auto index = presentation_.selected;
  switch (presentation_.page) {
    case MenuPage::main:
      switch (index) {
        case 0:
          pending_ = MenuAction::new_game;
          stage_ = 1;
          previous_ = MenuPage::main;
          populate(MenuPage::difficulty);
          break;
        case 1:
          if (profile_.get().has_checkpoint()) {
            stage_ = profile_.get().checkpoint_stage();
            difficulty_ = profile_.get().checkpoint_difficulty();
            costume_ = profile_.get().costume();
            finish(MenuAction::continue_game, 2);
          } else {
            populate(MenuPage::confirm);
          }
          break;
        case 2:
          pending_ = MenuAction::stage_select;
          populate(MenuPage::stage);
          break;
        case 3:
          populate(MenuPage::options);
          break;
        case 4:
          pending_ = MenuAction::concept_mission;
          populate(MenuPage::extra);
          break;
        case quit_row:
          finish(MenuAction::quit, 0);
          break;
        default:
          break;
      }
      break;
    case MenuPage::stage:
    case MenuPage::extra:
      stage_ = row_values_.at(index);
      previous_ = presentation_.page;
      populate(MenuPage::difficulty);
      break;
    case MenuPage::difficulty:
      difficulty_ = row_values_.at(index);
      profile_.get().set_current_difficulty(difficulty_);
      settings_changed_ = true;
      finish(pending_, pending_ == MenuAction::new_game       ? 1U
                       : pending_ == MenuAction::stage_select ? 3U
                                                              : extra_status);
      break;
    case MenuPage::costume:
      costume_ = row_values_.at(index);
      profile_.get().set_costume(costume_);
      settings_changed_ = true;
      populate(MenuPage::options);
      break;
    case MenuPage::options:
      if (index == 2) {
        populate(MenuPage::costume);
      } else if (index == 3) {
        populate(MenuPage::main);
      }
      break;
    case MenuPage::confirm:
      populate(MenuPage::main);
      break;
    case MenuPage::complete:
      break;
  }
}
void MenuFlow::adjust(int direction) {
  if (presentation_.page != MenuPage::options || presentation_.selected > 1 ||
      direction == 0) {
    return;
  }
  const auto index = presentation_.selected;
  auto& profile = profile_.get();
  const unsigned current =
      index == 0 ? profile.music_volume() : profile.effects_volume();
  const unsigned next =
      direction < 0 ? (current < volume_step ? 0 : current - volume_step)
                    : std::min(current + volume_step, volume_maximum);
  if (next != current) {
    if (index == 0) {
      profile.set_music_volume(next);
    } else {
      profile.set_effects_volume(next);
    }
    settings_changed_ = true;
    populate(MenuPage::options);
    presentation_.selected = index;
  }
}
void MenuFlow::cancel() {
  switch (presentation_.page) {
    case MenuPage::main:
      finish(MenuAction::return_to_title, title_status);
      break;
    case MenuPage::difficulty:
      populate(previous_);
      break;
    case MenuPage::costume:
      populate(MenuPage::options);
      break;
    case MenuPage::complete:
      break;
    default:
      populate(MenuPage::main);
      break;
  }
}
void MenuFlow::update(MenuInput input) {
  if (selection_) {
    return;
  }
  if ((input.pressed & cancel_mask) != 0) {
    cancel();
  } else if ((input.pressed & confirm_mask) != 0) {
    accept();
  } else if ((input.repeated & up_mask) != 0) {
    navigate(-1);
  } else if ((input.repeated & down_mask) != 0) {
    navigate(1);
  } else if ((input.pressed & left_mask) != 0) {
    adjust(-1);
  } else if ((input.pressed & right_mask) != 0) {
    adjust(1);
  }
}
}  // namespace bw::game
