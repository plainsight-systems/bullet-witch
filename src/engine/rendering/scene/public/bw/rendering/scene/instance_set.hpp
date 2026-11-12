#ifndef BW_RENDERING_SCENE_INSTANCE_SET_HPP
#define BW_RENDERING_SCENE_INSTANCE_SET_HPP

#include "bw/math/matrix/matrix4.hpp"
#include "bw/rendering/graph/builder.hpp"
#include "bw/rendering/graph/declaration.hpp"
#include "bw/rendering/rhi/device.hpp"
#include "bw/rendering/rhi/resources.hpp"
#include "bw/rendering/rhi/result.hpp"
#include "bw/rendering/scene/instance.hpp"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

// Responsibility: the set of instances (docs/architecture/scene.md,
// invariants 1 to 4): entry and removal at any frame under ids that stay
// with an instance, the publisher's changes, the frame start that makes
// every previous transform the transform of the frame before, and the list
// of records that changed, which is all an upload copies. The set is plain
// memory and touches no device; declare_instance_upload is the one function
// that does.
// Changed records are a list of indices, each added once; a changed
// record is uploaded whole, through an upload that is work of the frame's
// graph. A stage holds a few thousand instances and they change at a load,
// so the upload is neither split across threads nor redone in full. The
// buffer neither grows nor shrinks: the capacity is the creator's and a
// full set fails by name. No publisher teleports an instance, so nothing
// overrides the previous transform for one frame.
//
// Contract.
//   1. A set is created with a capacity; it holds at most that many
//      instances and its records are the indices 0 to capacity - 1.
//   2. enter(description, id) makes an instance and gives its id: the
//      record at id.index holds the description's fields as instance.hpp
//      states them, kInstanceLive, previous_object_to_world equal to
//      object_to_world and both world boxes equal to world_box(geometry
//      bounds, transform, max_displacement); the record is marked changed.
//      A full set is SetFull and makes nothing.
//   3. An id names its instance until remove. move, set_flags and remove
//      with an id whose instance was removed, or with an invalid id, are
//      IdStale and change nothing; an index reused by a later enter has a
//      generation no earlier id carries.
//   4. move(id, transform) sets object_to_world and the current world box
//      and marks the record changed; the previous fields are not touched.
//      set_flags replaces the two flag bits a publisher states and marks
//      the record changed. remove value-initializes the record, so it is
//      not live, and marks it changed.
//   5. begin_frame() is the frame start: for every record marked changed by
//      a move since the frame start before, previous_object_to_world and
//      the previous world box become the current ones and the record is
//      marked changed again. A frame calls it once, before the frame's
//      publications.
//   6. changed() is the indices marked since the last clear_changed(),
//      ascending, each once. extent() is one past the highest index ever
//      live; records() is the records [0, extent()). cluster_capacity
//      (pipeline) is the sum of geometry.cluster_count over the live
//      instances with that pipeline index, and total_cluster_capacity()
//      that sum over every live instance with a pipeline index.
//   7. declare_instance_upload(device, set, buffer, graph) declares the
//      frame's upload. The record buffer is created on the first call,
//      byte addressed, capacity * kInstanceRecordBytes, with a raw view
//      whose index shaders read records through. Every call imports it
//      into `graph`, entering and leaving the frame readable by shaders,
//      sets buffer.handle to its handle in that frame and marks it as
//      history. When the changed list is not empty the call makes one
//      buffer holding the changed records in ascending index order and
//      declares one Copy pass that copies each run of consecutive indices
//      to its records' place, then clears the list; the pass runs whatever
//      reads the records, since they leave the frame. A frame in which
//      nothing changed makes no buffer and declares no pass.
//      The copies are commands of the frame: the CPU never writes the
//      record buffer, so a frame the device has not run yet reads the
//      records as its own and earlier frames' copies left them, whatever
//      later frames have declared.
//      Every pass of the frame whose shaders read the records declares
//      buffer.handle Sampled.
//      A device refusal (the record buffer, its view, the frame's buffer)
//      is returned by its ResourceError with the device's detail text; the
//      list is kept and nothing is declared.
//      An InstanceBuffer is neither copied nor moved: the pass borrows its
//      `record` member until the frame is executed, and the buffer outlives
//      that execution.
//   One thread publishes into a set, calls begin_frame and declares the
//   upload.
//
// Core guidelines read: I.1, E.27 (a named error per refusal, nothing
// changed on one), C.21, R.1 (the Refs own the buffer and view), F.20.
// Performance guidelines read: CDSA.3 (generation compared on every
// access), MEM.2 (fixed slots and a free list, no allocation after
// construction), GPU.1 (only changed records cross to the device).
namespace bw::scene {

enum class InstanceError : std::uint8_t {
  None,
  SetFull,
  IdStale,
};

class InstanceSet {
public:
  explicit InstanceSet(std::uint32_t capacity);

  [[nodiscard]] InstanceError enter(const InstanceDescription& description, InstanceId& id);
  [[nodiscard]] InstanceError move(InstanceId id, const math::Matrix4f& transform);
  [[nodiscard]] InstanceError set_flags(InstanceId id, std::uint32_t flags);
  [[nodiscard]] InstanceError remove(InstanceId id);

  void begin_frame();

  [[nodiscard]] std::uint32_t capacity() const noexcept;
  [[nodiscard]] std::uint32_t extent() const noexcept;
  [[nodiscard]] std::span<const InstanceRecord> records() const noexcept;
  [[nodiscard]] std::span<const std::uint32_t> changed() const noexcept;
  void clear_changed() noexcept;
  [[nodiscard]] std::uint32_t cluster_capacity(std::uint32_t pipeline) const noexcept;
  [[nodiscard]] std::uint32_t total_cluster_capacity() const noexcept;

private:
  std::vector<InstanceRecord> records_{};
  std::vector<std::uint32_t> generations_{};
  std::vector<std::uint32_t> cluster_counts_{};
  std::vector<std::uint32_t> free_{};
  std::vector<std::uint32_t> changed_{};
  // One bit of state per index: changed since the last clear, moved since
  // the last frame start.
  std::vector<std::uint8_t> marks_{};
  std::vector<std::uint32_t> pipeline_clusters_{};
  // Each instance's box in its mesh's space, as entered: move makes the
  // current world box from it (contract 4), which the record's world boxes
  // alone cannot give back under a rotation.
  std::vector<Box> local_bounds_{};
  std::uint32_t extent_{};
  std::uint32_t total_clusters_{};
};

// `count` consecutive records from record index `first`, read from the
// frame's upload buffer where the runs before it end.
struct InstanceCopyRun {
  std::uint32_t first{};
  std::uint32_t count{};
};

// The record buffer on the device, the view shaders read it through, and
// the upload of the frame declared last.
struct InstanceBuffer {
  // Records the frame's copies inside the Copy pass.
  struct Record {
    InstanceBuffer* buffer;
    void operator()(graph::RecordContext& context) const;
  };

  rhi::Ref<rhi::Buffer> buffer{};
  rhi::Ref<rhi::ShaderResourceView> view{};
  // The record buffer's handle in the frame declared last; a pass whose
  // shaders read the records declares it Sampled.
  graph::BufferHandle handle{};
  // That frame's changed records, its handle and the runs the pass copies;
  // empty when nothing changed. A replaced buffer goes through its Ref, so
  // a frame in flight keeps its own.
  rhi::Ref<rhi::Buffer> upload{};
  graph::BufferHandle upload_handle{};
  std::vector<InstanceCopyRun> runs{};
  Record record{this};

  InstanceBuffer() = default;
  InstanceBuffer(const InstanceBuffer&) = delete;
  InstanceBuffer& operator=(const InstanceBuffer&) = delete;
  InstanceBuffer(InstanceBuffer&&) = delete;
  InstanceBuffer& operator=(InstanceBuffer&&) = delete;
  ~InstanceBuffer() = default;
};

// `resource` names the refused buffer or view.
struct InstanceUploadFailure {
  rhi::ResourceError resource{rhi::ResourceError::None};
  std::string detail{};
  [[nodiscard]] bool ok() const noexcept { return resource == rhi::ResourceError::None; }
};

[[nodiscard]] InstanceUploadFailure declare_instance_upload(rhi::Device& device, InstanceSet& set,
                                                            InstanceBuffer& buffer,
                                                            graph::FrameGraph& graph);

} // namespace bw::scene

#endif
