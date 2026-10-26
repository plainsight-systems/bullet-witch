// Public event timing/lifetime contracts exercised in process with bounded
// authored records. Source crossings, wrap order, repeated-frame updates,
// interruption/reset and malformed decode are separate from game effects.
// Core I.1/SL.con.3 and Performance MEM.9/CDSA.32 apply as documented by the
// production headers. Fixed receiver storage makes hot allocations observable.
#include <limits>
#include <memory_resource>

#include "../../../fixtures/model_fixture.hpp"
#include "bw/animation/event_timeline.hpp"
namespace {
void require(bool value, const char* message) {
  if (!value) throw std::runtime_error(message);
}
template <class Function>
void rejects(Function function) {
  bool rejected = false;
  try {
    function();
  } catch (const std::exception&) {
    rejected = true;
  }
  require(rejected, "Invalid event input must reject");
}
class Backing final : public bw::memory::CpuBacking {
 public:
  std::size_t calls{};
  void* allocate(std::size_t bytes, std::size_t alignment) noexcept override {
    try {
      ++calls;
      return std::pmr::new_delete_resource()->allocate(bytes, alignment);
    } catch (const std::bad_alloc&) {
      return nullptr;
    }
  }
  void deallocate(void* pointer, std::size_t bytes,
                  std::size_t alignment) noexcept override {
    std::pmr::new_delete_resource()->deallocate(pointer, bytes, alignment);
  }
};
struct Call {
  bw::animation::MotionEventPhase phase;
  std::size_t channel, record;
  float previous, current;
};
class Receiver final : public bw::animation::MotionEventReceiver {
 public:
  std::array<Call, 128> calls{};
  std::size_t count{}, resets{};
  bool cancellation{};
  void receive(
      bw::animation::MotionEventPhase phase,
      const bw::animation::MotionEventContext& context) noexcept override {
    if (count >= calls.size()) std::terminate();
    std::span{calls}[count++] = {phase, context.channel, context.record,
                                 context.previous_frame, context.current_frame};
    if (phase == bw::animation::MotionEventPhase::enter &&
        context.tag == bw::formats::kCancellationEventTag) {
      cancellation = true;
    }
  }
  void reset() noexcept override {
    ++resets;
    cancellation = false;
  }
  void clear() { count = 0; }
};
}  // namespace
void motion_event_tests() {
  namespace animation = bw::animation;
  namespace formats = bw::formats;
  namespace memory = bw::memory;
  namespace resources = bw::resources;
  using Phase = animation::MotionEventPhase;
  const auto backing = std::make_shared<Backing>();
  const auto domain = memory::Domain::create(backing);
  {
    resources::Bytes bytes(132);
    model_fixture::word(bytes, 0, 0x30415046);
    model_fixture::word(bytes, 20, 44);
    model_fixture::word(bytes, 44, formats::kCancellationEventTag);
    model_fixture::word(bytes, 48, 2);
    model_fixture::word(bytes, 52, 56);
    model_fixture::word(bytes, 88, 13);
    model_fixture::word(bytes, 124, 7);
    const auto decoded = formats::MotionEvents::parse(bytes, domain);
    const auto channels = decoded.channels();
    require(channels[4].records.size() == 2 &&
                channels[4].records[0].start_frame == 13 &&
                channels[4].records[1].start_frame == 7 &&
                channels[4].records[0].end_frame == -1 &&
                channels[4].records[0].payload.data() ==
                    std::span(bytes).subspan(56).data(),
            "Decode preserves source order and borrowed payload");
    Receiver receiver;
    animation::EventTimeline timeline(channels);
    const auto calls = backing->calls;
    timeline.advance(14, receiver);
    require(receiver.count == 6 && receiver.calls[0].record == 0 &&
                receiver.calls[3].record == 1 && receiver.cancellation,
            "Skipped instant events enter/update/exit in record order");
    timeline.reset(receiver);
    require(!receiver.cancellation && receiver.resets == 1,
            "Reset clears effect latch even after instant event exited");
    require(backing->calls == calls, "Timeline performs no domain allocations");
    auto bad = bytes;
    model_fixture::word(bad, 48, 129);
    rejects([&] { (void)formats::MotionEvents::parse(bad, domain); });
    bad = bytes;
    model_fixture::word(bad, 52, 100);
    rejects([&] { (void)formats::MotionEvents::parse(bad, domain); });
    bad = bytes;
    model_fixture::word(bad, 44, 0);
    rejects([&] { (void)formats::MotionEvents::parse(bad, domain); });
  }
  require(domain->snapshot().live_bytes == 0,
          "Prepared event metadata releases");
  const std::array<formats::MotionEvent, 3> events{
      {{8, 12, {}}, {1, 5, {}}, {0, -1, {}}}};
  formats::MotionEventChannels channels{};
  channels[0] = {1, events};
  animation::EventTimeline timeline(channels);
  Receiver receiver;
  timeline.advance(1.9F, receiver);
  require(receiver.count == 2 && receiver.calls[0].phase == Phase::enter &&
              receiver.calls[0].record == 1 && receiver.calls[0].current == 1,
          "Crossing truncates frame and excludes start zero from initial zero");
  receiver.clear();
  timeline.advance(1.99F, receiver);
  require(receiver.count == 1 && receiver.calls[0].phase == Phase::update,
          "Repeated integer frame updates active interval without reentry");
  receiver.clear();
  timeline.advance(5, receiver);
  require(receiver.count == 2 && receiver.calls[1].phase == Phase::exit,
          "Interval exits inclusively at end");
  timeline.advance(9.25F, receiver);
  receiver.clear();
  timeline.interrupt(receiver);
  require(receiver.count == 1 && receiver.calls[0].phase == Phase::interrupt &&
              receiver.calls[0].current == 9.25F,
          "Interruption retains source float clock and distinct callback");
  timeline.reset(receiver, 7);
  timeline.advance(9, receiver);
  receiver.clear();
  timeline.advance(2, receiver);
  require(receiver.count == 7 && receiver.calls[0].phase == Phase::exit &&
              receiver.calls[0].record == 0 && receiver.calls[1].record == 1 &&
              receiver.calls[4].record == 2 &&
              receiver.calls[6].phase == Phase::exit,
          "Wrap exits active state then enters/updates/exits head records");
  timeline.reset(receiver, 6);
  receiver.clear();
  timeline.advance(2, receiver);
  require(receiver.count == 9 && receiver.calls[0].record == 0 &&
              receiver.calls[3].record == 1 && receiver.calls[6].record == 2,
          "Wrap tail/head events retain record order rather than chronological "
          "sort");
  receiver.clear();
  const auto before = timeline.current_frame();
  rejects([&] {
    timeline.advance(std::numeric_limits<float>::infinity(), receiver);
  });
  rejects([&] { timeline.reset(receiver, -2); });
  require(receiver.count == 0 && timeline.current_frame() == before,
          "Invalid clock rejects before callbacks and state changes");
  timeline.reset(receiver, -1);
  receiver.clear();
  timeline.advance(2, receiver);
  require(receiver.count == 0,
          "Negative source previous frame suppresses initial dispatch");
}
