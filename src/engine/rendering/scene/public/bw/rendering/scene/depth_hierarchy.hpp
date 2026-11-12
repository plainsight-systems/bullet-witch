#ifndef BW_RENDERING_SCENE_DEPTH_HIERARCHY_HPP
#define BW_RENDERING_SCENE_DEPTH_HIERARCHY_HPP

#include "bw/rendering/graph/builder.hpp"
#include "bw/rendering/graph/declaration.hpp"
#include "bw/rendering/rhi/device.hpp"
#include "bw/rendering/rhi/format.hpp"
#include "bw/rendering/rhi/resources.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

// Responsibility: the depth hierarchy (docs/architecture/scene.md, "Depth
// hierarchy", invariants 9, 10 and 12): a texture whose every level holds
// the farthest depth under each texel, built from the depth target twice a
// frame; the build after phase two is the history the next frame's phase
// one reads.
// The hierarchy is one 16-bit float channel, refreshed between the two
// phases of one frame. At an odd view size the last level 0 texel reads the
// last column or row twice, which still keeps the farthest depth of the
// texel's own pixels. The chain runs down to a one-texel level. Every
// target builds it in compute.
//
// Contract.
//   1. Depth is 0 at the near plane and 1 at the far plane
//      (bw/math/matrix/matrix4.hpp), so the farthest depth is the largest.
//   2. depth_hierarchy_extent(view) is, per axis, the view's size rounded
//      up to a power of two, halved, and at least 1.
//      depth_hierarchy_levels(extent) is floor(log2(max(width, height))) +
//      1: a full chain down to one texel. The format is
//      kDepthHierarchyFormat.
//   3. A build fills every level of its texture from a depth target of the
//      view's size. Level 0 texel (x, y) is the largest of the depth
//      texels (2x, 2y), (2x + 1, 2y), (2x, 2y + 1) and (2x + 1, 2y + 1),
//      each coordinate clamped to the view's last column and row. Level k
//      texel (x, y) is the largest of level k - 1's texels (2x, 2y) to
//      (2x + 1, 2y + 1), clamped to that level's size. A value is stored
//      rounded to the 16-bit float nearest it.
//   4. A DepthHierarchy owns two textures of the extent and level count of
//      contract 2: the history and the first build. begin_frame(device,
//      view) makes or remakes both when the view's size is not the one
//      they were made for, with the device's failure returned and nothing
//      usable on one: both textures null, extent and levels zero. A build
//      reads the frame's depth through a view made while the frame is
//      recorded; when the device refuses that view the build records
//      nothing, its texture keeps what it held, so that frame's phase two
//      tests depth that is not its own, and the next begin_frame returns
//      ViewFailed before anything else. has_history() is true when the
//      frame before declared
//      a second build, the size has not changed since and invalidate() was
//      not called; the owner calls invalidate() when a frame's graph did
//      not execute.
//   5. declare_depth_hierarchy(hierarchy, graph) imports both textures
//      into the frame and marks the history as leaving it; the two handles
//      are the frame's. declare_depth_hierarchy_build(hierarchy, graph,
//      frame, depth, build) declares the compute work that fills that
//      build's
//      texture from `depth`, which the work reads and does not write; the
//      number of passes is not part of the contract. A frame declares the
//      first build after the depth pass's first draw and the second after
//      its second draw; nothing else writes either texture.
//   6. create_depth_hierarchy reads the build's compute artifacts from
//      `shader_directory` and makes its pipelines; a failure is returned
//      by name with a detail text and `out` is left empty.
//
// Core guidelines read: I.1, E.27, R.1, C.21, Con.5. Performance
// guidelines read: GPU.9 (two textures kept for the scene's life, remade
// only on a size change), GPU.1 (nothing is read back).
namespace bw::scene {

inline constexpr rhi::Format kDepthHierarchyFormat = rhi::Format::R16Float;

[[nodiscard]] rhi::Extent2 depth_hierarchy_extent(rhi::Extent2 view) noexcept;
[[nodiscard]] std::uint32_t depth_hierarchy_levels(rhi::Extent2 extent) noexcept;

enum class DepthHierarchyError : std::uint8_t {
  None,
  ArtifactFailed,
  PipelineFailed,
  TextureFailed,
  // The scratch buffer the levels are reduced in, or its view.
  BufferFailed,
  ViewFailed,
};

struct DepthHierarchyFailure {
  DepthHierarchyError error{DepthHierarchyError::None};
  std::string detail{};
  [[nodiscard]] bool ok() const noexcept { return error == DepthHierarchyError::None; }
};

enum class DepthHierarchyBuild : std::uint8_t { First, Second };

// The frame's two imports.
struct DepthHierarchyFrame {
  graph::TextureHandle history{};
  graph::TextureHandle first_build{};
};

class DepthHierarchy {
public:
  DepthHierarchy();
  DepthHierarchy(const DepthHierarchy&) = delete;
  DepthHierarchy& operator=(const DepthHierarchy&) = delete;
  DepthHierarchy(DepthHierarchy&&) noexcept;
  DepthHierarchy& operator=(DepthHierarchy&&) noexcept;
  ~DepthHierarchy();

  [[nodiscard]] DepthHierarchyFailure begin_frame(rhi::Device& device, rhi::Extent2 view);
  [[nodiscard]] bool has_history() const noexcept;
  void invalidate() noexcept;

  // The size and level count of both textures; zero before the first
  // begin_frame and after one that failed.
  [[nodiscard]] rhi::Extent2 extent() const noexcept;
  [[nodiscard]] std::uint32_t levels() const noexcept;
  // The textures, for a reader that samples one: null before the first
  // begin_frame.
  [[nodiscard]] rhi::Texture* history() const noexcept;
  [[nodiscard]] rhi::Texture* first_build() const noexcept;

private:
  friend struct DepthHierarchyAccess;
  struct State;
  std::unique_ptr<State> state_;
};

[[nodiscard]] DepthHierarchyFailure
create_depth_hierarchy(rhi::Device& device, const std::filesystem::path& shader_directory,
                       DepthHierarchy& out);

[[nodiscard]] DepthHierarchyFrame declare_depth_hierarchy(DepthHierarchy& hierarchy,
                                                          graph::FrameGraph& graph);

void declare_depth_hierarchy_build(DepthHierarchy& hierarchy, graph::FrameGraph& graph,
                                   const DepthHierarchyFrame& frame, graph::TextureHandle depth,
                                   DepthHierarchyBuild build);

} // namespace bw::scene

#endif
