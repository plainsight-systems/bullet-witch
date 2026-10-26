// Character motion ownership above the engine's reusable playback ring.
// Borrow one immutable character catalog and one stable presentation sink for
// this owner's lifetime. One game thread mutates it; costume replacement may
// share the stable owner but must not advance it while preparing an appearance.
// Resolve an explicit clip/binding/parameter request, retire the previous
// receiver's scoped effects, bind the new current-motion timeline, and retain
// outgoing clips only for pose blending. Advance every retained clip clock,
// consume model pose/shape output, then dispatch only the selected controller.
// Finish-frame retirement occurs after pose consumption. No action
// completion/return-to-idle policy is inferred; game action owners decide those
// transitions. Setup allocates the fixed ring. Selection and ticking allocate
// nothing and never parse archives. Invalid identities/timing reject before
// mutation; synchronous receiver callbacks must not throw or reenter. Explicit
// clear resets owned presentation effects before releasing borrowed parameter
// views. Destruction also clears effects, so the sink must outlive this owner.
// Both design corpora: Core I.11 (#ri-raw) distinguishes borrowed catalogs and
// sinks from stable owned clocks; MEM.9 (guidelines/memory/
// MEM.9-embedded-allocate-at-init.md) fixes storage before steady-state work.
// Original current-only event ownership: motion-event-timeline.md; controller
// phases and pinned Unreal comparison: alicia-animation-layers.md.
// Native selection changes apply prepared INT entry/exit blend overrides;
// preserve_phase bypasses these overrides after the actor resolves normalized
// destination time. This flag does not itself calculate that time.
#pragma once

#include "bw/animation/playback_ring.hpp"
#include "character_animations.hpp"

namespace bw::game {
struct CharacterMotionRequest {
  std::size_t clip{}, binding{};
  std::optional<std::size_t> parameter;
  std::uint32_t channel{};
  double start_seconds{}, blend_seconds{};
  float rate{1};
  bool fresh{}, remove_on_completion{}, preserve_phase{};
};
class CharacterMotionPlayer {
 public:
  CharacterMotionPlayer(const CharacterAnimations& catalog,
                        MotionPresentationSink& sink, std::size_t capacity,
                        const std::shared_ptr<memory::Domain>& domain);
  ~CharacterMotionPlayer();
  CharacterMotionPlayer(const CharacterMotionPlayer&) = delete;
  CharacterMotionPlayer& operator=(const CharacterMotionPlayer&) = delete;
  CharacterMotionPlayer(CharacterMotionPlayer&&) = delete;
  CharacterMotionPlayer& operator=(CharacterMotionPlayer&&) = delete;
  animation::PlaybackAdmission request(const CharacterMotionRequest& request);
  void advance(double seconds);
  // Source events follow model clock/pose/shape evaluation; emit once after
  // consumed positive ticks. A zero-time appearance publication emits nothing.
  void dispatch_events();
  // A derived upper owner clears all pose contributors when its
  // selected nonrepeat motion completes; receiver teardown follows model pose.
  bool retire_completed_owner();
  void finish_frame();
  void clear() noexcept;
  void set_rate(float rate);
  const animation::ClipPlaybackState* current() const;
  std::span<const animation::PoseLayer> layers() const {
    return playback_.layers();
  }
  bool cancellable() const;

 private:
  const CharacterAnimations& catalog_;
  MotionPresentationSink& sink_;
  animation::PlaybackRing playback_;
  const CharacterAnimations::Parameter* parameter_{};
  std::optional<MotionPresentationReceiver> receiver_;
  std::optional<animation::EventTimeline> timeline_;
  bool events_pending_{}, retirement_pending_{};
};
}  // namespace bw::game
