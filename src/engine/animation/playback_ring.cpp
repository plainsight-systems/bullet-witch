// Retained source controller scheduling, separate from action/event policy.
// playback_ring.hpp declares borrowed clip lifetime and phase ordering.
// Implementation E.4 (#re-design-invariants) validates an entire tick before
// mutation; MEM.9 (guidelines/memory/MEM.9-embedded-allocate-at-init.md)
// retains constructor-sized entries and contribution storage. Both MCP corpora
// were consulted for design and implementation. Source equations/retirement and
// the pinned Unreal ownership comparison are in alicia-animation-layers.md.
#include "bw/animation/playback_ring.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace bw::animation {
namespace {
constexpr std::size_t kMaximumControllers = 32;
double sampled_time(double time, const PreparedClip& clip) {
  const auto duration = clip.duration_seconds();
  if (!clip.repeating()) return std::clamp(time, 0.0, duration);
  auto result = std::fmod(time, duration);
  if (result < 0) result += duration;
  return result;
}
}  // namespace

PlaybackRing::PlaybackRing(std::size_t capacity,
                           const std::shared_ptr<memory::Domain>& domain)
    : entries_(memory::Allocator<Entry>(domain)),
      contributions_(memory::Allocator<PoseContribution>(domain)) {
  if (capacity == 0 || capacity > kMaximumControllers) {
    throw std::invalid_argument("Playback controller capacity must be 1..32");
  }
  entries_.resize(capacity);
  contributions_.resize(capacity);
}

PlaybackAdmission PlaybackRing::request(const ClipPlaybackRequest& request) {
  if (!request.clip || request.channel >= layers_.size() ||
      !std::isfinite(request.start_seconds) || request.start_seconds < 0 ||
      !std::isfinite(request.blend_seconds) || request.blend_seconds < 0 ||
      !std::isfinite(request.rate)) {
    throw std::invalid_argument("Invalid playback controller request");
  }
  if (current_) {
    auto& state = entries_.at(*current_).state;
    if (state.linked && state.clip == request.clip && !request.fresh) {
      // A publication alias changes the requested identity without restarting
      // an already active resource or changing its channel/rate/fade.
      state.binding = request.binding;
      return {.slot = *current_, .started = false, .replaced = std::nullopt};
    }
  }
  if (generation_ == std::numeric_limits<std::uint64_t>::max()) {
    throw std::overflow_error("Playback controller generation exhausted");
  }
  const auto slot = current_ ? (*current_ + 1) % entries_.size() : 0;
  auto& destination = entries_.at(slot);
  PlaybackAdmission admission{.slot = slot, .started = true};
  if (destination.state.linked) admission.replaced = destination.state;
  const auto start =
      std::min(request.start_seconds, request.clip->duration_seconds());
  const auto sample = sampled_time(start, *request.clip);
  const auto initial_weight = current_ ? 0.0F : 1.0F;
  destination = {
      .state = {.clip = request.clip,
                .binding = request.binding,
                .generation = ++generation_,
                .channel = request.channel,
                .previous_seconds = sample,
                .seconds = sample,
                .previous_unwrapped_seconds = start,
                .unwrapped_seconds = start,
                .rate = request.rate,
                .weight = request.blend_seconds > 0 ? initial_weight : 1.0F,
                .linked = true,
                .completed = false,
                .remove_on_completion = request.remove_on_completion},
      .blend_remaining = request.blend_seconds,
      .blend_duration = request.blend_seconds,
      .initial_weight = initial_weight};
  current_ = slot;
  publish_layers();
  return admission;
}

void PlaybackRing::advance(double seconds) {
  if (!std::isfinite(seconds) || seconds < 0) {
    throw std::invalid_argument("Invalid playback controller delta");
  }
  // Check clock overflow before any retirement, blend or pose-view mutation.
  for (const auto& entry : entries_) {
    const auto& state = entry.state;
    if (!state.linked) continue;
    const auto delta = seconds * state.rate;
    if (!std::isfinite(delta) ||
        !std::isfinite(state.unwrapped_seconds + delta) ||
        !std::isfinite(state.seconds + delta)) {
      throw std::overflow_error("Playback controller clock overflow");
    }
  }
  for (auto& entry : entries_) {
    auto& state = entry.state;
    if (!state.linked) continue;
    const auto delta = seconds * state.rate;
    state.previous_seconds = state.seconds;
    state.previous_unwrapped_seconds = state.unwrapped_seconds;
    const auto next = state.seconds + delta;
    state.completed = next >= state.clip->duration_seconds() || next < 0;
    state.seconds = sampled_time(next, *state.clip);
    state.unwrapped_seconds = state.clip->repeating()
                                  ? state.unwrapped_seconds + delta
                                  : state.seconds;
    entry.blend_remaining = std::max(0.0, entry.blend_remaining - seconds);
    state.weight = entry.blend_duration > 0
                       ? static_cast<float>(1 + (entry.initial_weight - 1) *
                                                    (entry.blend_remaining /
                                                     entry.blend_duration))
                       : 1;
  }
  // Model update advances clocks before pruning obscured entries; root/pose
  // consumers then observe the resulting retained chain.
  retire_obscured();
  publish_layers();
}

void PlaybackRing::finish_frame() {
  // This is the source post-pose auto-removal phase. Holding a terminal pose
  // or clearing an owner's entire bank remains an explicit game decision.
  for (auto& entry : entries_) {
    if (entry.state.linked && entry.state.completed &&
        entry.state.remove_on_completion) {
      entry.state.linked = false;
    }
  }
  publish_layers();
}

void PlaybackRing::clear() {
  for (auto& entry : entries_) entry.state.linked = false;
  current_.reset();
  layer_count_ = 0;
}

void PlaybackRing::set_rate(std::size_t slot, float rate) {
  auto& state = entries_.at(slot).state;
  if (!state.linked || !std::isfinite(rate)) {
    throw std::invalid_argument("Invalid playback controller rate target");
  }
  state.rate = rate;
}

void PlaybackRing::retire_obscured() {
  std::uint64_t fully_weighted_generation = 0;
  for (const auto& entry : entries_) {
    if (entry.state.linked && entry.state.weight >= 1) {
      fully_weighted_generation =
          std::max(fully_weighted_generation, entry.state.generation);
    }
  }
  for (auto& entry : entries_) {
    if (entry.state.generation < fully_weighted_generation) {
      entry.state.linked = false;
    }
  }
}

void PlaybackRing::publish_layers() {
  layer_count_ = 0;
  if (!current_) return;
  std::size_t used = 0;
  for (std::uint32_t channel = 0; channel < layers_.size(); ++channel) {
    const auto first = used;
    // Starting immediately after the latest ring entry visits retained
    // contributors oldest first, even when interruption has recycled a slot.
    for (std::size_t offset = 1; offset <= entries_.size(); ++offset) {
      const auto& state =
          entries_.at((*current_ + offset) % entries_.size()).state;
      if (!state.linked || state.channel != channel) continue;
      contributions_.at(used++) = {state.clip, state.seconds, state.weight};
    }
    if (used == first) continue;
    layers_.at(layer_count_++) = {
        channel, std::span<const PoseContribution>(contributions_)
                     .subspan(first, used - first)};
  }
}
}  // namespace bw::animation
