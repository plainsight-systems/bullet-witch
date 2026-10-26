// Decode game receiver payloads once, then dispatch source entry guards and
// latches without parsing or allocation. See header for sink/lifetime contract.
// Core SL.con.3 (#rsl-bounds) validates payload fields before publication;
// Performance CDSA.32 (guidelines/cpu-dsa/CDSA.32-transform-static-data-into-
// the-consumer-layout.md) keeps prepared command access constant-time.
// Both implementation corpora consulted; original source is recorded in
// research/engine/subsystems/motion-event-timeline.md.
#include "motion_presentation.hpp"

#include <bit>
#include <limits>

#include "bw/content/formats/binary_reader.hpp"

namespace bw::game {
namespace {
constexpr std::size_t kSoundChannel = 0, kVoiceChannel = 1, kFaceChannel = 3,
                      kCancelChannel = 4, kInterruptChannel = 5,
                      kCarryChannel = 6, kVisibilityChannel = 8;
constexpr std::uint64_t kPayloadValue = 32, kVisibilityValue = 36;
constexpr std::int32_t kExpressionDivisor = 10000000, kWeightDivisor = 10000,
                       kFaceBank = 1, kEyeBank = 2;
constexpr float kWeightScale = 100;
MotionPresentationCommand prepare(std::size_t channel,
                                  const formats::MotionEvent& event) {
  const resources::detail::Reader reader{event.payload};
  switch (channel) {
    case kSoundChannel:
      return SoundMotionCommand{
          reader.u32(kPayloadValue),
          std::bit_cast<std::int32_t>(reader.u32(kVisibilityValue))};
    case kCancelChannel:
      return CancellationMotionCommand{};
    case kVisibilityChannel: {
      const auto value =
          std::bit_cast<std::int32_t>(reader.u32(kVisibilityValue));
      if (value == std::numeric_limits<std::int32_t>::min()) {
        resources::detail::malformed("Visibility target magnitude overflows");
      }
      return VisibilityMotionCommand{value > 0 ? value : -value, value > 0};
    }
    case kFaceChannel: {
      const auto value = std::bit_cast<std::int32_t>(reader.u32(kPayloadValue));
      const auto expression = value / kExpressionDivisor - 1;
      // Packed decimal weight truncates to an integer before scaling.
      const auto weight_percent = (value % kExpressionDivisor) / kWeightDivisor;
      return FacialMotionCommand{
          expression, expression == 0 || expression == 1 ? kEyeBank : kFaceBank,
          static_cast<float>(weight_percent) / kWeightScale,
          value % kWeightDivisor};
    }
    case kVoiceChannel:
      return VoiceMotionCommand{reader.u32(kPayloadValue)};
    default:
      return UnsupportedMotionCommand{};
  }
}
}  // namespace
PreparedMotionPresentation::PreparedMotionPresentation(
    const formats::MotionEvents& events,
    const std::shared_ptr<memory::Domain>& domain)
    : channels_(events.channels()),
      commands_(memory::Allocator<MotionPresentationCommand>(domain)) {
  std::size_t count = 0;
  for (const auto& channel : channels_) count += channel.records.size();
  commands_.reserve(count);
  for (std::size_t channel = 0; channel < channels_.size(); ++channel) {
    offsets_.at(channel) = commands_.size();
    for (const auto& event : channels_.at(channel).records) {
      commands_.push_back(prepare(channel, event));
    }
  }
}
formats::MotionEventChannels PreparedMotionPresentation::ordinary_channels()
    const {
  auto result = channels_;
  result.at(kInterruptChannel).records = {};
  result.at(kCarryChannel).records = {};
  return result;
}
const MotionPresentationCommand& PreparedMotionPresentation::command(
    std::size_t channel, std::size_t record) const noexcept {
  if (channel >= channels_.size() ||
      record >= std::span{channels_}[channel].records.size()) {
    std::terminate();
  }
  return std::span{commands_}[std::span{offsets_}[channel] + record];
}
void MotionPresentationReceiver::receive(
    animation::MotionEventPhase phase,
    const animation::MotionEventContext& context) noexcept {
  const auto& command = prepared_.command(context.channel, context.record);
  if (std::holds_alternative<UnsupportedMotionCommand>(command)) {
    sink_.unsupported(phase, context);
    return;
  }
  if (const auto* sound = std::get_if<SoundMotionCommand>(&command)) {
    if (phase == animation::MotionEventPhase::enter) {
      if (context.event.start_frame == 0 && context.event.end_frame == 0 &&
          first_sound_) {
        first_sound_ = false;
      } else if (static_cast<float>(context.event.start_frame) <
                     context.previous_frame &&
                 static_cast<float>(context.event.end_frame) <
                     context.previous_frame) {
        return;
      }
    }
    sink_.sound(*sound, phase, context);
    return;
  }
  if (phase != animation::MotionEventPhase::enter) return;
  if (std::holds_alternative<CancellationMotionCommand>(command)) {
    cancellation_ = true;
  } else if (const auto* face = std::get_if<FacialMotionCommand>(&command)) {
    // The initial zero interval is allowed once; wrapped stale face events
    // otherwise retain the receiver's raw-float previous-clock guard.
    if (context.event.start_frame == 0 && context.event.end_frame == 0 &&
        first_face_) {
      first_face_ = false;
    } else if (static_cast<float>(context.event.start_frame) <
                   context.previous_frame &&
               static_cast<float>(context.event.end_frame) <
                   context.previous_frame) {
      return;
    }
    sink_.facial(*face);
  } else if (context.previous_frame <=
             static_cast<float>(context.event.start_frame)) {
    if (const auto* visibility =
            std::get_if<VisibilityMotionCommand>(&command)) {
      sink_.visibility(*visibility);
    } else if (const auto* voice = std::get_if<VoiceMotionCommand>(&command)) {
      sink_.voice(*voice);
    }
  }
}
void MotionPresentationReceiver::reset() noexcept {
  cancellation_ = false;
  first_face_ = true;
  first_sound_ = true;
  sink_.reset_presentation();
}
}  // namespace bw::game
