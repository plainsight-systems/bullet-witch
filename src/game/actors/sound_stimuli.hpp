// World-owned original actor-sound attention queue. Actor audio supplies copied
// action, position and affiliation; this owner maps the original stimulus facts
// and retains at most four events until the next world dispatch. It owns no
// audio handles, actors or clocks. Emission cannot synchronously enter AI.
// Invalid coordinates throw before mutation; disabled actions are discarded.
// Drain copies then clears the fixed queue. Source insertion deliberately
// replicates the last slot when inserting earlier: preserve the executed
// original behavior, rather than substituting a standard priority queue.
// SL.con.3 (#rsl-bounds) bounds all four slots; MEM.9
// (guidelines/memory/MEM.9-embedded-allocate-at-init.md) keeps frame storage
// fixed. Both design corpora read. Provenance: first-combat-exchange.md sound
// closure.
#pragma once
#include <array>
#include <cstdint>

#include "bw/math/vector.hpp"
namespace bw::game {
struct ActorSoundStimulus {
  math::Vector3<float> position{};
  std::uint32_t affiliation{}, message{};
  std::uint16_t sound{};
  float radius{}, priority{-1};
};
class ActorSoundStimuli {
 public:
  ActorSoundStimuli() = default;
  ActorSoundStimuli(const ActorSoundStimuli&) = delete;
  ActorSoundStimuli& operator=(const ActorSoundStimuli&) = delete;
  ActorSoundStimuli(ActorSoundStimuli&&) = delete;
  ActorSoundStimuli& operator=(ActorSoundStimuli&&) = delete;
  ~ActorSoundStimuli() = default;
  void emit(std::uint32_t action, const math::Vector3<float>& position,
            std::uint32_t affiliation);
  std::array<ActorSoundStimulus, 4> drain();

 private:
  std::array<ActorSoundStimulus, 4> pending_{};
};
}  // namespace bw::game
