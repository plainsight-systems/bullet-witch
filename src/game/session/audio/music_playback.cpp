// Reconstruct the one source music command/envelope service, not one fade per
// requester. Source evidence and contract are in the header. Core I.1/E.6 and
// performance MEM.9 (read in implementation) keep native writes exclusively
// here and allocate only when the prepared-track receiver admits a new track.
#include "music_playback.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace bw::game {
namespace {
void valid_interval(float seconds) {
  if (!std::isfinite(seconds) || seconds < 0) {
    throw std::invalid_argument("Invalid music control interval");
  }
}
}  // namespace
MusicPlayback::MusicPlayback(audio::MusicOutput& output, Prepare prepare)
    : output_(output), prepare_(std::move(prepare)) {
  if (!prepare_) throw std::invalid_argument("Missing music preparation");
}
MusicPlayback::~MusicPlayback() { retire(); }
void MusicCommands::play(MusicRequest request) const { owner_->play(request); }
void MusicCommands::stop(float fade_seconds) const {
  owner_->stop(fade_seconds);
}
void MusicPlayback::play(MusicRequest request) {
  valid_interval(request.fade_seconds);
  if (request.track == 0) {
    throw std::invalid_argument("Invalid script music track");
  }
  // Native 44bd90 does not restart active music on another play request.
  if ((phase_ == Phase::fading_in || phase_ == Phase::playing) &&
      command_ != Command::stop) {
    return;
  }
  requested_track_ = request.track;
  requested_fade_ = request.fade_seconds;
  command_ = Command::play;
}
void MusicPlayback::stop(float fade_seconds) {
  valid_interval(fade_seconds);
  stop_fade_ = fade_seconds;
  command_ = Command::stop;
}
void MusicPlayback::retire() noexcept {
  output_.stop_music();
  phase_ = Phase::idle;
  command_ = Command::none;
  gain_ = 0;
}
bool MusicPlayback::advance(float control_seconds) {
  valid_interval(control_seconds);
  if (phase_ != Phase::idle && !output_.music_active()) phase_ = Phase::idle;
  if (phase_ == Phase::idle) {
    const bool admitted = command_ == Command::play;
    if (admitted) {
      prepare_(requested_track_);
      gain_ = requested_fade_ == 0 ? 1.F : 0.F;
      output_.set_music_envelope(gain_);
      output_.start_music();
      duration_ = requested_fade_;
      remaining_ = 0;
      phase_ = requested_fade_ == 0 ? Phase::playing : Phase::fading_in;
    }
    command_ = Command::none;
    return admitted;
  }
  if (phase_ == Phase::fading_in || phase_ == Phase::playing) {
    if (command_ == Command::none) {
      if (phase_ == Phase::fading_in) {
        remaining_ = std::min(duration_, remaining_ + control_seconds);
        gain_ = remaining_ / duration_;
        if (remaining_ == duration_) phase_ = Phase::playing;
        output_.set_music_envelope(gain_);
      }
      return false;
    }
    phase_ = Phase::fading_out;
    duration_ = remaining_ = stop_fade_;
    fade_start_ = gain_;
  }
  // Pending play survives this fade. Admission waits until the next update,
  // matching the native switch: the old track cannot fade or stop its
  // successor.
  remaining_ = std::max(0.F, remaining_ - control_seconds);
  gain_ = duration_ == 0 ? 0 : fade_start_ * remaining_ / duration_;
  output_.set_music_envelope(gain_);
  if (gain_ <= 0) {
    output_.stop_music();
    phase_ = Phase::idle;
  }
  return false;
}
}  // namespace bw::game
