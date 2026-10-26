// Resolve the original fixed UI receiver into bounded icon runs. Both-phase
// Core I.1 and performance MEM.9 apply as documented in menu_prompt.hpp.
#include "menu_prompt.hpp"

#include <algorithm>
#include <cmath>
#include <span>

namespace bw::game {
namespace {
constexpr std::uint16_t kConfirmPad = 120, kBackPad = 121, kConfirmKey = 94,
                        kBackKey = 95, kSpaceKey = 54, kShiftKey = 39;
// Source advances a scale pivot by 64 around a fixed rect. At scale 1/3,
// its resulting canvas displacement is (1 - 1/3) *64, not 64.
constexpr float kIconPitch = 128.F / 3, kGlyphAdvance = 128.F / 3,
                kTextInset = 32.F / 15;
constexpr unsigned kIconCount = 149;
}  // namespace
MenuPrompts menu_prompts(bool controller_connected) noexcept {
  if (controller_connected) {
    return {{{kConfirmPad}, 1, kIconPitch, kGlyphAdvance + kTextInset},
            {{kBackPad}, 1, kIconPitch, kGlyphAdvance + kTextInset}};
  }
  return {
      {{kConfirmKey, kSpaceKey}, 2, kIconPitch, 2 * kGlyphAdvance + kTextInset},
      {{kBackKey, kShiftKey}, 2, kIconPitch, 2 * kGlyphAdvance + kTextInset}};
}
bool valid_menu_prompt(const MenuPrompt& prompt) noexcept {
  return prompt.count > 0 && prompt.count <= prompt.icons.size() &&
         std::isfinite(prompt.icon_pitch) && prompt.icon_pitch >= 0 &&
         std::isfinite(prompt.text_advance) && prompt.text_advance >= 0 &&
         std::ranges::all_of(std::span(prompt.icons).first(prompt.count),
                             [](auto icon) { return icon < kIconCount; });
}
}  // namespace bw::game
