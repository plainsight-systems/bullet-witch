// Localized-text repair: design I.1 and implementation E.4, with MEM.9
// retrieved in both phases. Policy publishes content IDs; the surface owns
// prepared purchased text/art. No English fallback or frame-time decoding.
// Main-menu feedback implementation: original supplied-time tracks and copied
// presentation values; prepared localized text and resident textures. Core E.4
// (#re-design-invariants), performance MEM.9 (guidelines/memory/
// MEM.9-embedded-allocate-at-init.md) read during implementation require
// checked snapshots and fixed frame storage. Source proof:
// frontend-feedback.md. G2: bounded native game menu selection reconstruction.
// Inputs/ownership: MenuFlow's header contract; Profile supplies saved
// decisions and injected unlocks supply campaign achievement availability.
// Concept-menu access intentionally omits the original completion gate; child
// mission availability still comes from the original content refresh. All
// state is single-threaded. Failures from profile validation propagate to the
// session. Algorithm: construct page rows, wrap/skip navigation, dispatch one
// edge and publish an immutable original-status selection. Costume selection
// and options update their actual profile fields; campaign difficulty updates
// live record0. No mission or checkpoint is fabricated. Extra status5 is
// preserved internally; StartupSession applies native accessor0x49a560
// mapping5 to Lua6 at the script boundary. Costs: bounded64-row work, constexpr
// labels and no update heap allocation. MCP design: Enum.3 (#renum-class);
// implementation SL.con.3
// (#rsl-bounds) checks indices via at(); performance COPY.7
// (guidelines/copy-move/COPY.7-eliminate- hidden-copies.md) borrows profile and
// copies only small presentation values. Concept-menu change design and
// implementation consult I.1 (#ri-explicit) for the documented independent
// policy and COPY.7 for retaining small inputs and borrowed profile storage.
#include "menu_flow.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>
#include <string_view>

#include "profile.hpp"

namespace bw::game {
namespace {
constexpr std::size_t kStageCount = 6, kCostumeChoices = 12, kExtraChoices = 64;
constexpr std::size_t quit_row = 5;
constexpr unsigned extra_status = 5;
constexpr unsigned title_status = 7;
constexpr std::uint32_t up_mask = 0x01000000U;
constexpr std::uint32_t down_mask = 0x02000000U;
constexpr std::uint32_t confirm_mask = 0x1010;
constexpr std::uint32_t cancel_mask = 0x2020;
}  // namespace
MenuFlow::MenuFlow(Profile& profile, MenuUnlocks unlocks)
    : profile_(profile), unlocks_(unlocks) {}
void MenuFlow::start() {
  selection_.reset();
  settings_changed_ = false;
  costume_complete_ = costume_changed_ = false;
  save_requested_ = save_pending_ = false;
  difficulty_pending_ = false;
  stage_backdrop_ = {};
  stage_ = 1;
  difficulty_ = profile_.get().difficulty();
  costume_ = profile_.get().costume();
  main_selection_ = 0;
  pending_accept_ = false;
  options_return_ = false;
  child_fade_ = 0;
  child_reveal_ = false;
  departure_fade_seconds_ = 0;
  music_fade_.reset();
  feedback_ = {};
  scale_ = {};
  for (auto& value : scale_) value.value = 1;
  description_ = {};
  pulse_seconds_ = 0;
  sounds_ = {};
  constexpr float kPanelCueDelay = .3F;
  sounds_.add(MenuSound::panel, kPanelCueDelay);
  populate(MenuPage::main);
  begin_main_motion(MainMenuMotion::entering);
}
void MenuFlow::add(std::string_view label, std::string_view value,
                   bool enabled) {
  presentation_.rows.at(presentation_.count) = {label, value, enabled};
  ++presentation_.count;
}
void MenuFlow::populate(MenuPage page) {
  // Rows describe policy choices; asset lookup and drawing belong to the
  // surface.
  const auto old_page = presentation_.page;
  const auto prompts = presentation_.prompts;
  presentation_ = {};
  presentation_.prompts = prompts;
  presentation_.page = page;
  if (page == MenuPage::main) {
    presentation_.selected = main_selection_;
    if (old_page != MenuPage::main) {
      begin_main_motion(MainMenuMotion::returning);
    }
    presentation_.main = feedback_;
  }
  switch (page) {
    case MenuPage::main:

      add({});
      // Steam's original 0x470e50 always returns true, even with no checkpoint.
      // The selection itself reports absent data rather than fabricating it.
      add({});
      add({}, {}, unlocks_.stage_select);
      add({});
      // Intentional policy departure: concept access needs no campaign clear.
      // The child page still enumerates only content marked available.
      add({});
      add({});
      break;
    case MenuPage::difficulty:

      for (unsigned index = 0; index < menu_difficulty_count; ++index) {
        if (unlocks_.difficulties.at(index)) {
          row_values_.at(presentation_.count) = index;
          if (index == difficulty_) {
            presentation_.selected = presentation_.count;
          }
          add({});
          presentation_.rows.at(presentation_.count - 1).compatibility_id =
              index;
        }
      }
      break;
    case MenuPage::stage:

      for (unsigned index = 0; index < kStageCount; ++index) {
        row_values_.at(presentation_.count) = index + 1;
        add({});
        presentation_.rows.at(presentation_.count - 1).compatibility_id =
            index + 1;
        const auto best = profile_.get().mission_best(false, index + 1);
        presentation_.rows.at(presentation_.count - 1).best_rank = best.rank;
        presentation_.rows.at(presentation_.count - 1).best_score = best.score;
      }
      break;
    case MenuPage::extra:
      profile_.get().refresh_installed_content();
      settings_changed_ = true;

      for (unsigned index = 0; index < kExtraChoices; ++index) {
        if (profile_.get().extra_available(index)) {
          row_values_.at(presentation_.count) = index;
          add({});
          presentation_.rows.at(presentation_.count - 1).compatibility_id =
              index;
          const auto best = profile_.get().mission_best(true, index);
          presentation_.rows.at(presentation_.count - 1).best_rank = best.rank;
          presentation_.rows.at(presentation_.count - 1).best_score =
              best.score;
        }
      }
      if (presentation_.count == 0) {
        throw std::runtime_error(
            "Installed concept menu has no available rows");
      }
      break;
    case MenuPage::costume:
      profile_.get().refresh_installed_content();
      settings_changed_ = true;

      for (unsigned index = 0; index < kCostumeChoices; ++index) {
        if (index == 0 || profile_.get().costume_available(index)) {
          row_values_.at(presentation_.count) = index;
          add({});
          presentation_.rows.at(presentation_.count - 1).compatibility_id =
              index;
        }
      }
      break;
    case MenuPage::options:
      // Shared native Options owns every row and setting; this is a rendezvous.
      break;
    case MenuPage::confirm:

      add({});
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
  // Bound wraparound even when every row is disabled.
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
  constexpr float kReturnConceal = .5F, kContinueConceal = .25F,
                  kDifficultyConceal = 1.F, kMenuMusicFade = 1.F,
                  kMissionMusicFade = 3.F;
  if (action == MenuAction::return_to_title || action == MenuAction::quit) {
    departure_fade_seconds_ = kReturnConceal;
    music_fade_ = kMenuMusicFade;
  } else if (action == MenuAction::continue_game) {
    departure_fade_seconds_ = kContinueConceal;
    // Continue reports success after its loading task finishes concealment.
  } else {
    departure_fade_seconds_ = kDifficultyConceal;
    if (action == MenuAction::new_game) music_fade_ = kMissionMusicFade;
    // Continue/stage/concept stop music after their child returns;
    // the session already owns that post-conceal completion boundary.
  }
  selection_ =
      MenuSelection{action, stage_, difficulty_, costume_, original_status};
  // The original child keeps drawing its response curves during conceal.
  // Selection closes input; the session still supplies presentation seconds.
  if (presentation_.page == MenuPage::difficulty) {
    selector_confirmation_seconds_ = 0;
  }
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
          enter_child(MenuPage::difficulty);
          break;
        case 1:
          if (profile_.get().has_checkpoint()) {
            stage_ = profile_.get().checkpoint_stage();
            difficulty_ = profile_.get().checkpoint_difficulty();
            costume_ = profile_.get().costume();
            populate(MenuPage::confirm);
            start_dialog(FrontendDialog::continue_game);
          } else {
            populate(MenuPage::confirm);
            start_dialog(FrontendDialog::no_checkpoint);
          }
          break;
        case 2:
          pending_ = MenuAction::stage_select;
          enter_child(MenuPage::stage);
          break;
        case 3:
          populate(MenuPage::options);
          break;
        case 4:
          pending_ = MenuAction::concept_mission;
          enter_child(MenuPage::extra);
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
      stage_selection_ = index;
      stage_backdrop_ = presentation_.stage;
      previous_ = presentation_.page;
      difficulty_pending_ = true;
      child_transition_ = 0;
      break;
    case MenuPage::difficulty:
      difficulty_ = row_values_.at(index);
      profile_.get().set_current_difficulty(difficulty_);
      finish(pending_, pending_ == MenuAction::new_game       ? 1U
                       : pending_ == MenuAction::stage_select ? 3U
                                                              : extra_status);
      break;
    case MenuPage::costume:
      start_dialog(FrontendDialog::change_costume);
      break;
    case MenuPage::options:
    case MenuPage::confirm:
    case MenuPage::complete:
      break;
  }
}
void MenuFlow::cancel() {
  switch (presentation_.page) {
    case MenuPage::main:
      finish(MenuAction::return_to_title, title_status);
      break;
    case MenuPage::difficulty:
      begin_child_exit(false);
      break;
    case MenuPage::costume:
      if (!costume_changed_) {
        begin_child_exit(false);
      } else if (profile_.get().autosave_enabled()) {
        save_requested_ = save_pending_ = true;
      } else {
        start_dialog(FrontendDialog::save_costume);
      }
      break;
    case MenuPage::stage:
    case MenuPage::extra:
      begin_child_exit(false);
      break;
    case MenuPage::complete:
      break;
    default:
      populate(MenuPage::main);
      break;
  }
}
void MenuFlow::begin_main_motion(MainMenuMotion motion) {
  feedback_.motion = motion;
  feedback_.elapsed = 0;
  description_.value = 0;
  presentation_.main = feedback_;
}
void MenuFlow::animate(float seconds) {
  constexpr float kWobbleAmplitude = 9.496299743652344F;
  constexpr float kWobblePeriod = 3.530513048171997F;
  constexpr float kScaleDuration = .14F;
  constexpr float kDescriptionDuration = .17F;
  const auto target = feedback_.motion == MainMenuMotion::idle ? 1.F : 0.F;
  description_ = math::damped_response(description_,
                                       {target, kDescriptionDuration}, seconds);
  feedback_.description_alpha = description_.value;
  feedback_.confirmation_elapsed =
      std::min(1.F, feedback_.confirmation_elapsed + seconds);
  const auto wobble = math::periodic_sine_interpolate(
      math::PeriodicSineEndpoints<float>{-kWobbleAmplitude, kWobbleAmplitude},
      pulse_seconds_ / kWobblePeriod);
  for (std::size_t i = 0; i < scale_.size(); ++i) {
    scale_.at(i) =
        math::damped_response(scale_.at(i), {1.F, kScaleDuration}, seconds);
    feedback_.row_y.at(i) = i == main_selection_ ? wobble : 0.F;
    feedback_.row_scale.at(i) = scale_.at(i).value;
  }
  pulse_seconds_ = std::fmod(pulse_seconds_ + seconds, kWobblePeriod);
  feedback_.elapsed += seconds;
  if (presentation_.page == MenuPage::main) presentation_.main = feedback_;
}
void MenuFlow::update(MenuInput input, float seconds) {
  if (!std::isfinite(seconds) || seconds < 0) {
    throw std::invalid_argument("Invalid menu presentation interval");
  }
  sounds_ = {};
  if (selection_) {
    if (presentation_.page == MenuPage::difficulty) {
      update_child_animation(seconds);
    } else if (presentation_.page == MenuPage::main) {
      animate(seconds);
    }
    return;
  }
  if (presentation_.page == MenuPage::difficulty ||
      presentation_.page == MenuPage::stage ||
      presentation_.page == MenuPage::extra ||
      presentation_.page == MenuPage::costume ||
      presentation_.page == MenuPage::confirm) {
    update_child(input, seconds);
    return;
  }
  // External native Options owns its input and presentation; a closing call
  // returns through complete_options() rather than duplicate menu policy.
  if (presentation_.page == MenuPage::options) return;
  constexpr float kOptionsFadeSeconds = .25F;
  if (options_return_) {
    child_fade_ += seconds;
    presentation_.opacity =
        1 - std::clamp(child_fade_ / kOptionsFadeSeconds, 0.F, 1.F);
    if (child_fade_ >= kOptionsFadeSeconds) {
      options_return_ = false;
      cancel();
      presentation_.opacity = 0;
    }
    animate(seconds);
    return;
  }
  if (child_reveal_) {
    child_fade_ += seconds;
    presentation_.opacity =
        std::clamp(child_fade_ / kOptionsFadeSeconds, 0.F, 1.F);
    if (child_fade_ >= kOptionsFadeSeconds) child_reveal_ = false;
    animate(seconds);
    return;
  }
  if (presentation_.page == MenuPage::main &&
      feedback_.motion != MainMenuMotion::idle) {
    constexpr float kEntrySeconds = 1.8F, kChildSeconds = .85F;
    const auto duration =
        feedback_.motion == MainMenuMotion::returning
            ? (main_selection_ == 2 || main_selection_ == 4 ? 1.F
                                                            : kChildSeconds)
            : (feedback_.motion == MainMenuMotion::entering ||
                       main_selection_ != 3
                   ? kEntrySeconds
                   : kChildSeconds);
    const bool options_exit = pending_accept_ && main_selection_ == 3;
    if (options_exit) {
      presentation_.opacity =
          1 -
          std::clamp((feedback_.elapsed - kChildSeconds) / kOptionsFadeSeconds,
                     0.F, 1.F);
    } else if (feedback_.motion == MainMenuMotion::returning &&
               main_selection_ == 3) {
      presentation_.opacity =
          std::clamp(feedback_.elapsed / kOptionsFadeSeconds, 0.F, 1.F);
    }
    if (feedback_.elapsed >=
        duration + (options_exit ? kOptionsFadeSeconds : 0.F)) {
      feedback_.motion = MainMenuMotion::idle;
      if (pending_accept_) {
        pending_accept_ = false;
        accept();
        if (options_exit) {
          child_reveal_ = true;
          child_fade_ = 0;
          presentation_.opacity = 0;
        }
      }
    }
    animate(seconds);
    return;
  }
  const auto before_page = presentation_.page;
  const auto before_row = presentation_.selected;
  // Cues belong to accepted policy transitions, never to sprite submission.
  if ((input.pressed & cancel_mask) != 0) {
    sounds_.add(MenuSound::back);
    if (presentation_.page == MenuPage::options) {
      options_return_ = true;
      child_fade_ = 0;
    } else {
      cancel();
    }
  } else if ((input.pressed & confirm_mask) != 0) {
    if (presentation_.count != 0 &&
        presentation_.rows.at(presentation_.selected).enabled) {
      if (presentation_.page != MenuPage::options ||
          presentation_.selected >= 2) {
        sounds_.add(presentation_.page == MenuPage::difficulty
                        ? MenuSound::mission_start
                        : MenuSound::confirm);
      }
      if (presentation_.page == MenuPage::main &&
          presentation_.selected != quit_row) {
        main_selection_ = presentation_.selected;
        constexpr float kConfirmedScale = 1.1F;
        scale_.at(main_selection_).value = kConfirmedScale;
        feedback_.confirmation_elapsed = 0;
        pending_accept_ = true;
        begin_main_motion(MainMenuMotion::leaving);
      } else if (presentation_.page == MenuPage::options &&
                 presentation_.selected == 3) {
        options_return_ = true;
        child_fade_ = 0;
      } else {
        accept();
      }
    }
  } else if ((input.repeated & up_mask) != 0) {
    navigate(-1);
  } else if ((input.repeated & down_mask) != 0) {
    navigate(1);
  }
  if (before_page == presentation_.page &&
      before_row != presentation_.selected) {
    sounds_.add(MenuSound::cursor);
    if (presentation_.page == MenuPage::main) {
      main_selection_ = presentation_.selected;
      constexpr float kChangedDescription = .5F;
      description_.value = kChangedDescription;
    }
  }
  animate(seconds);
}
// Native child tasks retain their own supplied presentation-time state. The
// parent main task waits; no world clock or render-time mutation is involved.
void MenuFlow::enter_child(MenuPage page) {
  constexpr float kCostumeCueDelay = .1F, kStageCueDelay = .3F;
  populate(page);
  child_ = {};
  child_.current_costume = costume_;
  child_description_ = {};
  scroll_ = {};
  child_highlight_ = {};
  constexpr float kCursorInitial = -100;
  for (auto& position : selector_position_) position = {kCursorInitial, 0};
  selector_target_ = {};
  selector_opacity_ = selector_scale_ = {};
  selector_confirmation_seconds_ = 1;
  if (page == MenuPage::costume) {
    constexpr float kRowPitch = 172;
    scroll_.value = -static_cast<float>(presentation_.selected) * kRowPitch;
    child_highlight_.at(presentation_.selected).value = 1;
    sounds_.add(MenuSound::panel, kCostumeCueDelay);
  } else if (page == MenuPage::stage || page == MenuPage::extra) {
    sounds_.add(MenuSound::panel, kStageCueDelay);
  } else {
    for (auto& highlight : child_highlight_) highlight.value = 1;
  }
  presentation_.child = child_;
  if (page == MenuPage::difficulty && previous_ != MenuPage::main) {
    presentation_.stage = stage_backdrop_;
  }
  difficulty_pending_ = false;
  update_child_animation(0);
}
void MenuFlow::begin_costume_child() {
  if (presentation_.page != MenuPage::options) {
    throw std::logic_error("Costume child requires active native Options");
  }
  costume_complete_ = costume_changed_ = false;
  costume_ = profile_.get().costume();
  enter_child(MenuPage::costume);
}
void MenuFlow::acknowledge_costume_child() {
  if (!costume_complete_) {
    throw std::logic_error("Costume child is not complete");
  }
  costume_complete_ = false;
  populate(MenuPage::options);
}
bool MenuFlow::take_settings_save_request() {
  const auto requested = save_requested_;
  save_requested_ = false;
  return requested;
}
void MenuFlow::settings_save_completed(bool success) {
  if (!save_pending_) throw std::logic_error("Unrequested costume save result");
  save_pending_ = false;
  if (success) {
    sounds_.add(MenuSound::save);
    costume_changed_ = false;
  }
  start_dialog(!success ? FrontendDialog::save_failed
               : profile_.get().autosave_enabled()
                   ? FrontendDialog::costume_saved
                   : FrontendDialog::costume_saved_manual);
}
void MenuFlow::start_dialog(FrontendDialog content, bool yes) {
  const bool choices = content == FrontendDialog::change_costume ||
                       content == FrontendDialog::save_costume ||
                       content == FrontendDialog::continue_game;
  presentation_.dialog = content;
  dialog_.start(
      yes, choices ? MenuDialogMode::yes_no : MenuDialogMode::acknowledgment);
  presentation_.dialog_view = dialog_.presentation();
  const auto requests = dialog_.take_sounds();
  for (const auto request : std::span(requests.requests).first(requests.size)) {
    sounds_.add(request.sound, request.delay_seconds);
  }
}
void MenuFlow::update_dialog(MenuInput input, float seconds) {
  dialog_.update(input, seconds);
  const auto requests = dialog_.take_sounds();
  for (const auto request : std::span(requests.requests).first(requests.size)) {
    sounds_.add(request.sound, request.delay_seconds);
  }
  presentation_.dialog_view = dialog_.presentation();
  if (!presentation_.dialog_view.complete) return;
  const auto kind = presentation_.dialog;
  const auto yes = presentation_.dialog_view.yes;
  presentation_.dialog = FrontendDialog::none;
  switch (kind) {
    case FrontendDialog::change_costume:
      if (yes) {
        costume_ = row_values_.at(presentation_.selected);
        profile_.get().set_costume(costume_);
        child_.current_costume = costume_;
        costume_changed_ = true;
        start_dialog(FrontendDialog::costume_changed);
      }
      break;
    case FrontendDialog::save_costume:
      if (yes) {
        save_requested_ = save_pending_ = true;
      } else {
        begin_child_exit(false);
      }
      break;
    case FrontendDialog::costume_saved:
    case FrontendDialog::costume_saved_manual:
      begin_child_exit(false);
      break;
    case FrontendDialog::save_failed:
      start_dialog(FrontendDialog::save_costume);
      break;
    case FrontendDialog::no_checkpoint:
      populate(MenuPage::main);
      break;
    case FrontendDialog::continue_game:
      if (yes) {
        finish(MenuAction::continue_game, 2);
      } else {
        populate(MenuPage::main);
      }
      break;
    case FrontendDialog::profile_loading:
    case FrontendDialog::profile_creating:
    case FrontendDialog::profile_load_failed:
    case FrontendDialog::profile_overwrite:
    case FrontendDialog::profile_created:
    case FrontendDialog::profile_enable_autosave:
      throw std::logic_error("Profile-load dialog reached menu policy");
    case FrontendDialog::costume_changed:
    case FrontendDialog::none:
      break;
  }
}
void MenuFlow::begin_child_exit(bool accepted) {
  child_.motion = ChildMenuMotion::leaving;
  child_.elapsed = 0;
  child_exit_accept_ = accepted;
  if (presentation_.page == MenuPage::difficulty) {
    constexpr float kCancelShift = 80;
    selector_target_.at(0) -= kCancelShift;
  }
  presentation_.child = child_;
}
// The native difficulty selector has independent position, alpha and scale
// responses. Keep them in policy so drawing never advances mutable animation.
void MenuFlow::update_difficulty_selector(float seconds) {
  constexpr float kPositionResponse = .068F, kRevealResponse = .15F,
                  kExitPositionResponse = .25F, kExitAlphaResponse = .3F;
  const bool leaving = child_.motion == ChildMenuMotion::leaving;
  if (!leaving) {
    const auto id =
        presentation_.rows.at(presentation_.selected).compatibility_id;
    constexpr std::array<float, 2> kOffset{-128, -24};
    for (std::size_t axis = 0; axis < selector_target_.size(); ++axis) {
      selector_target_.at(axis) =
          difficulty_row_positions.at(id).at(axis) + kOffset.at(axis);
    }
  }
  for (std::size_t axis = 0; axis < selector_position_.size(); ++axis) {
    selector_position_.at(axis) = math::damped_response(
        selector_position_.at(axis),
        {selector_target_.at(axis),
         leaving ? kExitPositionResponse : kPositionResponse},
        seconds);
    child_.selector_position.at(axis) = selector_position_.at(axis).value;
  }
  const bool active = child_.motion == ChildMenuMotion::active;
  selector_opacity_ = math::damped_response(
      selector_opacity_,
      {active ? 1.F : 0.F, leaving ? kExitAlphaResponse : kRevealResponse},
      seconds);
  selector_scale_ = math::damped_response(
      selector_scale_,
      {child_.motion == ChildMenuMotion::entering ? 0.F : 1.F, kRevealResponse},
      seconds);
  child_.selector_opacity = selector_opacity_.value;
  child_.selector_scale = selector_scale_.value;
  if (selection_ && selector_confirmation_seconds_ < 1) {
    const auto echo = math::periodic_sine_interpolate(
        math::PeriodicSineEndpoints<float>{0, 1},
        selector_confirmation_seconds_);
    child_.selector_echo = echo * echo;
    selector_confirmation_seconds_ =
        std::min(1.F, selector_confirmation_seconds_ + seconds);
  } else {
    child_.selector_echo = 0;
  }
}
void MenuFlow::update_child_animation(float seconds) {
  constexpr float kScrollResponse = .08F, kDescriptionResponse = .17F,
                  kHighlightResponse = .2F, kPulsePeriod = 2.F;
  const bool costume = presentation_.page == MenuPage::costume;
  const bool difficulty = presentation_.page == MenuPage::difficulty;
  const float pitch = costume ? 172.F : 298.F;
  scroll_ = math::damped_response(
      scroll_,
      {-static_cast<float>(presentation_.selected) * pitch, kScrollResponse},
      seconds);
  child_description_ = math::damped_response(
      child_description_,
      {child_.motion == ChildMenuMotion::active ? 1.F : 0.F,
       kDescriptionResponse},
      seconds);
  child_.scroll = scroll_.value;
  child_.description_alpha = child_description_.value;
  for (std::size_t i = 0; i < presentation_.count; ++i) {
    const auto target =
        i == presentation_.selected ? 1.F : (difficulty ? .4F : 0.F);
    if (costume) {
      constexpr float kHighlightSpeed = 3.5F;
      const float delta =
          std::clamp(target - child_highlight_.at(i).value,
                     -kHighlightSpeed * seconds, kHighlightSpeed * seconds);
      child_highlight_.at(i).value += delta;
    } else {
      child_highlight_.at(i) = math::damped_response(
          child_highlight_.at(i), {target, kHighlightResponse}, seconds);
    }
    child_.highlight.at(i) = child_highlight_.at(i).value;
  }
  child_.pulse = std::fmod(child_.pulse + seconds, kPulsePeriod);
  child_.input_delay = std::max(0.F, child_.input_delay - seconds);
  child_.elapsed += seconds;
  if (difficulty) update_difficulty_selector(seconds);
  presentation_.child = child_;
  if (presentation_.page == MenuPage::stage ||
      presentation_.page == MenuPage::extra) {
    auto& stage = presentation_.stage;
    stage.visible = true;
    stage.is_concept = presentation_.page == MenuPage::extra;
    stage.count = presentation_.count;
    stage.selected = presentation_.selected;
    stage.motion = child_;
    for (std::size_t i = 0; i < stage.count; ++i) {
      stage.identities.at(i) = presentation_.rows.at(i).compatibility_id;
    }
    stage.best_rank = presentation_.rows.at(stage.selected).best_rank;
    stage.best_score = presentation_.rows.at(stage.selected).best_score;
  }
}
void MenuFlow::update_child(MenuInput input, float seconds) {
  update_child_animation(seconds);
  if (presentation_.dialog != FrontendDialog::none) {
    update_dialog(input, seconds);
    return;
  }
  if (save_pending_ || costume_complete_) return;
  if (presentation_.page == MenuPage::confirm) return;
  const bool costume = presentation_.page == MenuPage::costume;
  const bool difficulty = presentation_.page == MenuPage::difficulty;
  constexpr float kStageEntry = 1.8F, kCostumeEntry = 1.3F, kConceal = .25F;
  if (child_.motion == ChildMenuMotion::entering) {
    if (costume) {
      presentation_.opacity = std::min(1.F, child_.elapsed / kConceal);
    }
    if (child_.elapsed >= (costume ? kCostumeEntry : kStageEntry)) {
      child_.motion = ChildMenuMotion::active;
      if (difficulty) {
        constexpr float kCursorRevealScale = 1.2F;
        selector_scale_.value = kCursorRevealScale;
      }
      presentation_.child = child_;
    }
    return;
  }
  if (child_.motion == ChildMenuMotion::leaving) {
    constexpr float kCostumeReverse = .4F;
    const float duration =
        costume ? kCostumeReverse + kConceal
                : (difficulty && previous_ == MenuPage::main ? 1.8F : 1.F);
    if (costume) {
      presentation_.opacity =
          1 -
          std::clamp((child_.elapsed - kCostumeReverse) / kConceal, 0.F, 1.F);
    }
    if (child_.elapsed < duration) return;
    if (costume) {
      costume_complete_ = true;
    } else if (difficulty && child_exit_accept_) {
      finish(pending_, pending_ == MenuAction::new_game       ? 1U
                       : pending_ == MenuAction::stage_select ? 3U
                                                              : extra_status);
    } else if (difficulty && previous_ != MenuPage::main) {
      populate(previous_);
      presentation_.selected = stage_selection_;
      child_.motion = ChildMenuMotion::active;
      child_.elapsed = kStageEntry;
      constexpr float kStagePitch = 298.F;
      scroll_.value = -static_cast<float>(stage_selection_) * kStagePitch;
      presentation_.child = child_;
    } else {
      populate(MenuPage::main);
    }
    return;
  }
  if (difficulty_pending_) {
    child_transition_ += seconds;
    if (child_transition_ >= kConceal) enter_child(MenuPage::difficulty);
    return;
  }
  constexpr std::uint32_t kPageUpMask = 0x10000000U,
                          kPageDownMask = 0x20000000U;
  constexpr float kChangedDescription = .5F;
  const auto before = presentation_.selected;
  if ((input.pressed & cancel_mask) != 0) {
    sounds_.add(MenuSound::back);
    cancel();
  } else if ((input.pressed & confirm_mask) != 0 && child_.input_delay == 0) {
    sounds_.add(difficulty ? MenuSound::mission_start : MenuSound::confirm);
    accept();
  } else {
    int direction = 0;
    if ((input.repeated & up_mask) != 0) {
      direction = -1;
    } else if ((input.repeated & down_mask) != 0) {
      direction = 1;
    } else if (!difficulty && (input.repeated & kPageUpMask) != 0) {
      direction = costume ? -2 : -3;
    } else if (!difficulty && (input.repeated & kPageDownMask) != 0) {
      direction = costume ? 2 : 3;
    }
    if (difficulty) {
      navigate(direction);
    } else if (presentation_.count != 0) {
      presentation_.selected = static_cast<std::size_t>(
          std::clamp(static_cast<int>(presentation_.selected) + direction, 0,
                     static_cast<int>(presentation_.count) - 1));
    }
  }
  if (before != presentation_.selected) {
    sounds_.add(MenuSound::cursor);
    child_description_.value = kChangedDescription;
    if (!costume && !difficulty) {
      constexpr float kStageDebounce = .1F;
      child_.input_delay = kStageDebounce;
    }
  }
  presentation_.child = child_;
}
}  // namespace bw::game
