#ifndef BW_RENDERING_TARGET_PASS_HPP
#define BW_RENDERING_TARGET_PASS_HPP

#include "bw/rendering/rhi/command_list.hpp"
#include "bw/rendering/rhi/device.hpp"
#include "bw/rendering/rhi/format.hpp"
#include "bw/rendering/rhi/resources.hpp"
#include "bw/rendering/rhi/result.hpp"

#include <string_view>

// Responsibility: the two steps every draw into a window or an offscreen
// target repeats before its first draw call: a depth target that follows
// the color target's size, and a pass that clears both and covers the
// whole target. The host and the offscreen test both begin their pass
// here, so neither writes a depth target description or a pass description
// of its own.
//
// Contract.
//   1. resize_depth_target leaves `target` alone when it is set and its
//      extent is `extent`. Otherwise it creates a texture of `extent`, one
//      mip, `format`, usage DepthStencil, initial access DepthStencilWrite,
//      clear depth 1 (the far plane), named `debug_name`, and replaces
//      `target` with it; the replaced texture is released through its Ref,
//      so the device keeps it until the frames that drew to it complete.
//      On a failure `target` is unchanged and the device's error and
//      detail are returned.
//   2. begin_cleared_pass begins a render pass named `name` with one color
//      target, `color`, cleared to its clear value and stored, and, when
//      `depth` is not null, that depth target cleared to its clear value
//      and not stored; then sets the viewport to (0, 0, extent.width,
//      extent.height, depth 0 to 1) and the scissor to the same rectangle.
//      The caller records its draws and ends the pass.
//      Preconditions: `list` is not inside a pass; `color` is in the
//      RenderTarget access and `depth` in DepthStencilWrite; both have the
//      extent `extent`.
//
// Read guidance: F.2 (making a target and beginning a pass are two
// functions), I.1, E.27.
namespace bw::rendering {

[[nodiscard]] rhi::Status<rhi::ResourceError>
resize_depth_target(rhi::Device& device, rhi::Extent2 extent, rhi::Format format,
                    std::string_view debug_name, rhi::Ref<rhi::Texture>& target);

void begin_cleared_pass(rhi::CommandList& list, rhi::Texture& color, rhi::Texture* depth,
                        rhi::Extent2 extent, std::string_view name);

} // namespace bw::rendering

#endif
