// Original native yes/no task shared by pause, Options and controller settings.
// Owns only selection and presentation progress; callers interpret completion.
// Back selects No, horizontal toggles, Confirm closes after the native input
// gate. No world clock, resource/storage capability or allocation is admitted.
// The caller supplies presentation seconds and consumes bounded UI sounds.
// Shared layout reproduces the same authored panel geometry for every client.
// Core E.4 (#re-design-invariants) and performance MEM.9 (guidelines/memory/
// MEM.9-embedded-allocate-at-init.md), read in both phases, enforce validated
// updates and fixed snapshots. Source task 4a8340 is documented in
// research/engine/subsystems/native-pause-menu.md.
#pragma once
#include "game/ui/audio/menu_audio.hpp"
#include "menu_input.hpp"
namespace bw::game {
enum class MenuDialogMode : std::uint8_t { yes_no, acknowledgment };
struct MenuDialogPresentation {
  bool yes{}, closing{}, complete{};
  float progress{};
};
class MenuDialog {
 public:
  void start(bool default_yes, MenuDialogMode mode = MenuDialogMode::yes_no);
  void update(MenuInput input, float seconds);
  // Original storage progress tasks disable input and close their dialog
  // explicitly after the operation/minimum display interval. Idempotent.
  void close();
  const MenuDialogPresentation& presentation() const { return view_; }
  MenuSoundRequests take_sounds();

 private:
  MenuDialogPresentation view_;
  MenuSoundRequests sounds_;
  float elapsed_{};
  MenuDialogMode mode_{MenuDialogMode::yes_no};
};
// Native text defaults to 32 pixels; 0x30 is center alignment, not size.
// text_y is the first line center, choice_y is the choice label center.
// Both rendering and measured panel height use this one font-size contract.
inline constexpr float menu_dialog_font_size = 32;
struct MenuDialogGeometry {
  float top{}, bottom{}, text_y{}, choice_y{}, opacity{}, decoration{};
};
MenuDialogGeometry menu_dialog_geometry(float progress, unsigned text_lines,
                                        bool choices = true);
}  // namespace bw::game
