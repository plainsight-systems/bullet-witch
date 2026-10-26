// Source music fade is game policy over a shared native output, independent of
// menu object lifetime and world pause. E.6 and MEM.9 (see header) keep
// ownership explicit and update allocation-free; both implementation corpora
// consulted.
#include "game/frontend_audio.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace bw::game {
FrontEndAudio::FrontEndAudio(audio::CueOutput& output,
                             std::function<void()> prepare_music)
    : cues_(output),
      music_(output.music()),
      prepare_music_(std::move(prepare_music)) {
  if (!prepare_music_) throw std::invalid_argument("Missing music preparation");
}
void FrontEndAudio::submit(const MenuSoundRequests& requests) {
  cues_.submit(requests);
}
void FrontEndAudio::start_menu_music() {
  if (!prepared_) {
    prepare_music_();
    prepared_ = true;
  }
  music_.set_music_envelope(1);
  music_.start_music();
  gain_ = 1;
  fading_ = false;
  remaining_ = duration_ = 0;
}
void FrontEndAudio::stop_menu_music(float duration_seconds) {
  if (!std::isfinite(duration_seconds) || duration_seconds < 0) {
    throw std::invalid_argument("Invalid frontend music fade duration");
  }
  if (duration_seconds == 0) {
    music_.stop_music();
    gain_ = 0;
    fading_ = false;
    remaining_ = duration_ = 0;
    return;
  }
  fade_start_ = gain_;
  remaining_ = duration_ = duration_seconds;
  fading_ = true;
}
void FrontEndAudio::advance(float presentation_seconds) {
  if (!std::isfinite(presentation_seconds) || presentation_seconds < 0) {
    throw std::invalid_argument("Invalid frontend audio delta");
  }
  cues_.advance(presentation_seconds);
  if (!fading_) return;
  remaining_ = std::max(0.0, remaining_ - presentation_seconds);
  gain_ = fade_start_ * static_cast<float>(remaining_ / duration_);
  music_.set_music_envelope(gain_);
  if (remaining_ == 0) {
    music_.stop_music();
    fading_ = false;
  }
}
}  // namespace bw::game
