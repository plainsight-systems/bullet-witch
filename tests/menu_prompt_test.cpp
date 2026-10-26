// Copied native UI prompts remain independent of gameplay bindings. Verify
// availability, source image identities, distinct geometry and invalid runs.
// Core I.1 and performance MEM.9 apply as in menu_prompt.hpp.
#include "game/menu_prompt.hpp"

#include <cmath>
#include <limits>
#include <stdexcept>

namespace {
void require(bool value, const char* message) {
  if (!value) throw std::runtime_error(message);
}
}  // namespace
void menu_prompt_contracts() {
  using bw::game::menu_prompts;
  using bw::game::valid_menu_prompt;
  const auto pad = menu_prompts(true);
  const auto keyboard = menu_prompts(false);
  require(pad.confirm.count == 1 && pad.confirm.icons[0] == 120 &&
              pad.back.count == 1 && pad.back.icons[0] == 121,
          "Xbox prompts use fixed UI face buttons");
  require(keyboard.confirm.count == 2 && keyboard.confirm.icons[0] == 94 &&
              keyboard.confirm.icons[1] == 54 && keyboard.back.count == 2 &&
              keyboard.back.icons[0] == 95 && keyboard.back.icons[1] == 39,
          "Keyboard UI defaults combine fixed and default key images");
  require(std::abs(keyboard.confirm.icon_pitch - 42.666667F) < .001F &&
              std::abs(keyboard.confirm.text_advance - 87.466667F) < .001F &&
              std::abs(pad.confirm.text_advance - 44.8F) < .001F,
          "Source scale-pivot advance yields the final canvas icon pitch");
  for (const auto prompt :
       {pad.confirm, pad.back, keyboard.confirm, keyboard.back}) {
    require(valid_menu_prompt(prompt), "Published prompt is valid");
  }
  auto invalid = pad.confirm;
  invalid.count = 5;
  require(!valid_menu_prompt(invalid), "Run overflow rejects before access");
  invalid = pad.confirm;
  invalid.icons[0] = 149;
  require(!valid_menu_prompt(invalid), "Unknown image is rejected");
  invalid = pad.confirm;
  invalid.icon_pitch = std::numeric_limits<float>::quiet_NaN();
  require(!valid_menu_prompt(invalid), "Nonfinite geometry is rejected");
}
