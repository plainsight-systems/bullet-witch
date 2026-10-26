// Original startup storage policy; see the header and source evidence report.
// I.1 makes admission of destructive replacement explicit. MEM.9 keeps frame
// state fixed; only the existing storage operation owns bounded file buffers.
#include "profile_load_flow.hpp"

#include <cmath>
#include <stdexcept>

namespace bw::game {
namespace {
constexpr float kRevealSeconds = .1F, kLoadHoldSeconds = 1,
                kCreateHoldSeconds = 3;
}
ProfileLoadFlow::ProfileLoadFlow(Profile& profile,
                                 platform::SaveStorage& storage,
                                 LogoMedia& media)
    : profile_(profile), storage_(storage), media_(media) {}
void ProfileLoadFlow::start() {
  if (progress_.status == FlowStatus::pending) {
    throw std::logic_error("Profile load already active");
  }
  phase_ = Phase::resources;
  progress_ = {FlowStatus::pending, false, 0};
  error_ = {};
  identity_ = ProfileLoadDialog::none;
  sounds_ = {};
  elapsed_ = 0;
  create_pending_ = false;
}
ProfileLoadPresentation ProfileLoadFlow::presentation() const {
  return {identity_, dialog_.presentation(),
          identity_ != ProfileLoadDialog::overwrite &&
              identity_ != ProfileLoadDialog::enable_autosave,
          phase_ == Phase::dialog};
}
MenuSoundRequests ProfileLoadFlow::take_sounds() {
  const auto result = sounds_;
  sounds_ = {};
  return result;
}
bool ProfileLoadFlow::error_pending() const {
  return identity_ == ProfileLoadDialog::overwrite ||
         identity_ == ProfileLoadDialog::load_failed;
}
void ProfileLoadFlow::collect_sounds() {
  const auto batch = dialog_.take_sounds();
  for (const auto& request : std::span(batch.requests).first(batch.size)) {
    sounds_.add(request.sound, request.delay_seconds);
  }
}
void ProfileLoadFlow::open_dialog(ProfileLoadDialog identity) {
  identity_ = identity;
  phase_ = Phase::dialog;
  const bool choice = identity == ProfileLoadDialog::overwrite ||
                      identity == ProfileLoadDialog::enable_autosave;
  dialog_.start(
      true, choice ? MenuDialogMode::yes_no : MenuDialogMode::acknowledgment);
  collect_sounds();
}
void ProfileLoadFlow::begin_progress(ProfileLoadDialog identity) {
  identity_ = identity;
  elapsed_ = 0;
  create_pending_ = identity == ProfileLoadDialog::creating;
  phase_ = create_pending_ ? Phase::creating : Phase::loading;
  dialog_.start(false, MenuDialogMode::acknowledgment);
  collect_sounds();
}
void ProfileLoadFlow::finish_dialog() {
  switch (identity_) {
    case ProfileLoadDialog::load_failed:
      open_dialog(ProfileLoadDialog::overwrite);
      return;
    case ProfileLoadDialog::overwrite:
      if (dialog_.presentation().yes) {
        begin_progress(ProfileLoadDialog::creating);
      } else {
        // The PC storage provider always has a device. Original No returns
        // to its check/load path, preserving existing files and active state.
        phase_ = Phase::lookup;
        identity_ = ProfileLoadDialog::none;
      }
      return;
    case ProfileLoadDialog::created:
      if (!profile_.get().autosave_enabled()) {
        open_dialog(ProfileLoadDialog::enable_autosave);
        return;
      }
      break;
    case ProfileLoadDialog::enable_autosave: {
      auto settings = profile_.get().settings_control();
      settings.set_autosave(dialog_.presentation().yes);
      if (dialog_.presentation().yes) {
        const auto status =
            profile_.get().settings_save_control(storage_).save();
        if (status != platform::SaveStatus::ok) {
          error_ = {status, "BulletWitchSystem"};
          // Original autosave-enable completion does not reopen replacement.
          // Retain the failed System save result and dirty settings for retry.
        }
      }
      break;
    }
    default:
      throw std::logic_error("Profile load completed a noninteractive page");
  }
  identity_ = ProfileLoadDialog::none;
  phase_ = Phase::conceal;
}
void ProfileLoadFlow::update(LogoUpdate input, bool resources_ready) {
  if (!std::isfinite(input.delta) || input.delta < 0) {
    throw std::invalid_argument(
        "Profile load interval must be finite and nonnegative");
  }
  sounds_ = {};
  if (progress_.status != FlowStatus::pending) return;
  switch (phase_) {
    case Phase::resources:
      if (!resources_ready) return;
      media_.get().begin_fade(FadeDirection::reveal, kRevealSeconds);
      progress_.visible = true;
      phase_ = Phase::revealing;
      break;
    case Phase::revealing:
      if (!media_.get().fade_pending()) phase_ = Phase::lookup;
      break;
    case Phase::lookup:
      error_ = profile_.get().load_startup(storage_);
      begin_progress(error_.status == platform::SaveStatus::missing &&
                             error_.file == "BulletWitchSystem"
                         ? ProfileLoadDialog::creating
                         : ProfileLoadDialog::loading);
      break;
    case Phase::loading:
      dialog_.update({}, input.delta);
      collect_sounds();
      elapsed_ += input.delta;
      if (elapsed_ >= kLoadHoldSeconds ||
          (error_.status != platform::SaveStatus::ok &&
           error_.status != platform::SaveStatus::missing)) {
        dialog_.close();
        collect_sounds();
      }
      if (!dialog_.presentation().complete) break;
      if (error_.status == platform::SaveStatus::ok) {
        sounds_.add(MenuSound::save);
        identity_ = ProfileLoadDialog::none;
        phase_ = Phase::conceal;
      } else {
        open_dialog(error_.status == platform::SaveStatus::missing
                        ? ProfileLoadDialog::load_failed
                        : ProfileLoadDialog::overwrite);
      }
      break;
    case Phase::creating:
      if (create_pending_) {
        error_ = profile_.get().create_defaults(storage_);
        create_pending_ = false;
      }
      dialog_.update({}, input.delta);
      collect_sounds();
      elapsed_ += input.delta;
      if (elapsed_ >= kCreateHoldSeconds ||
          error_.status != platform::SaveStatus::ok) {
        dialog_.close();
        collect_sounds();
      }
      if (!dialog_.presentation().complete) break;
      if (error_.status == platform::SaveStatus::ok) {
        open_dialog(ProfileLoadDialog::created);
        sounds_.add(MenuSound::save);
      } else {
        // Original SaveCore failure returns to the desktop lookup. A partial
        // save remains observable there; no creation acknowledgment is sent.
        identity_ = ProfileLoadDialog::none;
        phase_ = Phase::lookup;
      }
      break;
    case Phase::dialog:
      dialog_.update({input.pressed_actions, input.pressed_actions, 0},
                     input.delta);
      collect_sounds();
      if (dialog_.presentation().complete) finish_dialog();
      break;
    case Phase::conceal:
      media_.get().begin_fade(FadeDirection::conceal, 0);
      phase_ = Phase::closing;
      break;
    case Phase::closing:
      if (!media_.get().fade_pending()) {
        progress_.visible = false;
        phase_ = Phase::finish;
      }
      break;
    case Phase::finish:
      progress_.status = FlowStatus::complete;
      progress_.result = 1;
      break;
  }
}
}  // namespace bw::game
