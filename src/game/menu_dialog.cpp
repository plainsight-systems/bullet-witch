// Reconstruct the common native dialog once, keeping callers' save/exit policy
// outside its bounded presentation state. E.4 and MEM.9 were reread during
// implementation: validate before mutation, no ordinary update allocation.
#include "menu_dialog.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

#include "bw/math/scalar.hpp"
namespace bw::game {
void MenuDialog::start(bool default_yes, MenuDialogMode mode) {
  mode_ = mode;
  view_ = {.yes = default_yes};
  elapsed_ = 0;
  sounds_ = {};
  sounds_.add(MenuSound::dialog_open);
}
void MenuDialog::update(MenuInput input, float seconds) {
  if (!std::isfinite(seconds) || seconds < 0) {
    throw std::invalid_argument("Invalid dialog interval");
  }
  sounds_ = {};
  if (view_.complete) {
    return;
  }
  constexpr float kGate = .3F, kRate = 4.4F, kTolerance = .01F;
  constexpr std::uint32_t kConfirm = 0x1010, kCancel = 0x2020,
                          kHorizontal = 0x0c000000;
  elapsed_ += seconds;
  if (view_.closing) {
    view_.progress = std::max(0.F, view_.progress - kRate * seconds);
    view_.complete = view_.progress < kTolerance;
    return;
  }
  view_.progress = std::min(1.F, view_.progress + kRate * seconds);
  if (elapsed_ < kGate || view_.progress < 1.F - kTolerance) {
    return;
  }
  if ((input.pressed & kConfirm) != 0) {
    if (mode_ == MenuDialogMode::yes_no) {
      sounds_.add(MenuSound::confirm);
    }
    sounds_.add(MenuSound::dialog_close);
    view_.closing = true;
  } else if (mode_ == MenuDialogMode::yes_no &&
             (input.pressed & kCancel) != 0) {
    sounds_.add(MenuSound::back);
    view_.yes = false;
  } else if (mode_ == MenuDialogMode::yes_no &&
             (input.repeated & kHorizontal) != 0) {
    sounds_.add(MenuSound::cursor);
    view_.yes = !view_.yes;
  }
}
MenuSoundRequests MenuDialog::take_sounds() {
  const auto result = sounds_;
  sounds_ = {};
  return result;
}
MenuDialogGeometry menu_dialog_geometry(float progress, unsigned text_lines,
                                        bool choices) {
  if (!std::isfinite(progress) || progress < 0 || progress > 1 ||
      text_lines == 0) {
    throw std::invalid_argument("Invalid dialog geometry");
  }
  constexpr float kSize = 48, kBorder = 36, kGap = 12, kChoiceHeight = 64,
                  kCenterY = 360, kHalf = .5F, kChoiceOffset = 26,
                  kExpansionEnd = .9F, kOpacityStart = .3F,
                  kDecorationStart = .7F, kChannelDuration = .3F;
  const auto half_height =
      (static_cast<float>(text_lines) * kSize + 2 * kBorder +
       (choices ? kGap + kChoiceHeight : 0)) *
      kHalf;
  const auto expansion = math::eased_tangent_interpolate(
      math::EasedTangentEndpoints<float>{0, 1, 0, 2},
      std::clamp(progress / kExpansionEnd, 0.F, 1.F));
  return {
      kCenterY - expansion * half_height,
      kCenterY + expansion * half_height,
      kCenterY - half_height + kBorder + kSize * kHalf,
      kCenterY + half_height - kBorder - kChoiceOffset,
      std::clamp((progress - kOpacityStart) / kChannelDuration, 0.F, 1.F),
      std::clamp((progress - kDecorationStart) / kChannelDuration, 0.F, 1.F)};
}
}  // namespace bw::game
