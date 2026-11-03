// Unit test: public contracts of bw/rendering/rhi/format.hpp, one scenario
// per CTest entry. Each scenario names the header lines it asserts.
#include "bw/rendering/rhi/format.hpp"

#include "test_main.hpp"

#include <array>
#include <cstdint>

namespace {
using bw::rhi::Extent2;
using bw::rhi::Format;
using bw_rhi_test::require;

// format.hpp:12-14 and 87-98: every enumerator has a row, Unknown has
// block_bytes 0. :107-110 and 214-230: row_bytes and slice_bytes count whole
// blocks, a partial block counting whole.
void block_table_and_byte_counts() {
  require(bw::rhi::format_info(Format::Unknown).block_bytes == 0U, "Unknown has no bytes");
  for (std::uint32_t value = 1; value < static_cast<std::uint32_t>(Format::Count); ++value) {
    const auto info = bw::rhi::format_info(static_cast<Format>(value));
    require(info.block_bytes != 0U && info.block_width != 0U && info.block_height != 0U,
            "every format has a row");
  }
  const auto bc1 = bw::rhi::format_info(Format::BC1Unorm);
  require(bc1.block_width == 4U && bc1.block_height == 4U && bc1.block_bytes == 8U &&
              bc1.is_compressed,
          "BC1 is 4x4 blocks of 8 bytes");
  require(bw::rhi::format_info(Format::BC7UnormSrgb).is_srgb, "BC7 sRGB is sRGB");
  const auto d24s8 = bw::rhi::format_info(Format::D24UnormS8Uint);
  require(d24s8.is_depth && d24s8.has_stencil, "D24S8 has depth and stencil");
  require(!bw::rhi::format_info(Format::D32Float).has_stencil, "D32 has no stencil");

  require(bw::rhi::row_bytes(Format::R8G8B8A8Unorm, 3) == 12U, "three RGBA8 texels");
  require(bw::rhi::row_bytes(Format::BC1Unorm, 5) == 16U, "five texels are two BC1 blocks");
  require(bw::rhi::row_bytes(Format::BC1Unorm, 1) == 8U, "a partial block counts whole");
  require(bw::rhi::slice_bytes(Format::BC3Unorm, Extent2{5, 5}) == 64U,
          "5x5 BC3 is 2x2 blocks of 16 bytes");
  require(bw::rhi::slice_bytes(Format::R16G16B16A16Float, Extent2{4, 2}) == 64U,
          "4x2 RGBA16F is 64 bytes");
  require(bw::rhi::row_bytes(Format::Unknown, 16) == 0U, "Unknown counts no bytes");
}

constexpr std::array scenarios{
    bw_rhi_test::Scenario{"block_table_and_byte_counts", block_table_and_byte_counts},
};
} // namespace

int main(int argc, char** argv) { return bw_rhi_test::run_scenario(argc, argv, scenarios); }
