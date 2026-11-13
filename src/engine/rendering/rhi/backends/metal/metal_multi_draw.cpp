#include "metal_multi_draw.hpp"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace bw::rhi::metal {

namespace {

// Private and hazard-tracked: only the GPU writes and reads them, and
// tracking orders the kernel's writes in one command buffer before the
// render encoder's reads in the next, as observed (metal_context.cpp,
// draw_indexed_indirect_count).
[[nodiscard]] MTL::ResourceOptions private_tracked_options() noexcept {
  return static_cast<MTL::ResourceOptions>(MTL::ResourceStorageModePrivate |
                                           MTL::ResourceHazardTrackingModeTracked);
}

} // namespace

// The library is made from the embedded bytes as the backend makes a
// shader's (metal_device.cpp, load_function), then the kernel's function and
// its compute pipeline.
std::unique_ptr<MultiDrawPool>
MultiDrawPool::create(MTL::Device& device, std::uint32_t frames_in_flight, std::string& error) {
  const AutoreleaseScope pool;
  const std::span<const unsigned char> bytes = multi_draw_encode_library();
  dispatch_data_t data =
      dispatch_data_create(bytes.data(), bytes.size(), nullptr, DISPATCH_DATA_DESTRUCTOR_DEFAULT);
  NS::Error* library_error = nullptr;
  MTL::Library* library = device.newLibrary(data, &library_error);
  dispatch_release(data);
  if (library == nullptr) {
    error =
        "the multi-draw encode library: " + error_text(library_error, "newLibrary returned none");
    return nullptr;
  }
  MTL::Function* function = library->newFunction(make_string("encode_multi_draw"));
  library->release();
  if (function == nullptr) {
    error = "the multi-draw encode library has no function named 'encode_multi_draw'";
    return nullptr;
  }
  NS::Error* pipeline_error = nullptr;
  MTL::ComputePipelineState* pipeline = device.newComputePipelineState(function, &pipeline_error);
  function->release();
  if (pipeline == nullptr) {
    error = "the multi-draw encode pipeline: " +
            error_text(pipeline_error, "newComputePipelineState returned none");
    return nullptr;
  }
  return std::unique_ptr<MultiDrawPool>(new MultiDrawPool(device, pipeline, frames_in_flight));
}

MultiDrawPool::MultiDrawPool(MTL::Device& device, MTL::ComputePipelineState* pipeline,
                             std::uint32_t frames_in_flight)
    : device_(&device), pipeline_(pipeline), slots_(frames_in_flight) {}

MultiDrawPool::~MultiDrawPool() {
  for (const FrameSlot& slot : slots_) {
    for (const Entry& entry : slot.entries) {
      entry.commands->release();
      entry.ranges->release();
      entry.copies->release();
    }
  }
  pipeline_->release();
}

// Indexed draw commands that inherit the encoder's pipeline and set their own
// buffers: the two descriptor heaps and the command's own copy of the
// top-level argument buffer, at bind points 0 to 2 of both stages, so a
// converter-built pipeline reads its draw's index from the copy's root
// constants.
// With inheritBuffers false the commands ignore the encoder's buffers, and
// the descriptor's maximum vertex and fragment buffer bind counts bound the
// buffers a command may set. Making the indirect command buffer gives nil
// on failure, and its size is fixed at creation.
MultiDrawPool::Entry MultiDrawPool::create_entry(std::uint32_t maximum) const {
  const AutoreleaseScope pool;
  const std::uint32_t capacity = std::bit_ceil(maximum);
  const ScopedRelease<MTL::IndirectCommandBufferDescriptor> descriptor{
      MTL::IndirectCommandBufferDescriptor::alloc()->init()};
  descriptor->setCommandTypes(MTL::IndirectCommandTypeDrawIndexed);
  descriptor->setInheritBuffers(false);
  descriptor->setInheritPipelineState(true);
  descriptor->setMaxVertexBufferBindCount(kArgumentBufferBindPoint + 1U);
  descriptor->setMaxFragmentBufferBindCount(kArgumentBufferBindPoint + 1U);
  MTL::IndirectCommandBuffer* commands =
      device_->newIndirectCommandBuffer(descriptor.get(), capacity, private_tracked_options());
  if (commands == nullptr) {
    return Entry{nullptr, nullptr, nullptr, 0, "indirect command buffer"};
  }
  const std::uint64_t range_count = (capacity + kRangeCommands - 1U) / kRangeCommands;
  MTL::Buffer* ranges =
      device_->newBuffer(range_count * 2U * sizeof(std::uint32_t), private_tracked_options());
  if (ranges == nullptr) {
    commands->release();
    return Entry{nullptr, nullptr, nullptr, 0, "execution range buffer"};
  }
  MTL::Buffer* copies = device_->newBuffer(
      static_cast<std::uint64_t>(capacity) * kArgumentCopyStride, private_tracked_options());
  if (copies == nullptr) {
    ranges->release();
    commands->release();
    return Entry{nullptr, nullptr, nullptr, 0, "argument copies buffer"};
  }
  set_label(*commands, "bw.rhi.metal.multi_draw_commands");
  set_label(*ranges, "bw.rhi.metal.multi_draw_ranges");
  set_label(*copies, "bw.rhi.metal.multi_draw_arguments");
  return Entry{commands, ranges, copies, capacity, nullptr};
}

// The frame's slot hands out its entries in order. An entry too small for
// `maximum` is replaced in place: its slot's last frame has completed
// (reset), so no GPU work still reads it.
// The signature is the header's: a size and a frame number.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
MultiDrawPool::Slot MultiDrawPool::acquire(std::uint32_t maximum, std::uint64_t frame) {
  FrameSlot& slot = slots_[frame % slots_.size()];
  slot.last_frame = frame;
  if (slot.used < slot.entries.size() && slot.entries[slot.used].capacity >= maximum) {
    const Entry& entry = slot.entries[slot.used];
    ++slot.used;
    return Slot{entry.commands, entry.ranges, entry.copies, nullptr};
  }
  const Entry created = create_entry(maximum);
  if (created.commands == nullptr) {
    return Slot{nullptr, nullptr, nullptr, created.refused};
  }
  if (slot.used < slot.entries.size()) {
    Entry& replaced = slot.entries[slot.used];
    replaced.commands->release();
    replaced.ranges->release();
    replaced.copies->release();
    replaced = created;
  } else {
    slot.entries.push_back(created);
  }
  ++slot.used;
  return Slot{created.commands, created.ranges, created.copies, nullptr};
}

void MultiDrawPool::reset(std::uint64_t completed_frame) noexcept {
  for (FrameSlot& slot : slots_) {
    if (slot.last_frame <= completed_frame) {
      slot.used = 0;
    }
  }
}

} // namespace bw::rhi::metal
