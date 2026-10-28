// Map original actor-context actions, preserve strict priority admission and
// the source's reverse-copy quirk, then hand copied events to the world. Both
// implementation corpora: SL.con.3 bounds fixed storage; MEM.9 avoids
// allocation. This is attention policy; cue playback and voice arbitration
// remain separate.
#include "sound_stimuli.hpp"

#include <stdexcept>
namespace bw::game {
namespace {
struct Definition {
  float radius{-1}, priority{-1};
  std::uint32_t message{};
};
constexpr std::array<Definition, 22> kDefinitions{{{10, 1, 20001},
                                                   {10, 1, 20001},
                                                   {15, 1, 20001},
                                                   {15, 1, 20001},
                                                   {15, 1, 20001},
                                                   {15, 1, 20001},
                                                   {},
                                                   {},
                                                   {},
                                                   {15, 2, 20002},
                                                   {15, 1, 20002},
                                                   {30, 10, 20002},
                                                   {10, 10, 1},
                                                   {60, 100, 2},
                                                   {40, 100, 2},
                                                   {60, 200, 2},
                                                   {80, 300, 2},
                                                   {},
                                                   {},
                                                   {20, 10, 3},
                                                   {},
                                                   {}}};
}  // namespace
void ActorSoundStimuli::emit(std::uint32_t action,
                             const math::Vector3<float>& position,
                             std::uint32_t affiliation) {
  if (!math::all_finite(position)) {
    throw std::invalid_argument("Nonfinite actor sound stimulus");
  }
  if (action >= kDefinitions.size()) return;
  const auto definition = kDefinitions.at(action);
  if (definition.priority < 0) return;
  for (std::size_t i = 0; i < pending_.size(); ++i) {
    if (pending_.at(i).priority >= definition.priority) continue;
    // Original copies slot3 into2, then2 into1, etc. The replicated entry is
    // intentional source behavior, numerically witnessed independently.
    for (std::size_t j = pending_.size() - 1; j > i; --j) {
      pending_.at(j - 1) = pending_.at(j);
    }
    pending_.at(i) = {
        position,           affiliation,
        definition.message, static_cast<std::uint16_t>(action + 1),
        definition.radius,  definition.priority};
    return;
  }
}
std::array<ActorSoundStimulus, 4> ActorSoundStimuli::drain() {
  const auto result = pending_;
  pending_ = {};
  return result;
}
}  // namespace bw::game
