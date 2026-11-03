// Unit test: public contracts of bw/rendering/rhi/synchronization.hpp, one
// scenario per CTest entry. Each scenario names the header declaration it asserts.
#include "bw/rendering/rhi/synchronization.hpp"

#include "test_main.hpp"

#include <array>

namespace {
using bw::rhi::Format;
using bw::rhi::TextureDescription;
using bw::rhi::TextureDimension;
using bw_rhi_test::require;

TextureDescription texture(Format format, bw::rhi::Extent3 extent, TextureDimension dimension) {
  TextureDescription description{};
  description.extent = extent;
  description.format = format;
  description.dimension = dimension;
  return description;
}

// (synchronization.hpp, staging_layout) and
// (synchronization.hpp, kStagingRowAlignment): rows of row_bytes(format, width)
// at a pitch rounded up to 256 bytes, slices at row_pitch * rows, a mip's
// extent halved per level and clamped to 1, depth only for a volume.
void staging_layout_pitch_and_bytes() {
  const auto rgba = bw::rhi::staging_layout(
      texture(Format::R8G8B8A8Unorm, {100, 10, 1}, TextureDimension::Texture2D), 0);
  require(rgba.row_pitch == 512U, "400 row bytes round up to 512");
  require(rgba.rows == 10U && rgba.slice_pitch == 5120U && rgba.total_bytes == 5120U,
          "ten rows of 512");
  require(rgba.width == 100U && rgba.height == 10U && rgba.depth == 1U, "mip 0 extent");

  const auto small = bw::rhi::staging_layout(
      texture(Format::R8G8B8A8Unorm, {16, 16, 1}, TextureDimension::Texture2D), 4);
  require(small.width == 1U && small.height == 1U, "mip 4 of 16x16 is 1x1");
  require(small.row_pitch == bw::rhi::kStagingRowAlignment && small.total_bytes == 256U,
          "one texel still takes one aligned row");

  const auto bc1 = bw::rhi::staging_layout(
      texture(Format::BC1Unorm, {256, 64, 1}, TextureDimension::Texture2D), 0);
  require(bc1.rows == 16U, "64 texel rows are 16 block rows");
  require(bc1.row_pitch == 512U, "64 blocks of 8 bytes is 512");

  const auto volume =
      bw::rhi::staging_layout(texture(Format::R32Float, {8, 8, 4}, TextureDimension::Texture3D), 1);
  require(volume.depth == 2U, "a volume's depth halves per mip");
  require(volume.row_pitch == 256U && volume.slice_pitch == 1024U && volume.total_bytes == 2048U,
          "a volume mip is depth slices of row_pitch * rows");

  const auto flat = bw::rhi::staging_layout(
      texture(Format::R32Float, {8, 8, 4}, TextureDimension::Texture2DArray), 0);
  require(flat.depth == 1U, "a non-volume has depth 1");
}

// (synchronization.hpp, GpuFence) and (synchronization.hpp, fence_wait_error):
// a wait on an Unwritten fence is FenceNotWritten, on a Pending one
// FenceNotSubmitted; Submitted and Signaled can be waited on.
void fence_wait_error_by_state() {
  using bw::rhi::FenceState;
  using bw::rhi::StagingError;
  require(bw::rhi::fence_wait_error(FenceState::Unwritten) == StagingError::FenceNotWritten,
          "Unwritten");
  require(bw::rhi::fence_wait_error(FenceState::Pending) == StagingError::FenceNotSubmitted,
          "Pending");
  require(bw::rhi::fence_wait_error(FenceState::Submitted) == StagingError::None, "Submitted");
  require(bw::rhi::fence_wait_error(FenceState::Signaled) == StagingError::None, "Signaled");
}

constexpr std::array scenarios{
    bw_rhi_test::Scenario{"staging_layout_pitch_and_bytes", staging_layout_pitch_and_bytes},
    bw_rhi_test::Scenario{"fence_wait_error_by_state", fence_wait_error_by_state},
};
} // namespace

int main(int argc, char** argv) { return bw_rhi_test::run_scenario(argc, argv, scenarios); }
