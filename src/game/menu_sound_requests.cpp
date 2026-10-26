// Pure bounded menu event values shared by progression and frontend flows.
// Audio bank/cue resolution and playback remain in MenuAudio. Core I.1 and
// performance MEM.9, read in both phases, keep this dependency free of backend
// ownership and reject invalid events/time/capacity before changing the batch.
#include <cmath>
#include <stdexcept>

#include "menu_audio.hpp"
namespace bw::game {
void MenuSoundRequests::add(MenuSound sound, float delay_seconds) {
  if (sound > MenuSound::upgrade_purchase || !std::isfinite(delay_seconds) ||
      delay_seconds < 0) {
    throw std::invalid_argument("Invalid menu sound request");
  }
  if (size >= requests.size()) {
    throw std::length_error("Menu sound batch capacity exceeded");
  }
  requests.at(size++) = {sound, delay_seconds};
}
}  // namespace bw::game
