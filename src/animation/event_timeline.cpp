// Source-ordered event traversal; public lifetime, clock and receiver contracts
// are in event_timeline.hpp. Game effects are synchronous external receivers.
// Implementation: Core SL.con.3 (#rsl-bounds) bounds fixed state from validated
// channel spans; Performance CDSA.32 (guidelines/cpu-dsa/
// CDSA.32-transform-static-data-into-the-consumer-layout.md) consumes prepared
// contiguous timing, never parses payloads during updates. I.1/MEM.9 design
// rules remain applicable; both corpora consulted for both phases.
#include "bw/animation/event_timeline.hpp"

#include <cmath>
#include <limits>
#include <stdexcept>

namespace bw::animation {
namespace {
void validate_frame(float frame, float minimum) {
  if (!std::isfinite(frame) || frame < minimum ||
      static_cast<double>(frame) >
          static_cast<double>(std::numeric_limits<std::int32_t>::max())) {
    throw std::invalid_argument(
        "Motion event frame exceeds signed source clock");
  }
}
}  // namespace
EventTimeline::EventTimeline(formats::MotionEventChannels channels,
                             float initial_frame)
    : channels_(channels), current_frame_(initial_frame) {
  validate_frame(initial_frame, -1);
  for (const auto& channel : channels_) {
    if (channel.records.size() > formats::kMotionEventsPerChannel) {
      throw std::invalid_argument(
          "Motion event channel exceeds fixed capacity");
    }
  }
}
void EventTimeline::advance(float frame, MotionEventReceiver& receiver) {
  validate_frame(frame, 0);
  previous_frame_ = current_frame_;
  current_frame_ = frame;
  const auto previous = static_cast<std::int32_t>(previous_frame_);
  const auto current = static_cast<std::int32_t>(current_frame_);
  if (previous < 0) return;
  const bool wrapped = current < previous;
  for (std::size_t channel = 0; channel < channels_.size(); ++channel) {
    const auto& input = channels_.at(channel);
    auto& active = active_.at(channel);
    // Source wrap ends old intervals before selecting either side of the seam.
    if (wrapped) {
      for (std::size_t record = 0; record < input.records.size(); ++record) {
        if (!active.at(record)) continue;
        receiver.receive(
            MotionEventPhase::exit,
            {channel, record, input.tag, input.records[record],
             static_cast<float>(previous), static_cast<float>(current)});
        active.at(record) = false;
      }
    }
    for (std::size_t record = 0; record < input.records.size(); ++record) {
      const auto& event = input.records[record];
      const MotionEventContext context{channel,
                                       record,
                                       input.tag,
                                       event,
                                       static_cast<float>(previous),
                                       static_cast<float>(current)};
      const bool enters =
          wrapped
              ? (event.start_frame >= 0 && event.start_frame <= current) ||
                    previous < event.start_frame
              : previous < event.start_frame && event.start_frame <= current;
      if (enters) {
        receiver.receive(MotionEventPhase::enter, context);
        active.at(record) = true;
      }
      if (!active.at(record)) continue;
      receiver.receive(MotionEventPhase::update, context);
      if (wrapped || event.end_frame == -1 || event.end_frame <= current) {
        receiver.receive(MotionEventPhase::exit, context);
        active.at(record) = false;
      }
    }
  }
}
void EventTimeline::interrupt(MotionEventReceiver& receiver) noexcept {
  // Interruption has its own receiver operation and retains untruncated clocks.
  for (std::size_t channel = 0; channel < channels_.size(); ++channel) {
    const auto& input = std::span{channels_}[channel];
    auto& active = std::span{active_}[channel];
    for (std::size_t record = 0; record < input.records.size(); ++record) {
      if (!std::span{active}[record]) continue;
      receiver.receive(MotionEventPhase::interrupt,
                       {channel, record, input.tag, input.records[record],
                        previous_frame_, current_frame_});
      std::span{active}[record] = false;
    }
  }
}
void EventTimeline::reset(MotionEventReceiver& receiver, float initial_frame) {
  validate_frame(initial_frame, -1);
  interrupt(receiver);
  receiver.reset();
  previous_frame_ = -1;
  current_frame_ = initial_frame;
}
}  // namespace bw::animation
