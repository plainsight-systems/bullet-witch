// Independent original queue witness: equal priorities fill four slots;
// inserting a higher-priority event replicates the original last slot.
#include "game/actors/sound_stimuli.hpp"

#include "../../../fixtures/camera_fixture.hpp"
void sound_stimuli_tests() {
  using camera_fixture::require;
  bw::game::ActorSoundStimuli queue;
  for (std::uint32_t action = 0; action < 5; ++action) {
    queue.emit(action, {static_cast<float>(action), 1, 2}, action + 100);
  }
  const auto first = queue.drain();
  require(first[0].sound == 1 && first[1].sound == 2 && first[2].sound == 3 &&
              first[3].sound == 4,
          "Equal-priority attention events retain four, dropping the fifth");
  for (std::uint32_t action = 0; action < 4; ++action) {
    queue.emit(action, {}, 2);
  }
  queue.emit(13, {5, 6, 7}, 4);
  const auto replaced = queue.drain();
  require(
      replaced[0].sound == 14 && replaced[1].sound == 4 &&
          replaced[2].sound == 4 && replaced[3].sound == 4,
      "Higher-priority insertion preserves the witnessed source copy quirk");
  require(replaced[0].affiliation == 4 && replaced[0].radius == 60 &&
              replaced[0].message == 2 && replaced[0].position[1] == 6,
          "Attention event retains authored mapping and copied emitter facts");
  queue.emit(6, {}, 2);
  queue.emit(100, {}, 2);
  for (const auto& item : queue.drain()) {
    require(item.priority < 0,
            "Drain and disabled actions leave no live stimulus");
  }
}
