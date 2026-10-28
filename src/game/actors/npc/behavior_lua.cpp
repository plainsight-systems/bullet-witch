// Original scalar-return scoring through the shared protected Lua host. Core
// I.1/SL.C.1 expose arguments and contain runtime failures; MEM.9 retains fixed
// invocation metadata. Source evaluator narrows numeric return to float before
// comparison; it does not coerce bool/string to a numeric score.
#include "behavior_lua.hpp"

#include <stdexcept>
namespace bw::game {
void BehaviorLuaScoring::begin_level() { preparation_.prepare_level(); }
float BehaviorLuaScoring::evaluate(const formats::BehaviorNode& node) {
  return evaluate_behavior(context_, actor_identity_, node);
}
float evaluate_behavior(script::Context& context, std::uint32_t actor_identity,
                        const formats::BehaviorNode& node) {
  const std::array<script::Value, 1> arguments{
      static_cast<double>(actor_identity)};
  context.start(node.evaluator, arguments);
  const auto completion = context.pump();
  if (completion.state != script::RunState::complete) {
    throw std::runtime_error("Behavior evaluator did not complete: " +
                             completion.diagnostic);
  }
  const auto number = context.numeric_result();
  return number ? static_cast<float>(*number) : 0;
}
}  // namespace bw::game
