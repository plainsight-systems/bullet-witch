// Unit test: public contracts of bw/rendering/rhi/lifetime.hpp, one scenario
// per CTest entry. Each scenario names the header declaration it asserts.
#include "bw/rendering/rhi/lifetime.hpp"

#include "bw/rendering/rhi/descriptor_allocator.hpp"
#include "bw/rendering/rhi/resources.hpp"
#include "bw/testing/harness.hpp"
#include "test_objects.hpp"
#include "unwrap.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <utility>

namespace {
using bw::rhi::DeferredDeleteQueue;
using bw::rhi::FrameProgress;
using bw::rhi::Ref;
using bw::testing::require;
using bw_rhi_test::DestructionLog;
using Scenario = bw::testing::Scenario<void>;
using bw_rhi_test::TestResource;

// (lifetime.hpp, Resource) and (lifetime.hpp, Resource::release): the first
// Ref takes the count to one, a copy (construction or assignment) adds one,
// self-assignment adds none, a move adds none; the release that takes it to
// zero enqueues the object and does not destroy it. (lifetime.hpp, Ref): a
// null Ref owns nothing. (lifetime.hpp, DeferredDeleteQueue::destroy_completed):
// destroy_completed destroys it and returns how many.
void ref_count_and_release_enqueues() {
  DeferredDeleteQueue queue;
  DestructionLog log;
  Ref<TestResource> first = bw_rhi_test::make<TestResource>(queue, log, 1);
  require(first->ref_count() == 1U, "the first Ref takes the count to one");
  require(&first->delete_queue() == &queue, "the resource names its queue");
  Ref<TestResource> copy = first;
  require(first->ref_count() == 2U && copy == first, "a copy adds a reference");
  copy.reset();
  require(first->ref_count() == 1U && !copy, "releasing the copy releases one");
  Ref<TestResource> assigned;
  assigned = first;
  require(first->ref_count() == 2U && assigned == first, "copy assignment adds a reference");
  Ref<TestResource>& alias = assigned;
  assigned = alias;
  require(first->ref_count() == 2U && assigned == first,
          "self-assignment leaves the count unchanged");
  assigned = Ref<TestResource>{};
  require(first->ref_count() == 1U && !assigned, "assigning null releases one");
  Ref<TestResource> moved = std::move(first);
  require(moved->ref_count() == 1U, "a move adds no reference");
  const Ref<TestResource> none;
  require(!none && none.get() == nullptr, "a null Ref owns nothing");

  moved.reset();
  require(queue.pending() == 1U, "the release to zero enqueues the object");
  require(log.count == 0U, "the release does not destroy it");
  require(queue.destroy_completed(queue.current_frame()) == 1U, "destroy_completed returns 1");
  require(log.destroyed(1) && queue.pending() == 0U, "the queue destroyed it");
}

// (lifetime.hpp, Resource::release),
// (lifetime.hpp, DeferredDeleteQueue::destroy_completed) and
// (lifetime.hpp, DeferredDeleteQueue::drain): an entry is tagged with the
// queue's current frame; destroy_completed(frame) leaves entries tagged
// above frame untouched and destroys those at or below; drain destroys
// everything.
void nothing_destroyed_before_completion() {
  DeferredDeleteQueue queue;
  DestructionLog log;
  queue.set_current_frame(3);
  Ref<TestResource> at_three = bw_rhi_test::make<TestResource>(queue, log, 3);
  at_three.reset();
  queue.set_current_frame(4);
  Ref<TestResource> at_four = bw_rhi_test::make<TestResource>(queue, log, 4);
  at_four.reset();
  require(queue.pending() == 2U, "both entries wait");

  require(queue.destroy_completed(2) == 0U, "nothing tagged at or below frame 2");
  require(log.count == 0U, "nothing destroyed before its frame completed");
  require(queue.destroy_completed(3) == 1U, "frame 3 completed destroys its entry");
  require(log.destroyed(3) && !log.destroyed(4), "the frame 4 entry is untouched");
  require(queue.pending() == 1U, "one entry still waits");
  require(queue.drain() == 1U && log.destroyed(4), "drain destroys everything");
}

// (lifetime.hpp, DeferredDeleteQueue::destroy_completed): destruction
// between entries of one frame is enqueue order, not the order in which Refs
// were taken.
void destruction_order_is_enqueue_order() {
  DeferredDeleteQueue queue;
  DestructionLog log;
  Ref<TestResource> a = bw_rhi_test::make<TestResource>(queue, log, 1);
  Ref<TestResource> b = bw_rhi_test::make<TestResource>(queue, log, 2);
  Ref<TestResource> c = bw_rhi_test::make<TestResource>(queue, log, 3);
  b.reset();
  c.reset();
  a.reset();
  require(queue.destroy_completed(queue.current_frame()) == 3U, "three destroyed");
  require(log.count == 3U && log.ids[0] == 2 && log.ids[1] == 3 && log.ids[2] == 1,
          "destroyed in enqueue order");
}

// (lifetime.hpp, Resource): a resource outlives every view that holds it;
// (resources.hpp, View): a view holds a Ref to its resource. The
// resource is not enqueued while the view lives, and is destroyed only after
// the view, by a destroy_completed that covers its own tag.
void view_keeps_resource_alive() {
  // The pools outlive the queue, whose destruction may still destroy a view.
  bw::rhi::DescriptorAllocators pools(
      bw::rhi::DescriptorCapacities{8, 1, bw::shaders::kStorageDescriptors});
  DeferredDeleteQueue queue;
  DestructionLog log;
  const bw::rhi::DescriptorSlot slot =
      bw_rhi_test::unwrap(pools.allocate(bw::shaders::DescriptorArray::Buffer), "a buffer slot");
  const bw::rhi::BufferDescription description = bw_rhi_test::shader_buffer_description("Buffer");
  Ref<bw::rhi::Viewable> buffer =
      bw_rhi_test::make<bw_rhi_test::TestBuffer>(queue, description, &log, 1);
  Ref<bw_rhi_test::TestShaderResourceView> view =
      bw_rhi_test::make<bw_rhi_test::TestShaderResourceView>(queue, buffer, slot, pools, log, 2);
  require(view->resource() == buffer, "the view holds the resource");
  require(view->slot() == slot, "the view keeps its slot");
  buffer.reset();
  require(queue.pending() == 0U, "the resource is not enqueued while the view holds it");

  view.reset();
  require(queue.pending() == 1U, "the view is enqueued");
  queue.set_current_frame(5);
  require(queue.destroy_completed(4) == 1U, "the view's frame completed");
  require(log.destroyed(2) && !log.destroyed(1), "the view is destroyed, the resource lives");
  require(queue.pending() == 1U, "destroying the view enqueued the resource at frame 5");
  require(queue.destroy_completed(5) == 1U && log.destroyed(1),
          "the resource is destroyed once its own frame completes");
  require(log.ids[0] == 2 && log.ids[1] == 1, "the view before the resource");
}

// (lifetime.hpp, required_completion): with N frames in flight and completed C,
// a wait is required exactly when next - C - 1 >= N, for frame next - N.
void frames_in_flight_wait_exactly_at_bound() {
  require(!bw::rhi::required_completion(FrameProgress{1, 0}, 3).has_value(),
          "nothing queued before frame 1");
  require(!bw::rhi::required_completion(FrameProgress{3, 0}, 3).has_value(),
          "two queued is under the bound");
  require(bw::rhi::required_completion(FrameProgress{4, 0}, 3) == std::optional<std::uint64_t>{1},
          "three queued waits for frame 1");
  require(!bw::rhi::required_completion(FrameProgress{4, 1}, 3).has_value(),
          "after frame 1 completes two are queued");
  require(bw::rhi::required_completion(FrameProgress{10, 2}, 3) == std::optional<std::uint64_t>{7},
          "seven queued waits for frame next - N");
  require(bw::rhi::required_completion(FrameProgress{2, 0}, 1) == std::optional<std::uint64_t>{1},
          "one frame in flight waits for the previous frame");
  require(!bw::rhi::required_completion(FrameProgress{2, 1}, 1).has_value(),
          "one frame in flight with the previous complete");
  require(bw::rhi::kDefaultFramesInFlight == 3U, "the default bound is 3");
}

// (lifetime.hpp, DeferredDeleteQueue::~DeferredDeleteQueue): "a non-empty
// queue at destruction ... the remaining entries are destroyed then"; entries
// never destroyed by destroy_completed or drain are destroyed with the queue.
void queue_destroys_remaining_at_destruction() {
  DestructionLog log;
  {
    DeferredDeleteQueue queue;
    queue.set_current_frame(9);
    Ref<TestResource> left = bw_rhi_test::make<TestResource>(queue, log, 1);
    left.reset();
    require(queue.pending() == 1U && log.count == 0U, "one entry waits");
  }
  require(log.destroyed(1), "the queue's destruction destroyed the remaining entry");
}

// (lifetime.hpp, DeferredDeleteQueue::destroy_completed): every entry
// tagged at or below the frame
// is destroyed, including one a destructor enqueues during the call (a view
// releasing the last Ref to its resource) when its tag is at or below the
// frame; destruction is in enqueue order.
void enqueue_during_destroy_at_or_below_frame_is_destroyed() {
  bw::rhi::DescriptorAllocators pools(
      bw::rhi::DescriptorCapacities{4, 1, bw::shaders::kStorageDescriptors});
  DeferredDeleteQueue queue;
  DestructionLog log;
  Ref<bw::rhi::Viewable> buffer = bw_rhi_test::make<bw_rhi_test::TestBuffer>(
      queue, bw_rhi_test::shader_buffer_description("Held"), &log, 1);
  const bw::rhi::DescriptorSlot slot =
      bw_rhi_test::unwrap(pools.allocate(bw::shaders::DescriptorArray::Buffer), "a buffer slot");
  Ref<bw_rhi_test::TestShaderResourceView> view =
      bw_rhi_test::make<bw_rhi_test::TestShaderResourceView>(queue, buffer, slot, pools, log, 2);
  buffer.reset();
  view.reset();
  require(queue.pending() == 1U, "only the view is enqueued");
  require(queue.destroy_completed(queue.current_frame()) == 2U,
          "the view and the resource its destructor enqueued are both destroyed");
  require(log.count == 2U && log.ids[0] == 2 && log.ids[1] == 1, "view first, then resource");
  require(queue.pending() == 0U, "nothing remains");
}

constexpr std::array scenarios{
    Scenario{"ref_count_and_release_enqueues", ref_count_and_release_enqueues},
    Scenario{"nothing_destroyed_before_completion", nothing_destroyed_before_completion},
    Scenario{"destruction_order_is_enqueue_order", destruction_order_is_enqueue_order},
    Scenario{"view_keeps_resource_alive", view_keeps_resource_alive},
    Scenario{"frames_in_flight_wait_exactly_at_bound", frames_in_flight_wait_exactly_at_bound},
    Scenario{"queue_destroys_remaining_at_destruction", queue_destroys_remaining_at_destruction},
    Scenario{"enqueue_during_destroy_at_or_below_frame_is_destroyed",
             enqueue_during_destroy_at_or_below_frame_is_destroyed},
};
} // namespace

int main(int argc, char** argv) { return bw::testing::run_scenario(argc, argv, scenarios); }
