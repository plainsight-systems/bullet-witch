#ifndef BW_RENDERING_RHI_SYNCHRONIZATION_HPP
#define BW_RENDERING_RHI_SYNCHRONIZATION_HPP
#include "bw/memory/alignment.hpp"
#include "bw/rendering/rhi/format.hpp"
#include "bw/rendering/rhi/lifetime.hpp"
#include "bw/rendering/rhi/resources.hpp"
#include "bw/rendering/rhi/result.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

// Responsibility: GPU fences the CPU can poll or wait on, staging buffers
// the CPU can read, and the readback contract (docs/architecture/rhi.md,
// invariant 9 and "Readback": a copy to a staging resource is paired with a
// GPU fence; a lock blocks only while that fence is unsignaled, and says
// so; readback never busy-waits).
//
// Contract.
//   GpuFence: created Unwritten. CommandList::write_fence records a signal
//   that the GPU performs when it reaches that point of the frame's work;
//   from the replay of that command the fence is Pending, from the
//   end_frame that submits that frame it is Submitted, and once the GPU has
//   performed the signal it is Signaled. poll() is state() == Signaled,
//   with no blocking; wait() returns after the GPU has performed the signal,
//   blocking the calling thread on the backend's completion primitive, never
//   spinning, and has the precondition state() is Submitted or Signaled.
//   Device::wait_fence and lock_staging check the state first
//   (fence_wait_error) and reject Unwritten with FenceNotWritten and Pending
//   with FenceNotSubmitted; both waits would otherwise never return. A fence with no write recorded
//   since creation or the last reset never signals. reset() returns the
//   fence to Unwritten; it
//   is called only when no write is pending.
//   StagingBuffer: a CPU-readable buffer of `size` bytes filled by
//   CommandList::copy_to_staging. Device::lock_staging(staging, fence,
//   policy) returns the bytes: with WaitPolicy::Block it waits on the fence
//   first and reports waited; with WaitPolicy::PollOnly it returns NotReady
//   when the fence is unsignaled and never blocks. The bytes are valid until
//   unlock_staging; a second lock before unlock is AlreadyLocked.
//   Texture readback: copy_to_staging of a texture subresource writes it
//   with the layout staging_layout gives: rows of staging_row_bytes(format,
//   width) bytes at a pitch rounded up to kStagingRowAlignment, slices at
//   row_pitch * rows, so a reader decodes without asking the backend. For a
//   color format that is row_bytes; for a depth or depth-stencil format the
//   depth plane alone is read back, 2 bytes per texel for D16Unorm and 4 for
//   the others (the stencil plane is not read back this slice; a reader
//   that needs it asks in a later slice). Every backend writes that layout;
//   Direct3D 12 requires the 256-byte row alignment and the others accept
//   it.
//
// Core guidelines read: C.35 and C.67 (through Resource), E.27 (NotReady
// and FenceNotWritten are named codes, not a hang), I.1 (the wait policy is
// an argument). Performance guidelines read: GPU.7 (a fence per readback,
// never a device-wide wait; the caller may delay consumption and poll),
// CONC.2 (the fence's completion is a release the poll acquires; the
// backend's primitive provides it).
namespace bw::rhi {

// The Metal backend's fence waits on a shared event by value.
enum class FenceState : std::uint8_t { Unwritten, Pending, Submitted, Signaled };

class GpuFence : public Resource {
public:
  [[nodiscard]] virtual FenceState state() const noexcept = 0;
  [[nodiscard]] bool poll() const noexcept { return state() == FenceState::Signaled; }
  // Blocks until signaled. Precondition: state() is Submitted or Signaled.
  virtual void wait() noexcept = 0;
  // Precondition: no write is pending.
  virtual void reset() noexcept = 0;
  [[nodiscard]] std::string_view debug_name() const noexcept { return name_.view(); }

protected:
  GpuFence(DeferredDeleteQueue& queue, std::string_view name) noexcept
      : Resource(queue), name_(name) {}

private:
  DebugName name_;
};

// A readback pairs a copy to the staging buffer with a fence write and locks
// through the fence. Texture readback goes through the same staging buffer
// and a texture-to-buffer copy with the stated layout, so one lock path
// serves both.
class StagingBuffer : public Resource {
public:
  [[nodiscard]] std::uint64_t size() const noexcept { return size_; }
  [[nodiscard]] std::string_view debug_name() const noexcept { return name_.view(); }

protected:
  StagingBuffer(DeferredDeleteQueue& queue, std::uint64_t size, std::string_view name) noexcept
      : Resource(queue), size_(size), name_(name) {}

private:
  std::uint64_t size_;
  DebugName name_;
};

enum class WaitPolicy : std::uint8_t { PollOnly, Block };

enum class StagingError : std::uint8_t {
  None,
  // PollOnly and the fence is unsignaled.
  NotReady,
  // The fence has had no write since creation or reset: waiting would never
  // return.
  FenceNotWritten,
  // The write's frame has not been submitted (no end_frame yet): waiting
  // would never return.
  FenceNotSubmitted,
  AlreadyLocked,
  NotLocked,
  RangeOutOfBounds,
};

// The wait precondition as a named code: Unwritten is FenceNotWritten,
// Pending is FenceNotSubmitted, Submitted and Signaled are None.
[[nodiscard]] constexpr StagingError fence_wait_error(FenceState state) noexcept {
  switch (state) {
  case FenceState::Unwritten:
    return StagingError::FenceNotWritten;
  case FenceState::Pending:
    return StagingError::FenceNotSubmitted;
  case FenceState::Submitted:
  case FenceState::Signaled:
    return StagingError::None;
  }
  return StagingError::None;
}

struct StagingLock {
  std::span<const std::byte> bytes{};
  // True when the lock blocked on the fence.
  bool waited{};
};
using StagingLockResult = Result<StagingLock, StagingError>;

// The layout a texture subresource has in a staging buffer.
inline constexpr std::uint32_t kStagingRowAlignment = 256;
struct StagingLayout {
  std::uint32_t row_pitch{};
  std::uint32_t slice_pitch{};
  std::uint64_t total_bytes{};
  std::uint32_t rows{};
  std::uint32_t width{};
  std::uint32_t height{};
  std::uint32_t depth{};
};
// Direct3D 12 requires texture data rows at a 256-byte pitch alignment; the
// alignment is applied on every backend so the layout is one contract.
// The bytes of one readback row: row_bytes for a color format, the depth
// plane's bytes for a depth or depth-stencil format (the contract above).
[[nodiscard]] constexpr std::uint32_t staging_row_bytes(Format format,
                                                        std::uint32_t width) noexcept {
  const FormatInfo info = format_info(format);
  if (!info.is_depth) {
    return row_bytes(format, width);
  }
  return (format == Format::D16Unorm ? 2U : 4U) * width;
}

[[nodiscard]] constexpr StagingLayout staging_layout(const TextureDescription& texture,
                                                     std::uint32_t mip) noexcept {
  StagingLayout layout{};
  layout.width = texture.extent.width >> mip;
  layout.height = texture.extent.height >> mip;
  layout.depth =
      texture.dimension == TextureDimension::Texture3D ? (texture.extent.depth >> mip) : 1U;
  layout.width = layout.width == 0U ? 1U : layout.width;
  layout.height = layout.height == 0U ? 1U : layout.height;
  layout.depth = layout.depth == 0U ? 1U : layout.depth;
  const FormatInfo info = format_info(texture.format);
  layout.rows =
      info.block_height == 0U ? 0U : (layout.height + info.block_height - 1U) / info.block_height;
  layout.row_pitch =
      bw::memory::align_up(staging_row_bytes(texture.format, layout.width), kStagingRowAlignment);
  layout.slice_pitch = layout.row_pitch * layout.rows;
  layout.total_bytes = static_cast<std::uint64_t>(layout.slice_pitch) * layout.depth;
  return layout;
}

using GpuFenceResult = Result<Ref<GpuFence>, ResourceError>;
using StagingBufferResult = Result<Ref<StagingBuffer>, ResourceError>;

} // namespace bw::rhi
#endif
