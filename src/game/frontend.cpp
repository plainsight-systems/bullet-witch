// Front-end composition; see frontend.hpp for lifetime, transitions and cost.
// Menu readiness and fade completion gate input. Saving is only attempted for
// dirty profile data; an error never silently becomes successful persistence.
// Continue-without-saving retains in-memory choices and disables later writes.
// Design MCP I.30 (#ri-encapsulate), implementation E.6 (#re-raii), and
// cpp-performance COPY.4
// (guidelines/copy-move/COPY.4-rule-of-five-rule-of-zero.md) apply as in the
// header. Own no native handles or duplicated profile payloads.
#include "frontend.hpp"

#include <stdexcept>

namespace bw::game {
namespace {
constexpr float kMenuFadeSeconds = 0.5F;
constexpr std::uint32_t kVerticalActions = 0x03000000U;
constexpr std::uint32_t kConfirmActions = 0x1010U;
}  // namespace
FrontEnd::FrontEnd(Profile& profile, platform::SaveStorage& storage,
                   LogoMedia& media, MenuSurface& surface, MenuUnlocks unlocks)
    : profile_(profile),
      storage_(storage),
      media_(media),
      surface_(surface),
      load_(profile, storage, media),
      menu_(profile, unlocks) {}
FrontEnd::~FrontEnd() { surface_.end(); }
void FrontEnd::start(Screen screen) {
  if (screen == Screen::character_load) {
    surface_.begin();
    load_.start();
  } else if (screen == Screen::title_menu) {
    if (load_.progress().status != FlowStatus::complete) {
      throw std::logic_error(
          "Menu activation precedes profile load completion");
    }
    saving_enabled_ = load_.saving_enabled();
    menu_.start();
    menu_progress_ = {FlowStatus::pending, false, 0};
    phase_ = Phase::resources;
  } else {
    throw std::invalid_argument("Not a front-end screen");
  }
}
ScreenProgress FrontEnd::progress(Screen screen) const {
  if (screen == Screen::character_load) return load_.progress();
  if (screen == Screen::title_menu) return menu_progress_;
  throw std::invalid_argument("Not a front-end screen");
}
const std::optional<MenuSelection>& FrontEnd::selection() const {
  return menu_.selection();
}
void FrontEnd::show_error(ProfileResult result, unsigned choice) {
  MenuPresentation view;
  view.page = MenuPage::confirm;
  view.title = "Save data";
  view.rows[0].label = "Retry";
  view.rows[1].label = "Continue without saving";
  view.count = 2;
  view.selected = choice;
  view.message = result.status == platform::SaveStatus::invalid
                     ? "The save data could not be read."
                     : "The save data could not be accessed.";
  surface_.show(view);
}
void FrontEnd::persist() {
  if (!saving_enabled_ || !profile_.dirty()) {
    menu_.acknowledge_settings();
    return;
  }
  save_error_ = profile_.save(storage_);
  if (save_error_.status != platform::SaveStatus::ok) {
    error_choice_ = 0;
    phase_ = Phase::save_error;
    return;
  }
  menu_.acknowledge_settings();
}
void FrontEnd::update(Screen screen, LogoUpdate input, std::uint32_t repeated) {
  if (screen == Screen::character_load) {
    const bool ready = surface_.ready();
    load_.update(input, ready);
    if (ready && load_.progress().visible) {
      if (load_.error_pending()) {
        show_error(load_.error(), load_.error_choice());
      } else {
        MenuPresentation view;
        view.page = MenuPage::confirm;
        view.title = "Loading save data";
        surface_.show(view);
      }
    }
    return;
  }
  if (screen != Screen::title_menu) {
    throw std::invalid_argument("Not a front-end screen");
  }
  if (menu_progress_.status != FlowStatus::pending) return;
  switch (phase_) {
    case Phase::resources:
      if (!surface_.ready()) return;
      surface_.show(menu_.presentation());
      media_.begin_fade(FadeDirection::reveal, kMenuFadeSeconds);
      menu_progress_.visible = true;
      phase_ = Phase::revealing;
      break;
    case Phase::revealing:
      if (!media_.fade_pending()) phase_ = Phase::menu;
      break;
    case Phase::menu:
      menu_.update({input.pressed_actions, repeated});
      if (menu_.settings_changed() || menu_.selection()) persist();
      if (phase_ == Phase::save_error) break;
      if (menu_.selection()) {
        phase_ = Phase::conceal;
      } else {
        surface_.show(menu_.presentation());
      }
      break;
    case Phase::save_error:
      if ((input.pressed_actions & kVerticalActions) != 0) error_choice_ ^= 1U;
      if ((input.pressed_actions & kConfirmActions) != 0) {
        phase_ = Phase::menu;
        if (error_choice_ == 0) {
          persist();
        } else {
          saving_enabled_ = false;
          menu_.acknowledge_settings();
        }
      }
      break;
    case Phase::conceal:
      media_.begin_fade(FadeDirection::conceal, kMenuFadeSeconds);
      phase_ = Phase::closing;
      break;
    case Phase::closing:
      if (!media_.fade_pending()) phase_ = Phase::finish;
      break;
    case Phase::finish: {
      const auto selection = menu_.selection();
      if (!selection) {
        throw std::logic_error("Menu finished without a selection");
      }
      surface_.end();
      menu_progress_ = {FlowStatus::complete, false,
                        selection->original_status};
      break;
    }
  }
  if (phase_ == Phase::save_error) show_error(save_error_, error_choice_);
}
}  // namespace bw::game
