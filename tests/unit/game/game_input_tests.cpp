// Unit test: the game's actions and default mappings
// (bw/game/input/actions.hpp), one table-driven scenario.
#include "bw/game/input/actions.hpp"

#include "bw/input/key_state.hpp"
#include "bw/input/modifiers.hpp"
#include "bw/input/player_input.hpp"
#include "bw/platform/input/device_input.hpp"
#include "bw/testing/harness.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <initializer_list>
#include <memory>
#include <string>
#include <string_view>

namespace {

using bw::platform::InputElement;
using bw::platform::InputSample;
using bw::testing::require;

// (actions.hpp, contracts 1-4): the tables load, so every name they hold
// exists, with the default context applied; Move and Look are Axis2D and
// the rest Switch; a full stick looks at 1.7, 41 mouse counts at 1 with y
// flipped, the keys move by unit steps; PadA is Melee until the ring
// context is applied, then RingCast1's alone.
void input_tables() {
  const auto input = bw::input::PlayerInput::create({});
  const bw::game::GameInput loaded = bw::game::load_game_input(*input);
  require(loaded.ok(), "the game's tables load: " + loaded.failure.name);
  for (const bw::input::ActionDescription& action : bw::game::actions()) {
    const bool two_axes =
        action.name == bw::game::kActionMove || action.name == bw::game::kActionLook;
    require(action.type == (two_axes ? bw::input::ValueType::Axis2D : bw::input::ValueType::Switch),
            "the type of " + std::string(action.name));
  }
  struct Row {
    std::initializer_list<InputSample> samples;
    std::string_view action;
    bw::input::ActionValue value;
    bool ring;
    std::string_view silent;
  };
  const std::array rows{
      Row{{{InputElement::PadRightX, 1.0F}}, bw::game::kActionLook, {1.7F, 0.0F, 0.0F}, false, {}},
      Row{{{InputElement::MouseMotionY, 41.0F}},
          bw::game::kActionLook,
          {0.0F, -1.0F, 0.0F},
          false,
          {}},
      Row{{{InputElement::KeyW, 1.0F}}, bw::game::kActionMove, {0.0F, 1.0F, 0.0F}, false, {}},
      Row{{{InputElement::KeyS, 1.0F}}, bw::game::kActionMove, {0.0F, -1.0F, 0.0F}, false, {}},
      Row{{{InputElement::KeyA, 1.0F}}, bw::game::kActionMove, {-1.0F, 0.0F, 0.0F}, false, {}},
      Row{{{InputElement::KeyD, 1.0F}}, bw::game::kActionMove, {1.0F, 0.0F, 0.0F}, false, {}},
      Row{{{InputElement::PadA, 1.0F}}, bw::game::kActionMelee, {1.0F, 0.0F, 0.0F}, false, {}},
      Row{{{InputElement::PadA, 1.0F}},
          bw::game::kActionRingCast1,
          {1.0F, 0.0F, 0.0F},
          true,
          bw::game::kActionMelee},
  };
  for (std::size_t row = 0; row < rows.size(); ++row) {
    const auto player = bw::input::PlayerInput::create({});
    const bw::game::GameInput game = bw::game::load_game_input(*player);
    if (rows[row].ring) {
      require(player->add_context(game.ring_context, bw::game::kRingPriority) ==
                  bw::input::InputError::None,
              row, "the ring context applies");
    }
    bw::input::KeyState keys;
    for (const InputSample& sample : rows[row].samples) {
      keys.accumulate(sample);
    }
    keys.begin_frame();
    player->evaluate(keys);
    const bw::input::ActionState& state = player->state(player->find_action(rows[row].action));
    require(state.has(bw::input::ActionEvent::Started) &&
                std::abs(state.value.x - rows[row].value.x) < 1.0e-5F &&
                std::abs(state.value.y - rows[row].value.y) < 1.0e-5F,
            row, std::string(rows[row].action) + " fires with its value");
    if (!rows[row].silent.empty()) {
      require(player->state(player->find_action(rows[row].silent)).events == 0U, row,
              std::string(rows[row].silent) + " is hidden");
    }
  }
}

using Scenario = bw::testing::Scenario<void>;
constexpr std::array kScenarios{
    Scenario{"input_tables", &input_tables},
};

} // namespace

int main(int argc, char** argv) { return bw::testing::run_scenario(argc, argv, kScenarios); }
