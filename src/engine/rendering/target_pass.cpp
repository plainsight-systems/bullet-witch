#include "bw/rendering/target_pass.hpp"

#include "bw/rendering/rhi/access.hpp"
#include "bw/rendering/rhi/command_list.hpp"
#include "bw/rendering/rhi/device.hpp"
#include "bw/rendering/rhi/format.hpp"
#include "bw/rendering/rhi/pipeline_states.hpp"
#include "bw/rendering/rhi/resources.hpp"
#include "bw/rendering/rhi/result.hpp"

#include <string_view>
#include <utility>

// Responsibility: the out-of-line part of bw/rendering/target_pass.hpp.
namespace bw::rendering {

rhi::Status<rhi::ResourceError> resize_depth_target(rhi::Device& device, rhi::Extent2 extent,
                                                    rhi::Format format, std::string_view debug_name,
                                                    rhi::Ref<rhi::Texture>& target) {
  const rhi::Extent3 wanted{extent.width, extent.height, 1};
  if (target && target->description().extent == wanted) {
    return {};
  }
  rhi::TextureDescription description{};
  description.extent = wanted;
  description.format = format;
  description.usage = rhi::TextureUsage::DepthStencil;
  description.initial_access = rhi::Access::DepthStencilWrite;
  description.clear_value.depth = 1.0F;
  description.debug_name = debug_name;
  rhi::TextureResult created = device.create_texture(description);
  if (!created.ok()) {
    return {created.error(), created.detail()};
  }
  target = std::move(created.value());
  return {};
}

void begin_cleared_pass(rhi::CommandList& list, rhi::Texture& color, rhi::Texture* depth,
                        rhi::Extent2 extent, std::string_view name) {
  rhi::RenderPassDescription pass{};
  pass.colors[0] = rhi::ColorTarget{&color, 0, 0, rhi::LoadAction::Clear, rhi::StoreAction::Store};
  pass.color_count = 1;
  if (depth != nullptr) {
    pass.depth_stencil.texture = depth;
    pass.depth_stencil.depth_load = rhi::LoadAction::Clear;
    pass.depth_stencil.depth_store = rhi::StoreAction::NoAction;
  }
  pass.name = name;
  list.begin_render_pass(pass);
  list.set_viewport(rhi::ViewportRect{0.0F, 0.0F, static_cast<float>(extent.width),
                                      static_cast<float>(extent.height), 0.0F, 1.0F});
  list.set_scissor(rhi::ScissorRect{0, 0, extent.width, extent.height});
}

} // namespace bw::rendering
