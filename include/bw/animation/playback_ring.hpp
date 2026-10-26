// Bounded retained clip controllers, independent of gameplay and event sinks.
// A caller-configured ring (1..32 entries) owns clocks and contribution
// storage, borrowing immutable PreparedClips until destruction. Source game
// capacities are 4/2/1; they are policy, not global engine defaults. One owner
// thread mutates the ring. Returned states/layers borrow it and expire at the
// next mutation. Request validates before replacing the next ring entry;
// same-resource requests without fresh preserve its clock. Advance updates
// signed clip clocks and unscaled blend clocks, retires obscured older
// controllers, then publishes ordered channels. finish_frame removes
// auto-retiring completed entries after pose/event consumption. clear is
// explicit; no implicit return-to-idle/action policy. Signed unwrapped and
// sampled previous/current times remain distinct so callers can decide
// root/event wrap semantics. Event delivery is not inferred here. Capacity
// storage is allocated at construction. Request/update cost O(32*N), with no
// steady-state allocation or archive lookup. Invalid requests/deltas leave
// state intact. Pool admission reports overwritten state for receiver teardown;
// generation identifies reuse. Layers order oldest-to-newest and channels
// ascend. Bank ownership belongs to the caller's PoseLayers assembly. Both
// phases: Core I.1 (#ri-explicit), E.4
// (#re-design-invariants) expose phases and failure guarantees; performance
// MEM.9 (guidelines/memory/MEM.9-embedded-allocate-at-init.md) allocates fixed
// storage. Source 5e47a0/5e1a00/41e440/423b40 and pinned Unreal ownership
// comparison are recorded in alicia-animation-layers.md. This is not a gameplay
// state machine.
#pragma once

#include "pose.hpp"

namespace bw::animation {
struct ClipPlaybackRequest {
  const PreparedClip* clip{};
  std::size_t binding{};
  std::uint32_t channel{};
  double start_seconds{}, blend_seconds{};
  float rate{1};
  bool fresh{}, remove_on_completion{};
};
struct ClipPlaybackState {
  const PreparedClip* clip{};
  std::size_t binding{};
  std::uint64_t generation{};
  std::uint32_t channel{};
  double previous_seconds{}, seconds{};
  double previous_unwrapped_seconds{}, unwrapped_seconds{};
  float rate{1}, weight{1};
  bool linked{}, completed{}, remove_on_completion{};
};
struct PlaybackAdmission {
  std::size_t slot{};
  bool started{};
  std::optional<ClipPlaybackState> replaced;
};
class PlaybackRing {
 public:
  PlaybackRing(std::size_t capacity,
               const std::shared_ptr<memory::Domain>& domain);
  PlaybackRing(const PlaybackRing&) = delete;
  PlaybackRing& operator=(const PlaybackRing&) = delete;
  PlaybackRing(PlaybackRing&&) noexcept = default;
  PlaybackRing& operator=(PlaybackRing&&) noexcept = default;
  ~PlaybackRing() = default;
  PlaybackAdmission request(const ClipPlaybackRequest& request);
  void advance(double seconds);
  void finish_frame();
  void clear();
  void set_rate(std::size_t slot, float rate);
  std::size_t capacity() const { return entries_.size(); }
  std::optional<std::size_t> current_slot() const { return current_; }
  const ClipPlaybackState& state(std::size_t slot) const {
    return entries_.at(slot).state;
  }
  std::span<const PoseLayer> layers() const {
    return std::span<const PoseLayer>(layers_).first(layer_count_);
  }

 private:
  static constexpr std::size_t kChannelCount = 32;
  struct Entry {
    ClipPlaybackState state;
    double blend_remaining{}, blend_duration{};
    float initial_weight{1};
  };
  void retire_obscured();
  void publish_layers();
  memory::Vector<Entry> entries_;
  memory::Vector<PoseContribution> contributions_;
  std::array<PoseLayer, kChannelCount> layers_{};
  std::size_t layer_count_{};
  std::optional<std::size_t> current_;
  std::uint64_t generation_{};
};
}  // namespace bw::animation
