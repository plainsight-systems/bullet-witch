// Front-end composition; see frontend.hpp for lifetime, transitions and cost.
// Presentation deltas advance the original menu tracks through reveal and
// interaction; the menu policy owns admission and publishes only after its
// exit animation. Core I.1 (#ri-explicit) and performance MEM.9
// (guidelines/memory/MEM.9-embedded-allocate-at-init.md) were read for design
// and implementation: pass the existing delta, never create another clock.
// Menu readiness and fade completion gate input. Saving is only attempted for
// dirty profile data; an error never silently becomes successful persistence.
// Profile-load failures follow the original retry/overwrite dialog policy.
// Design MCP I.30 (#ri-encapsulate), implementation E.6 (#re-raii), and
// cpp-performance COPY.4
// (guidelines/copy-move/COPY.4-rule-of-five-rule-of-zero.md) apply as in the
// header. Own no native handles or duplicated profile payloads.
#include "frontend.hpp"

#include <stdexcept>

#include "game/ui/audio/frontend_audio.hpp"

namespace bw::game {
namespace {
constexpr float kMenuFadeSeconds = 0.5F;
// The load owner exposes identities, never replacement prose. The shared
// native surface resolves each identity from purchased localized resources.
FrontendDialog profile_dialog(ProfileLoadDialog identity) {
  switch (identity) {
    case ProfileLoadDialog::none:
      return FrontendDialog::none;
    case ProfileLoadDialog::loading:
      return FrontendDialog::profile_loading;
    case ProfileLoadDialog::creating:
      return FrontendDialog::profile_creating;
    case ProfileLoadDialog::load_failed:
      return FrontendDialog::profile_load_failed;
    case ProfileLoadDialog::overwrite:
      return FrontendDialog::profile_overwrite;
    case ProfileLoadDialog::created:
      return FrontendDialog::profile_created;
    case ProfileLoadDialog::enable_autosave:
      return FrontendDialog::profile_enable_autosave;
  }
  throw std::logic_error("Unknown profile-load dialog");
}

}  // namespace
FrontEnd::FrontEnd(Profile& profile, platform::SaveStorage& storage,
                   LogoMedia& media, MenuSurface& surface, MenuUnlocks unlocks,
                   PowerUpOutput* power_up_output, FrontEndAudio* audio,
                   std::optional<FrontendOptionsServices> options,
                   RankingOutput* ranking_output)
    : profile_(profile),
      storage_(storage),
      media_(media),
      surface_(surface),
      power_up_output_(power_up_output),
      audio_(audio),
      ranking_output_(ranking_output),
      load_(profile, storage, media),
      menu_(profile, unlocks),
      power_up_(profile.upgrade_purchases()) {
  if (options) {
    options_.emplace(options->settings, OptionsContext::frontend);
    options_output_ = &options->output.get();
  }
}
FrontEnd::~FrontEnd() {
  if (options_output_ != nullptr) options_output_->end();
  if (ranking_output_ != nullptr) ranking_output_->end();
  if (power_up_output_ != nullptr) power_up_output_->end();
  surface_.end();
}
void FrontEnd::start(Screen screen) {
  if (menu_progress_.status == FlowStatus::pending ||
      power_up_progress_.status == FlowStatus::pending) {
    throw std::logic_error("A front-end task already owns the shared surface");
  }
  if (screen == Screen::character_load) {
    surface_.begin();
    load_.start();
  } else if (screen == Screen::title_menu) {
    if (load_.progress().status != FlowStatus::complete) {
      throw std::logic_error(
          "Menu activation precedes profile load completion");
    }
    menu_.start();
    if (audio_ != nullptr) {
      audio_->start_menu_music();
      audio_->submit(menu_.take_sounds());
    }
    menu_progress_ = {FlowStatus::pending, false, 0};
    phase_ = Phase::resources;
  } else if (screen == Screen::power_up) {
    if (load_.progress().status != FlowStatus::complete) {
      throw std::logic_error("Power-up activation precedes profile load");
    }
    if (power_up_output_ == nullptr) {
      throw std::logic_error("Power-up presentation capability is required");
    }
    surface_.end();
    power_up_output_->begin();
    power_up_.start();
    if (audio_ != nullptr) audio_->submit(power_up_.take_sounds());
    power_up_progress_ = {FlowStatus::pending, false, 0};
    power_up_phase_ = Phase::resources;
  } else {
    throw std::invalid_argument("Not a front-end screen");
  }
}
ScreenProgress FrontEnd::progress(Screen screen) const {
  if (screen == Screen::character_load) return load_.progress();
  if (screen == Screen::title_menu) return menu_progress_;
  if (screen == Screen::power_up) return power_up_progress_;
  throw std::invalid_argument("Not a front-end screen");
}
const std::optional<MenuSelection>& FrontEnd::selection() const {
  return menu_.selection();
}
void FrontEnd::update(Screen screen, LogoUpdate input, std::uint32_t repeated,
                      const input::PhysicalState& physical,
                      float vertical_axis) {
  const MenuInput menu_input{input.pressed_actions, repeated, vertical_axis};
  menu_.set_prompts(menu_prompts(input.controller_connected));
  if (screen == Screen::character_load) {
    const bool ready = surface_.ready();
    load_.update(input, ready);
    if (audio_ != nullptr) audio_->submit(load_.take_sounds());
    if (ready && load_.progress().visible) {
      const auto loaded = load_.presentation();
      MenuPresentation view;
      view.page = MenuPage::confirm;
      view.storage_background = true;
      view.dialog = profile_dialog(loaded.identity);
      view.dialog_view = loaded.dialog;
      view.dialog_interactive = loaded.interactive;
      view.prompts = menu_prompts(input.controller_connected);
      surface_.show(view);
    }
    return;
  }
  if (screen == Screen::power_up) {
    update_power_up(input, repeated);
    return;
  }
  if (screen != Screen::title_menu) {
    throw std::invalid_argument("Not a front-end screen");
  }
  if (menu_progress_.status != FlowStatus::pending) return;
  if (options_active_) {
    update_options(input, menu_input, physical);
    return;
  }
  switch (phase_) {
    case Phase::resources:
      if (!surface_.ready()) return;
      surface_.show(menu_.presentation());
      media_.begin_fade(FadeDirection::reveal, kMenuFadeSeconds);
      menu_progress_.visible = true;
      phase_ = Phase::revealing;
      break;
    case Phase::revealing:
      menu_.update({}, input.delta);
      surface_.show(menu_.presentation());
      if (!media_.fade_pending()) phase_ = Phase::menu;
      break;
    case Phase::menu:
      menu_.update(menu_input, input.delta);
      if (menu_.presentation().page == MenuPage::options) {
        if (!options_ || options_output_ == nullptr) {
          throw std::logic_error("Frontend Options capability missing");
        }
        surface_.end();
        options_->start(profile_.costume());
        if (audio_ != nullptr) audio_->submit(options_->take_sounds());
        options_output_->activate(FrontendOptionsView::options);
        options_active_ = true;
        break;
      }
      if (menu_.take_settings_save_request()) {
        const auto result = profile_.record_save_control(storage_).save();
        menu_.settings_save_completed(result == platform::SaveStatus::ok);
      }
      if (menu_.selection()) {
        phase_ = Phase::conceal;
      } else {
        surface_.show(menu_.presentation());
      }
      break;
    case Phase::conceal:
      menu_.update({}, input.delta);
      surface_.show(menu_.presentation());
      media_.begin_fade(FadeDirection::conceal, menu_.departure_fade_seconds());
      phase_ = Phase::closing;
      break;
    case Phase::closing:
      // The native selector echo keeps animating while the session conceals.
      // Selection is already immutable; this advances presentation only.
      menu_.update({}, input.delta);
      surface_.show(menu_.presentation());
      if (!media_.fade_pending()) phase_ = Phase::finish;
      break;
    case Phase::finish: {
      // Publish the original status only after conceal and surface retirement.
      const auto selection = menu_.selection();
      if (!selection) {
        throw std::logic_error("Menu finished without a selection");
      }
      // Continue/stage/concept music fade belongs to the parent after the
      // child has completed conceal; new-game/title emit earlier.
      if (audio_ != nullptr &&
          (selection->action == MenuAction::continue_game ||
           selection->action == MenuAction::stage_select ||
           selection->action == MenuAction::concept_mission)) {
        constexpr float kMissionMusicFadeSeconds = 3;
        audio_->stop_menu_music(kMissionMusicFadeSeconds);
      }
      surface_.end();
      menu_progress_ = {FlowStatus::complete, false,
                        selection->original_status};
      break;
    }
  }
  if (audio_ != nullptr) {
    audio_->submit(menu_.take_sounds());
    if (const auto fade = menu_.take_music_fade()) {
      audio_->stop_menu_music(*fade);
    }
  }
}

// Parent Options remains alive while its native child owns presentation. Each
// child's completion returns to the retained Options row and edit transaction.
void FrontEnd::complete_options_child() {
  if (!options_) {
    throw std::logic_error("Options child completed without its parent owner");
  }
  const bool changed = options_child_ == OptionsChild::power_up &&
                       profile_.progression() != child_progression_;
  if (options_child_ == OptionsChild::power_up) {
    // Power Up owns the global conceal. Options owns the returning quarter-
    // second sprite reveal, so clear the completed overlay without a second
    // multiplicative fade or an independent clock.
    media_.begin_fade(FadeDirection::reveal, 0);
  }
  options_child_.reset();
  options_->complete_child(changed);
  options_output_->activate(FrontendOptionsView::options);
}
void FrontEnd::update_options(LogoUpdate input, MenuInput menu_input,
                              const input::PhysicalState& physical) {
  if (!options_ || options_output_ == nullptr) {
    throw std::logic_error("Active Options lost its borrowed capability");
  }
  if (options_returning_) {
    if (!surface_.ready()) return;
    options_returning_ = false;
    options_active_ = false;
    menu_.complete_options();
    surface_.show(menu_.presentation());
    return;
  }
  if (options_child_ == OptionsChild::costume) {
    if (!surface_.ready()) return;
    menu_.update(menu_input, input.delta);
    if (menu_.take_settings_save_request()) {
      const auto result = profile_.record_save_control(storage_).save();
      menu_.settings_save_completed(result == platform::SaveStatus::ok);
    }
    if (audio_ != nullptr) audio_->submit(menu_.take_sounds());
    if (menu_.costume_child_complete()) {
      surface_.end();
      menu_.acknowledge_costume_child();
      complete_options_child();
    } else {
      surface_.show(menu_.presentation());
    }
    return;
  }
  if (options_child_ == OptionsChild::power_up) {
    update_power_up(input, menu_input.repeated);
    if (power_up_progress_.status == FlowStatus::complete) {
      complete_options_child();
    }
    return;
  }
  if (options_child_ == OptionsChild::leaderboards) {
    if (ranking_output_ == nullptr) {
      throw std::logic_error("Ranking output missing");
    }
    if (!ranking_output_->ready()) return;
    ranking_.update(menu_input, input.delta);
    if (audio_ != nullptr) audio_->submit(ranking_.take_sounds());
    if (ranking_.completed()) {
      ranking_output_->end();
      complete_options_child();
    } else {
      auto view = ranking_.presentation();
      view.prompts = menu_prompts(input.controller_connected);
      ranking_output_->show(view);
    }
    return;
  }
  if (!options_output_->ready()) return;
  options_->update(menu_input, input.delta, physical);
  if (audio_ != nullptr) audio_->submit(options_->take_sounds());
  if (options_->completed()) {
    options_output_->end();
    surface_.begin();
    options_returning_ = true;
    return;
  }
  if (const auto child = options_->requested_child()) {
    options_output_->end();
    options_child_ = child;
    switch (*child) {
      case OptionsChild::costume:
        surface_.begin();
        menu_.begin_costume_child();
        if (audio_ != nullptr) audio_->submit(menu_.take_sounds());
        break;
      case OptionsChild::power_up:
        child_progression_ = profile_.progression();
        if (power_up_output_ == nullptr) {
          throw std::logic_error("Power-up presentation capability missing");
        }
        power_up_output_->begin();
        power_up_.start();
        if (audio_ != nullptr) audio_->submit(power_up_.take_sounds());
        power_up_progress_ = {FlowStatus::pending, false, 0};
        power_up_phase_ = Phase::resources;
        break;
      case OptionsChild::leaderboards:
        if (ranking_output_ == nullptr) {
          throw std::logic_error("Ranking output missing");
        }
        ranking_output_->begin();
        ranking_.start(RankingAvailability::unavailable);
        if (audio_ != nullptr) audio_->submit(ranking_.take_sounds());
        break;
    }
    return;
  }
  if (options_->presentation().page == OptionsPage::controls) {
    options_output_->activate(FrontendOptionsView::controls);
    if (options_output_->ready()) {
      auto view = options_->controller_presentation();
      view.prompts = menu_prompts(input.controller_connected);
      options_output_->show(view);
    }
  } else {
    options_output_->activate(FrontendOptionsView::options);
    if (options_output_->ready()) {
      auto view = options_->presentation();
      view.prompts = menu_prompts(input.controller_connected);
      options_output_->show(view);
    }
  }
}

// The native post-result task publishes its completion only after conceal.
// Confirmed purchases dirty Profile; the original root owns the later save
// task.
void FrontEnd::update_power_up(LogoUpdate input, std::uint32_t repeated) {
  constexpr float kPowerUpFadeSeconds = .25F;
  if (power_up_progress_.status != FlowStatus::pending) return;
  if (power_up_output_ == nullptr) {
    throw std::logic_error("Missing power-up output");
  }
  switch (power_up_phase_) {
    case Phase::resources:
      if (!power_up_output_->ready()) return;
      media_.begin_fade(FadeDirection::reveal, kPowerUpFadeSeconds);
      power_up_progress_.visible = true;
      power_up_phase_ = Phase::revealing;
      break;
    case Phase::revealing:
      power_up_.update({}, input.delta);
      if (!media_.fade_pending()) power_up_phase_ = Phase::menu;
      break;
    case Phase::menu:
      power_up_.update({input.pressed_actions, repeated}, input.delta);
      if (power_up_.conceal_requested()) {
        power_up_phase_ = Phase::conceal;
      }
      break;
    case Phase::conceal:
      power_up_.update({}, input.delta);
      media_.begin_fade(FadeDirection::conceal, kPowerUpFadeSeconds);
      power_up_phase_ = Phase::closing;
      break;
    case Phase::closing:
      power_up_.update({}, input.delta);
      if (!media_.fade_pending()) power_up_phase_ = Phase::finish;
      break;
    case Phase::finish:
      power_up_output_->end();
      power_up_progress_ = {FlowStatus::complete, false, 1};
      break;
  }
  if (audio_ != nullptr) audio_->submit(power_up_.take_sounds());
  if (power_up_progress_.visible) {
    auto view = power_up_.presentation();
    view.input_layout = input.controller_connected
                            ? PowerUpInputLayout::controller
                            : PowerUpInputLayout::keyboard;
    power_up_output_->show(view);
  }
}
}  // namespace bw::game
