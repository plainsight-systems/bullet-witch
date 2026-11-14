#ifndef BW_RENDERING_POSTPROCESS_BLOOM_HPP
#define BW_RENDERING_POSTPROCESS_BLOOM_HPP

#include "bw/rendering/graph/builder.hpp"
#include "bw/rendering/graph/declaration.hpp"
#include "bw/rendering/rhi/device.hpp"
#include "bw/rendering/rhi/format.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

// Responsibility: the bloom chain (docs/architecture/post-processing.md,
// invariants 6 and 12): scene colour, unthresholded, taken down six levels
// by a thirteen-fetch filter with fireflies averaged on the first, and back
// up by a tent filter that sums the levels. Tone mapping adds the result
// (tone_mapping.hpp).
//
// Contract.
//   1. Levels. There are kBloomLevels levels of kBloomFormat. Level 0 is
//      scene colour's size halved and level k is level k - 1's halved;
//      halving rounds up and no axis goes below 1 (bloom_level_extent).
//      The result is a texture of level 0's size and kBloomFormat.
//   2. What the filter keeps. A field of one colour gives that colour in
//      the result; and the result of an image that equals its own mirror
//      in x or in y equals its mirror too. The result is pre-exposed as
//      scene colour is: the chain neither divides the factor out nor
//      multiplies it in.
//   3. declare_bloom(bloom, device, graph, scene_color, size, result)
//      declares the raster work that makes the result from `scene_color`
//      of `size` and gives its handle, a frame-only texture; the number of
//      passes is not part of the contract. A pass reads its source through
//      a view made while the frame is recorded; when the device refuses
//      one, that pass and the passes after it record nothing;
//      record_failure() is then ViewFailed until the next declare_bloom,
//      which returns it before anything else, with nothing declared. A
//      caller reads record_failure() after the frame's graph is recorded
//      and does not execute a frame that has one (post_chain.hpp,
//      end_post_frame).
//   4. create_bloom reads the three artifacts from `shader_directory` and
//      makes the sampler and the two pipelines; a failure is returned by
//      name with a detail text and `out` is left empty.
//   There is no threshold, no dirt mask and no lens flare.
//
// The filter, which the two kernels state and contract 2 is what a test
// holds of it.
//   Down. A level is filtered from the image above it (scene colour for
//   level 0) by thirteen bilinear fetches, clamped at the edge, around the
//   point of the source under the destination texel's centre, at these
//   offsets in source texels, in five groups of four:
//     centre group, weight 0.5:  (-1, -1), (1, -1), (-1, 1), (1, 1)
//     four groups, weight 0.125 each, the two by two blocks of the grid
//     {-2, 0, 2} squared that share the fetch at (0, 0):
//       (-2, -2), (0, -2), (-2, 0), (0, 0)
//       (0, -2), (2, -2), (0, 0), (2, 0)
//       (-2, 0), (0, 0), (-2, 2), (0, 2)
//       (0, 0), (2, 0), (0, 2), (2, 2)
//   A group's value is the mean of its four fetches, and the texel is the
//   groups' weighted sum; the weights sum to 1. On level 0 alone a group's
//   value is instead the weighted mean of its four fetches with the weight
//   1 / (1 + Y), Y the Rec. 709 luminance (0.2126, 0.7152, 0.0722) of the
//   fetch as stored: a firefly is averaged down inside its group and the
//   groups still combine linearly.
//   Up. With D_k the levels and s = 1 / kBloomLevels, U_5 is s * D_5 and,
//   for k from 4 down to 0, U_k is s * D_k plus U_(k+1) filtered at U_k's
//   size by the three by three tent (1, 2, 1; 2, 4, 2; 1, 2, 1) / 16 of
//   bilinear fetches at offsets -1, 0 and 1 times kBloomTentRadius in
//   U_(k+1)'s texels, clamped at the edge. The result is U_0. Each level
//   carries its sixth as it is added, so no sum on the way up is larger
//   than the field it came from and none passes what kBloomFormat holds.
//
// Core guidelines read: I.1, E.27, C.21, Con.5, ES.45. Performance
// guidelines read: GPU.9 (the levels are the graph's frame-only textures;
// the pipelines and the sampler are made once).
namespace bw::postprocess {

inline constexpr std::uint32_t kBloomLevels = 6;
inline constexpr rhi::Format kBloomFormat = rhi::Format::R11G11B10Float;
inline constexpr float kBloomTentRadius = 1.0F;

// Contract 1. Precondition: level < kBloomLevels.
[[nodiscard]] constexpr rhi::Extent2 bloom_level_extent(rhi::Extent2 size,
                                                        std::uint32_t level) noexcept {
  rhi::Extent2 extent = size;
  for (std::uint32_t step = 0; step <= level; ++step) {
    extent.width = extent.width > 1U ? (extent.width + 1U) / 2U : 1U;
    extent.height = extent.height > 1U ? (extent.height + 1U) / 2U : 1U;
  }
  return extent;
}

enum class BloomError : std::uint8_t {
  None,
  ArtifactFailed,
  StateFailed,
  PipelineFailed,
  ViewFailed,
};

struct BloomFailure {
  BloomError error{BloomError::None};
  std::string detail{};
  [[nodiscard]] bool ok() const noexcept { return error == BloomError::None; }
};

class Bloom {
public:
  Bloom();
  Bloom(const Bloom&) = delete;
  Bloom& operator=(const Bloom&) = delete;
  Bloom(Bloom&&) noexcept;
  Bloom& operator=(Bloom&&) noexcept;
  ~Bloom();

  // Contract 3.
  [[nodiscard]] BloomFailure record_failure() const;

private:
  friend struct BloomAccess;
  struct State;
  std::unique_ptr<State> state_;
};

[[nodiscard]] BloomFailure create_bloom(rhi::Device& device,
                                        const std::filesystem::path& shader_directory, Bloom& out);

// Precondition: `bloom` was made by a create_bloom that returned no
// failure.
[[nodiscard]] BloomFailure declare_bloom(Bloom& bloom, rhi::Device& device,
                                         graph::FrameGraph& graph, graph::TextureHandle scene_color,
                                         rhi::Extent2 size, graph::TextureHandle& result);

} // namespace bw::postprocess

#endif
