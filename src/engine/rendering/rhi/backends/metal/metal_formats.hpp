#ifndef BW_RENDERING_RHI_BACKENDS_METAL_METAL_FORMATS_HPP
#define BW_RENDERING_RHI_BACKENDS_METAL_METAL_FORMATS_HPP
// Translation of the RHI's enumerations into Metal's, and the per-format
// capability table the backend reports (format.hpp, FormatCapability). Pure
// functions of their arguments; nothing here creates an API object.
//
// Contract: every RHI enumerator maps to exactly one Metal value; a Format
// with no Metal pixel format maps to MTL::PixelFormatInvalid and has no
// capability. VertexAttribute is reported for no format in this slice: no
// pipeline of this backend takes a vertex layout (pipeline_states.hpp,
// VertexLayoutUnsupported).
#include "bw/rendering/rhi/command_list.hpp"
#include "bw/rendering/rhi/format.hpp"
#include "bw/rendering/rhi/pipeline_states.hpp"
#include "bw/rendering/rhi/resources.hpp"

#include "metal_api.hpp"

namespace bw::rhi::metal {

// What the device says about the formats whose support varies by device.
struct FormatSupport {
  bool depth24_stencil8{};
  bool bc_compression{};
};

[[nodiscard]] MTL::PixelFormat pixel_format(Format format) noexcept;
[[nodiscard]] FormatCapability format_capability(Format format, FormatSupport support) noexcept;

[[nodiscard]] MTL::CompareFunction compare_function(CompareFunction function) noexcept;
[[nodiscard]] MTL::StencilOperation stencil_operation(StencilOp operation) noexcept;
[[nodiscard]] MTL::BlendOperation blend_operation(BlendOperation operation) noexcept;
[[nodiscard]] MTL::BlendFactor blend_factor(BlendFactor factor) noexcept;
[[nodiscard]] MTL::ColorWriteMask color_write_mask(ColorWriteMask mask) noexcept;
[[nodiscard]] MTL::SamplerAddressMode address_mode(AddressMode mode) noexcept;
[[nodiscard]] MTL::SamplerBorderColor border_color(BorderColor color) noexcept;
[[nodiscard]] MTL::PrimitiveType primitive_type(PrimitiveType type) noexcept;
[[nodiscard]] MTL::CullMode cull_mode(CullMode mode) noexcept;
[[nodiscard]] MTL::TriangleFillMode fill_mode(FillMode mode) noexcept;
[[nodiscard]] MTL::DepthClipMode depth_clip_mode(DepthClipMode mode) noexcept;
[[nodiscard]] MTL::LoadAction load_action(LoadAction action) noexcept;
[[nodiscard]] MTL::StoreAction store_action(StoreAction action) noexcept;
[[nodiscard]] MTL::IndexType index_type(IndexFormat format) noexcept;
[[nodiscard]] NS::UInteger index_bytes(IndexFormat format) noexcept;

// The texture type of a texture or a view: dimension, array size and sample
// count together pick it.
struct TextureShape {
  TextureDimension dimension{TextureDimension::Texture2D};
  std::uint32_t array_size{1};
  std::uint32_t sample_count{1};
};
[[nodiscard]] MTL::TextureType texture_type(TextureShape shape) noexcept;

// Whether a blend target does anything other than write the source (the
// pin's blending-enabled test).
[[nodiscard]] bool blending_enabled(const RenderTargetBlend& blend) noexcept;

} // namespace bw::rhi::metal
#endif
