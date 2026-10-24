// Game motion-event interpretation, separate from engine crossing/pose clocks.
// Prepare typed commands once from immutable decoded events; borrowed timing
// records and bytes must outlive this owner, its timeline and receiver.
// Receiver and sink have stable addresses, one owner thread, no reentrant
// dispatch. Ordinary binding deliberately leaves interrupt/carry channels
// unbound. Enter guards and face/cancellation latches follow the recovered
// receivers; listener visibility targets are not mesh indices. Sink owns
// effects and must report unsupported consumers, preserve listener-first
// routing and reset its scoped visibility/facial state. No throwing callbacks
// or implicit teardown calls. Setup is O(records), one domain vector; dispatch
// O(1), no allocation. Invalid packed targets reject during preparation before
// publication. Core I.11
// (#ri-raw) keeps borrowed receiver/catalog lifetimes explicit; Performance
// CDSA.32 (guidelines/cpu-dsa/CDSA.32-transform-static-data-into-the-consumer-
// layout.md) prepares typed payloads once. Both design corpora consulted.
// Source contracts and pinned reference: motion-event-timeline.md.
#pragma once

#include <variant>

#include "bw/animation/event_timeline.hpp"

namespace bw::game {
struct FacialMotionCommand {
  std::int32_t expression{}, bank{};
  float weight{};
  std::int32_t duration{};
};
struct VisibilityMotionCommand {
  std::int32_t target{};
  bool enabled{};
};
struct SoundMotionCommand {
  std::uint32_t action{};
  std::int32_t bone{};
};
struct VoiceMotionCommand {
  std::uint32_t identifier{};
};
struct CancellationMotionCommand {};
struct UnsupportedMotionCommand {};
using MotionPresentationCommand =
    std::variant<UnsupportedMotionCommand, CancellationMotionCommand,
                 VisibilityMotionCommand, FacialMotionCommand,
                 VoiceMotionCommand, SoundMotionCommand>;
class PreparedMotionPresentation {
 public:
  PreparedMotionPresentation(const formats::MotionEvents& events,
                             const std::shared_ptr<memory::Domain>& domain);
  formats::MotionEventChannels ordinary_channels() const;
  // Only matching channel/record identities from ordinary_channels are valid.
  const MotionPresentationCommand& command(std::size_t channel,
                                           std::size_t record) const noexcept;

 private:
  formats::MotionEventChannels channels_;
  memory::Vector<MotionPresentationCommand> commands_;
  std::array<std::size_t, formats::kMotionEventChannels> offsets_{};
};
class MotionPresentationSink {
 public:
  MotionPresentationSink() = default;
  MotionPresentationSink(const MotionPresentationSink&) = delete;
  MotionPresentationSink& operator=(const MotionPresentationSink&) = delete;
  MotionPresentationSink(MotionPresentationSink&&) = delete;
  MotionPresentationSink& operator=(MotionPresentationSink&&) = delete;
  virtual ~MotionPresentationSink() = default;
  virtual void visibility(VisibilityMotionCommand command) noexcept = 0;
  virtual void facial(FacialMotionCommand command) noexcept = 0;
  virtual void voice(VoiceMotionCommand command) noexcept = 0;
  virtual void sound(SoundMotionCommand command,
                     animation::MotionEventPhase phase,
                     const animation::MotionEventContext& context) noexcept = 0;
  virtual void unsupported(
      animation::MotionEventPhase phase,
      const animation::MotionEventContext& context) noexcept = 0;
  virtual void reset_presentation() noexcept = 0;
};
class MotionPresentationReceiver final : public animation::MotionEventReceiver {
 public:
  MotionPresentationReceiver(const PreparedMotionPresentation& prepared,
                             MotionPresentationSink& sink)
      : prepared_(prepared), sink_(sink) {}
  void receive(animation::MotionEventPhase phase,
               const animation::MotionEventContext& context) noexcept override;
  void reset() noexcept override;
  bool cancellation_enabled() const { return cancellation_; }

 private:
  const PreparedMotionPresentation& prepared_;
  MotionPresentationSink& sink_;
  bool cancellation_{}, first_face_{true}, first_sound_{true};
};
}  // namespace bw::game
