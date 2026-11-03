// Unit test: public contracts of bw/rendering/rhi/access.hpp, one scenario
// per CTest entry. Each scenario names the header lines it asserts.
#include "bw/rendering/rhi/access.hpp"

#include "test_main.hpp"

#include <array>

namespace {
using bw::rhi::Access;
using bw::rhi::Pipeline;
using bw_rhi_test::require;

// access.hpp:12-17 and 83-132: a legal access is any set of read-only bits,
// one write-only bit, the mergeable set (read-only with unordered access),
// or Discard alone; Unknown is never legal; two writable bits, or a write
// with a read, are not.
void masks_and_valid_access() {
  require(!bw::rhi::is_valid_access(Access::Unknown), "Unknown is not a legal access");
  require(bw::rhi::is_valid_access(Access::Discard), "Discard alone is legal");
  require(bw::rhi::is_valid_access(Access::CopySource | Access::ShaderResourcePixel |
                                   Access::VertexOrIndexBuffer),
          "any set of read-only bits is legal");
  require(bw::rhi::is_valid_access(Access::DepthStencilRead | Access::ShaderResourcePixel),
          "depth read with shader reads is legal");
  require(bw::rhi::is_valid_access(Access::ShaderResourceCompute | Access::UnorderedAccessCompute),
          "read-only bits merge with unordered access");
  require(bw::rhi::is_valid_access(Access::RenderTarget), "one write-only bit is legal");
  require(bw::rhi::is_valid_access(Access::CopyDestination), "one write-only bit is legal");
  require(!bw::rhi::is_valid_access(Access::RenderTarget | Access::CopyDestination),
          "two write-only bits are not legal");
  require(!bw::rhi::is_valid_access(Access::RenderTarget | Access::ShaderResourcePixel),
          "a write with a read is not legal");
  require(!bw::rhi::is_valid_access(Access::Discard | Access::CopySource),
          "Discard combined with anything is not legal");

  require(bw::rhi::is_read_only(Access::CopySource | Access::DepthStencilRead),
          "read-only bits are read-only");
  require(!bw::rhi::is_read_only(Access::UnorderedAccessGraphics), "unordered access writes");
  require(bw::rhi::is_write_only(Access::DepthStencilWrite), "depth write is write-only");
  require(bw::rhi::is_writable(Access::UnorderedAccessCompute), "unordered access is writable");
  require(bw::rhi::is_readable(Access::UnorderedAccessCompute), "unordered access is readable");
  require(!bw::rhi::is_readable(Access::RenderTarget), "a render target is not readable");
  require(bw::rhi::kMergeableMask == (bw::rhi::kReadOnlyMask | bw::rhi::kUnorderedAccessMask),
          "the mergeable mask is read-only plus unordered access");
}

// access.hpp:17-18 and 143-157: Pipeline is a bit set over the two queues;
// All is both; a single pipeline is exactly one of them. :167-171: a tracked
// access starts Unknown on Graphics.
void pipeline_bits() {
  require((Pipeline::Graphics | Pipeline::AsyncCompute) == Pipeline::All, "All is both queues");
  require((Pipeline::All & Pipeline::Graphics) == Pipeline::Graphics, "All contains Graphics");
  require(bw::rhi::is_single_pipeline(Pipeline::Graphics), "Graphics is one pipeline");
  require(bw::rhi::is_single_pipeline(Pipeline::AsyncCompute), "AsyncCompute is one pipeline");
  require(!bw::rhi::is_single_pipeline(Pipeline::All), "All is not one pipeline");
  require(!bw::rhi::is_single_pipeline(Pipeline::None), "None is not one pipeline");
  const bw::rhi::TrackedAccess tracked{};
  require(tracked.access == Access::Unknown && tracked.pipelines == Pipeline::Graphics,
          "a tracked access starts Unknown on Graphics");
}

constexpr std::array scenarios{
    bw_rhi_test::Scenario{"masks_and_valid_access", masks_and_valid_access},
    bw_rhi_test::Scenario{"pipeline_bits", pipeline_bits},
};
} // namespace

int main(int argc, char** argv) { return bw_rhi_test::run_scenario(argc, argv, scenarios); }
