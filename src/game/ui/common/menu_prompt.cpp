// Resolve the original fixed UI receiver into bounded icon runs. Both-phase
// Core I.1 and performance MEM.9 apply as documented in menu_prompt.hpp.
#include "menu_prompt.hpp"

#include <algorithm>
#include <cmath>
#include <span>

#include "input_icon.hpp"

namespace bw::game {
namespace {
constexpr std::uint16_t kNavigatePad = 102;
// Source advances a scale pivot by 64 around a fixed rect. At scale 1/3,
// its resulting canvas displacement is (1 - 1/3) *64, not 64.
constexpr float kIconPitch = 128.F / 3, kGlyphAdvance = 128.F / 3,
                kTextInset = 32.F / 15;
constexpr unsigned kIconCount = 149;
}  // namespace
MenuPrompts menu_prompts(MenuInputDevice device) noexcept {
  const auto key_run = [](std::span<const std::uint16_t> bindings) {
    MenuPrompt result;
    result.count = static_cast<std::uint8_t>(bindings.size());
    result.icon_pitch = kIconPitch;
    result.text_advance =
        static_cast<float>(bindings.size()) * kGlyphAdvance + kTextInset;
    for (std::size_t i = 0; i < bindings.size(); ++i) {
      std::span(result.icons)[i] =
          static_cast<std::uint16_t>(input_key_icon(bindings[i]));
    }
    return result;
  };
  const auto pad_run = [](input::PadButton button) {
    return MenuPrompt{{static_cast<std::uint16_t>(
                          input_pad_icon(static_cast<unsigned>(button)))},
                      1,
                      kIconPitch,
                      kGlyphAdvance + kTextInset};
  };
  if (device == MenuInputDevice::controller) {
    return {pad_run(kOriginalMenuBindings.confirm_button),
            pad_run(kOriginalMenuBindings.back_button),
            {{kNavigatePad}, 1, kIconPitch, kGlyphAdvance + kTextInset}};
  }
  const std::array<std::uint16_t, 4> navigation{
      // Original navigation descriptor orders wheel down before wheel up.
      kOriginalMenuBindings.wheel_keys[1], kOriginalMenuBindings.wheel_keys[0],
      kOriginalMenuBindings.navigation_keys[0],
      kOriginalMenuBindings.navigation_keys[1]};
  return {key_run(kOriginalMenuBindings.confirm_keys),
          key_run(kOriginalMenuBindings.back_keys), key_run(navigation)};
}
bool valid_menu_prompt(const MenuPrompt& prompt) noexcept {
  return prompt.count > 0 && prompt.count <= prompt.icons.size() &&
         std::isfinite(prompt.icon_pitch) && prompt.icon_pitch >= 0 &&
         std::isfinite(prompt.text_advance) && prompt.text_advance >= 0 &&
         std::ranges::all_of(std::span(prompt.icons).first(prompt.count),
                             [](auto icon) { return icon < kIconCount; });
}
}  // namespace bw::game
