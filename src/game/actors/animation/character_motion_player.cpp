// Integrate selected-motion events with retained source playback controllers.
// Header documents borrowed lifetimes and explicit action-policy boundaries.
// Implementation Core E.6 (#re-raii) resets scoped effects before sink
// teardown; MEM.9 (guidelines/memory/MEM.9-embedded-allocate-at-init.md) keeps
// ring and receiver/timeline storage fixed. Both implementation corpora
// consulted.
#include "character_motion_player.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace bw::game {
CharacterMotionPlayer::CharacterMotionPlayer(
    const CharacterAnimations& catalog, MotionPresentationSink& sink,
    std::size_t capacity, const std::shared_ptr<memory::Domain>& domain)
    : catalog_(catalog), sink_(sink), playback_(capacity, domain) {}
CharacterMotionPlayer::~CharacterMotionPlayer() { clear(); }

animation::PlaybackAdmission CharacterMotionPlayer::request(
    const CharacterMotionRequest& request) {
  if (!std::isfinite(request.blend_seconds) || request.blend_seconds < 0) {
    throw std::invalid_argument("Invalid requested motion blend duration");
  }
  const auto& clip = catalog_.motions().clip(request.clip);
  if (request.parameter && *request.parameter >= catalog_.parameters().size()) {
    throw std::out_of_range("Motion parameter identity is absent");
  }
  const auto* parameter =
      request.parameter ? &catalog_.parameters()[*request.parameter] : nullptr;
  const auto* previous = current();
  const bool rebind = request.fresh || previous == nullptr ||
                      previous->binding != request.binding ||
                      previous->clip != &clip || parameter_ != parameter;
  const auto start = std::min(request.start_seconds, clip.duration_seconds());
  const auto frame = static_cast<float>(start * clip.frames_per_second());
  // Source parameter transition durations apply only to actual selections;
  // a phase-preserving migration explicitly bypasses both INT overrides.
  auto blend = request.blend_seconds;
  if (!request.preserve_phase && (request.fresh || previous == nullptr ||
                                  previous->binding != request.binding)) {
    constexpr double kTransitionFramesPerSecond = 60;
    if (parameter_ != nullptr && parameter_->blend_frames &&
        parameter_->blend_frames->exit > 0) {
      blend = parameter_->blend_frames->exit / kTransitionFramesPerSecond;
    } else if (parameter != nullptr && parameter->blend_frames &&
               parameter->blend_frames->entry >= 0) {
      blend = parameter->blend_frames->entry / kTransitionFramesPerSecond;
    }
  }
  // Validate the whole event clock range before ring mutation or callbacks.
  if (parameter != nullptr &&
      (!std::isfinite(frame) || frame < 0 ||
       clip.duration_seconds() * clip.frames_per_second() >
           static_cast<double>(std::numeric_limits<std::int32_t>::max()))) {
    throw std::invalid_argument("Motion event clock exceeds source range");
  }
  const auto admission =
      playback_.request({.clip = &clip,
                         .binding = request.binding,
                         .channel = request.channel,
                         .start_seconds = request.start_seconds,
                         .blend_seconds = blend,
                         .rate = request.rate,
                         .fresh = request.fresh,
                         .remove_on_completion = request.remove_on_completion});
  if (rebind) {
    // Source positive seeks reset receiver latches without interrupt callbacks.
    if (receiver_) {
      if (timeline_ && request.start_seconds <= 0) {
        timeline_->interrupt(*receiver_);
      }
      receiver_->reset();
    }
    timeline_.reset();
    receiver_.reset();
    parameter_ = parameter;
    events_pending_ = false;
    retirement_pending_ = false;
    if (parameter != nullptr) {
      receiver_.emplace(parameter->presentation, sink_);
      timeline_.emplace(parameter->presentation.ordinary_channels(), frame);
    }
  }
  return admission;
}
void CharacterMotionPlayer::advance(double seconds) {
  playback_.advance(seconds);
  events_pending_ = events_pending_ || seconds > 0;
}
bool CharacterMotionPlayer::retire_completed_owner() {
  const auto* selected = current();
  if (selected == nullptr || selected->clip->repeating() ||
      !selected->completed) {
    return false;
  }
  playback_.clear();
  retirement_pending_ = true;
  return true;
}
void CharacterMotionPlayer::dispatch_events() {
  if (retirement_pending_) {
    clear();
    return;
  }
  // Model shape summation and pose composition precede source callbacks.
  const auto* selected = current();
  if (events_pending_ && selected != nullptr && timeline_ && receiver_) {
    timeline_->advance(static_cast<float>(selected->seconds *
                                          selected->clip->frames_per_second()),
                       *receiver_);
  }
  events_pending_ = false;
}
void CharacterMotionPlayer::finish_frame() { playback_.finish_frame(); }
void CharacterMotionPlayer::clear() noexcept {
  if (receiver_) {
    if (timeline_) timeline_->interrupt(*receiver_);
    receiver_->reset();
  }
  timeline_.reset();
  receiver_.reset();
  parameter_ = nullptr;
  events_pending_ = false;
  retirement_pending_ = false;
  playback_.clear();
}
void CharacterMotionPlayer::set_rate(float rate) {
  const auto current_slot = playback_.current_slot();
  if (!current_slot) throw std::logic_error("No selected motion rate target");
  playback_.set_rate(*current_slot, rate);
}
const animation::ClipPlaybackState* CharacterMotionPlayer::current() const {
  const auto current_slot = playback_.current_slot();
  if (!current_slot) return nullptr;
  const auto& state = playback_.state(*current_slot);
  return state.linked ? &state : nullptr;
}
bool CharacterMotionPlayer::cancellable() const {
  return receiver_ && receiver_->cancellation_enabled();
}
}  // namespace bw::game
